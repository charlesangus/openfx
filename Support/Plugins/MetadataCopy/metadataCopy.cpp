// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

#include <cctype>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "ofxsImageEffect.h"
#include "ofxsMetadata.h"

namespace {

  const char kModeParam[]           = "mode";
  const char kSourceFilterParam[]   = "sourceFilter";
  const char kSourceFilterModeParam[] = "sourceFilterMode";
  const char kMaskFilterParam[]     = "maskFilter";
  const char kMaskFilterModeParam[] = "maskFilterMode";

  const char kSourceClip[] = kOfxImageEffectSimpleSourceClipName;
  const char kMaskClip[]   = "Mask";

  enum ModeEnum {
    eModeSourceOnly,
    eModeMaskOnly,
    eModeSourceOverMask,
    eModeMaskOverSource
  };

  enum FilterModeEnum {
    eFilterModeKeysAndValues,
    eFilterModeKeysOnly,
    eFilterModeValuesOnly
  };

  char lowerCase(char c)
  {
    return char(tolower((unsigned char) c));
  }

  /** @brief does the whole of text match pattern, ignoring case, '*' standing for any run
  of characters including none? An empty pattern matches only empty text */
  bool globMatchNoCase(const std::string &text, const std::string &pattern)
  {
    size_t t = 0, p = 0, afterStar = std::string::npos, retry = 0;

    while(t < text.size()) {
      if(p < pattern.size() && pattern[p] == '*') {
        afterStar = ++p;
        retry = t;
      }
      else if(p < pattern.size() && lowerCase(pattern[p]) == lowerCase(text[t])) {
        p++;
        t++;
      }
      else if(afterStar != std::string::npos) {
        p = afterStar;
        t = ++retry;
      }
      else {
        return false;
      }
    }

    while(p < pattern.size() && pattern[p] == '*')
      p++;

    return p == pattern.size();
  }

  /** @brief every value of a key, comma separated, read back as the type the host holds it as */
  std::string valueText(const OFX::MetadataSet &metadata, const OFX::MetadataEntry &entry)
  {
    std::ostringstream text;

    for(int i = 0; i < entry.dimension; i++) {
      if(i)
        text << ",";

      switch(entry.type) {
      case OFX::eMetadataTypeInt    : text << metadata.getInt(entry.key, i); break;
      case OFX::eMetadataTypeDouble : text << metadata.getDouble(entry.key, i); break;
      default                       : text << metadata.getString(entry.key, i); break;
      }
    }

    return text.str();
  }

  bool matchesFilter(const OFX::MetadataSet &metadata,
                     const OFX::MetadataEntry &entry,
                     const std::string &pattern,
                     FilterModeEnum mode)
  {
    if(pattern.empty())
      return true;

    switch(mode) {
    case eFilterModeKeysOnly   : return globMatchNoCase(entry.key, pattern);
    case eFilterModeValuesOnly : return globMatchNoCase(valueText(metadata, entry), pattern);
    default                    : return globMatchNoCase(entry.key, pattern)
                                     || globMatchNoCase(valueText(metadata, entry), pattern);
    }
  }

  int bytesPerPixel(const OFX::Image &image)
  {
    int perComponent = 0;

    switch(image.getPixelDepth()) {
    case OFX::eBitDepthUByte  : perComponent = 1; break;
    case OFX::eBitDepthUShort : perComponent = 2; break;
    case OFX::eBitDepthHalf   : perComponent = 2; break;
    case OFX::eBitDepthFloat  : perComponent = 4; break;
    default : return 0;
    }

    return perComponent * image.getPixelComponentCount();
  }

  void copyPixels(const OFX::Image &src, OFX::Image &dst, const OfxRectI &window)
  {
    const int pixelBytes = bytesPerPixel(dst);

    if(pixelBytes == 0 || pixelBytes != bytesPerPixel(src))
      OFX::throwSuiteStatusException(kOfxStatErrImageFormat);

    for(int y = window.y1; y < window.y2; y++) {
      for(int x = window.x1; x < window.x2; x++) {
        void *to = dst.getPixelAddress(x, y);

        if(!to)
          continue;

        if(const void *from = src.getPixelAddress(x, y))
          memcpy(to, from, size_t(pixelBytes));
        else
          memset(to, 0, size_t(pixelBytes));
      }
    }
  }

}

////////////////////////////////////////////////////////////////////////////////
/** @brief takes its image from Source and its metadata from either input or from both
combined, each input's contribution first narrowed by a filter of its own, and passes the
image through untouched */
class MetadataCopyPlugin : public OFX::ImageEffect {
protected :
  // do not need to delete these, the ImageEffect is managing them for us
  OFX::Clip *dstClip_;
  OFX::Clip *srcClip_;
  OFX::Clip *maskClip_;

  OFX::ChoiceParam *mode_;
  OFX::StringParam *sourceFilter_;
  OFX::ChoiceParam *sourceFilterMode_;
  OFX::StringParam *maskFilter_;
  OFX::ChoiceParam *maskFilterMode_;

public :
  /** @brief ctor */
  MetadataCopyPlugin(OfxImageEffectHandle handle)
    : ImageEffect(handle)
    , dstClip_(0)
    , srcClip_(0)
    , maskClip_(0)
    , mode_(0)
    , sourceFilter_(0)
    , sourceFilterMode_(0)
    , maskFilter_(0)
    , maskFilterMode_(0)
  {
    dstClip_  = fetchClip(kOfxImageEffectOutputClipName);
    srcClip_  = fetchClip(kSourceClip);
    maskClip_ = fetchClip(kMaskClip);

    mode_             = fetchChoiceParam(kModeParam);
    sourceFilter_     = fetchStringParam(kSourceFilterParam);
    sourceFilterMode_ = fetchChoiceParam(kSourceFilterModeParam);
    maskFilter_       = fetchStringParam(kMaskFilterParam);
    maskFilterMode_   = fetchChoiceParam(kMaskFilterModeParam);
  }

  /* Override the render */
  virtual void render(const OFX::RenderArguments &args);

  /* Override getMetadata */
  virtual bool getMetadata(const OFX::MetadataArguments &args, OFX::MetadataSetBuilder &metadata, OFX::MetadataInheritanceSetter &inheritance);

protected :
  /* narrow what is inherited from one clip to the keys its own filter matches */
  void retainMatchingKeys(double time,
                          OFX::Clip &clip,
                          OFX::StringParam &filterParam,
                          OFX::ChoiceParam &filterModeParam,
                          OFX::MetadataInheritanceSetter &inheritance);
};

void
MetadataCopyPlugin::retainMatchingKeys(double time,
                                       OFX::Clip &clip,
                                       OFX::StringParam &filterParam,
                                       OFX::ChoiceParam &filterModeParam,
                                       OFX::MetadataInheritanceSetter &inheritance)
{
  std::string pattern;
  int filterMode = eFilterModeKeysAndValues;

  filterParam.getValue(pattern);
  filterModeParam.getValue(filterMode);

  // The host fills in a retained-keys list only for the first input clip the effect
  // described and leaves every other clip's empty, so the candidates have to come from
  // the clip's own metadata: filtering what getRetainedKeys reports would contribute
  // nothing at all from Mask.
  const OFX::MetadataSet metadata = clip.getMetadata(time);
  const std::vector<OFX::MetadataEntry> entries = metadata.entries();

  std::vector<std::string> kept;

  for(size_t i = 0; i < entries.size(); i++) {
    if(matchesFilter(metadata, entries[i], pattern, FilterModeEnum(filterMode)))
      kept.push_back(entries[i].key);
  }

  inheritance.setRetainedKeys(clip, kept);
}

bool
MetadataCopyPlugin::getMetadata(const OFX::MetadataArguments &args, OFX::MetadataSetBuilder &/*metadata*/, OFX::MetadataInheritanceSetter &inheritance)
{
  if(!OFX::getImageEffectHostDescription()->supportsMetadata)
    return false;

  int mode = eModeSourceOverMask;
  mode_->getValue(mode);

  // the list is read in increasing precedence, so where both inputs carry a key the one
  // named last is the one that survives
  std::vector<std::string> clips;

  switch(ModeEnum(mode)) {
  case eModeSourceOnly :
    clips.push_back(kSourceClip);
    break;
  case eModeMaskOnly :
    clips.push_back(kMaskClip);
    break;
  case eModeMaskOverSource :
    clips.push_back(kSourceClip);
    clips.push_back(kMaskClip);
    break;
  default :
    clips.push_back(kMaskClip);
    clips.push_back(kSourceClip);
    break;
  }

  inheritance.setSourceClips(clips);

  for(size_t i = 0; i < clips.size(); i++) {
    if(clips[i] == kSourceClip)
      retainMatchingKeys(args.time, *srcClip_, *sourceFilter_, *sourceFilterMode_, inheritance);
    else
      retainMatchingKeys(args.time, *maskClip_, *maskFilter_, *maskFilterMode_, inheritance);
  }

  return true;
}

// the overridden render function
void
MetadataCopyPlugin::render(const OFX::RenderArguments &args)
{
  std::unique_ptr<OFX::Image> dst(dstClip_->fetchImage(args.time));
  std::unique_ptr<OFX::Image> src(srcClip_->fetchImage(args.time));

  if(!dst.get() || !src.get())
    return;

  if(src->getPixelDepth() != dst->getPixelDepth()
     || src->getPixelComponents() != dst->getPixelComponents())
    OFX::throwSuiteStatusException(kOfxStatErrImageFormat);

  copyPixels(*src, *dst, args.renderWindow);
}

mDeclarePluginFactory(MetadataCopyExamplePluginFactory, {}, {});

using namespace OFX;
void MetadataCopyExamplePluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
  // basic labels
  desc.setLabels("Metadata Copy", "Metadata Copy", "Metadata Copy");
  desc.setPluginGrouping("OFX Example (Support)");

  // two inputs, so general is the only context this can be described in
  desc.addSupportedContext(eContextGeneral);

  // add supported pixel depths
  desc.addSupportedBitDepth(eBitDepthUByte);
  desc.addSupportedBitDepth(eBitDepthUShort);
  desc.addSupportedBitDepth(eBitDepthFloat);

  // set a few flags
  desc.setSingleInstance(false);
  desc.setHostFrameThreading(false);
  desc.setSupportsMultiResolution(true);
  desc.setSupportsTiles(true);
  desc.setTemporalClipAccess(false);
  desc.setRenderTwiceAlways(false);
  desc.setSupportsMultipleClipPARs(false);
}

void MetadataCopyExamplePluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum /*context*/)
{
  // the clip the image comes from, and the one the host offers metadata from by default
  ClipDescriptor *srcClip = desc.defineClip(kSourceClip);
  srcClip->addSupportedComponent(ePixelComponentRGBA);
  srcClip->addSupportedComponent(ePixelComponentAlpha);
  srcClip->setTemporalClipAccess(false);
  srcClip->setSupportsTiles(true);
  srcClip->setIsMask(false);

  // the second clip is read for its metadata alone, never for its pixels
  ClipDescriptor *maskClip = desc.defineClip(kMaskClip);
  maskClip->addSupportedComponent(ePixelComponentRGBA);
  maskClip->addSupportedComponent(ePixelComponentAlpha);
  maskClip->setTemporalClipAccess(false);
  maskClip->setSupportsTiles(true);
  maskClip->setIsMask(false);
  maskClip->setOptional(true);

  // create the mandated output clip
  ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
  dstClip->addSupportedComponent(ePixelComponentRGBA);
  dstClip->addSupportedComponent(ePixelComponentAlpha);
  dstClip->setSupportsTiles(true);

  PageParamDescriptor *page = desc.definePageParam("Controls");

  ChoiceParamDescriptor *mode = desc.defineChoiceParam(kModeParam);
  mode->setLabels("mode", "mode", "mode");
  mode->setHint("which input's metadata the output carries. The two combined modes keep "
                "every key of both inputs, the named-first input losing any key the other "
                "carries too; the two single-input modes pass that input's metadata "
                "through on its own and discard the other's entirely, so the other's "
                "unique keys are gone rather than merely overridden. The image always "
                "comes from Source whichever is chosen");
  mode->appendOption("Source only");
  mode->appendOption("Mask only");
  mode->appendOption("Source over Mask");
  mode->appendOption("Mask over Source");
  mode->setDefault(eModeSourceOverMask);
  mode->setAnimates(false);
  page->addChild(*mode);

  StringParamDescriptor *sourceFilter = desc.defineStringParam(kSourceFilterParam);
  sourceFilter->setLabels("source filter", "source filter", "source filter");
  sourceFilter->setHint("keep only the keys of Source this pattern matches, ignoring case, "
                        "'*' standing for any run of characters. The whole of the text has "
                        "to match, so 'timecode' matches that key alone and '*time*' matches "
                        "every key holding it. This is one pattern rather than a list: a line "
                        "of several names separated by spaces is one literal string and "
                        "matches nothing. Empty keeps every key");
  sourceFilter->setStringType(eStringTypeSingleLine);
  sourceFilter->setDefault("");
  sourceFilter->setAnimates(false);
  page->addChild(*sourceFilter);

  ChoiceParamDescriptor *sourceFilterMode = desc.defineChoiceParam(kSourceFilterModeParam);
  sourceFilterMode->setLabels("source filter mode", "source filter mode", "source filter mode");
  sourceFilterMode->setHint("what the source filter is matched against: a key's name, its "
                            "values, or either of them. Metadata View labels its own filter "
                            "mode the same way for a different thing: there the mode picks "
                            "what is displayed for a key already matched, here it picks what "
                            "the pattern is matched against");
  sourceFilterMode->appendOption("keys and values");
  sourceFilterMode->appendOption("keys only");
  sourceFilterMode->appendOption("values only");
  sourceFilterMode->setDefault(eFilterModeKeysAndValues);
  sourceFilterMode->setAnimates(false);
  page->addChild(*sourceFilterMode);

  StringParamDescriptor *maskFilter = desc.defineStringParam(kMaskFilterParam);
  maskFilter->setLabels("mask filter", "mask filter", "mask filter");
  maskFilter->setHint("the same pattern for Mask, applied to Mask's keys before the two "
                      "inputs are combined. Each input is filtered on its own, so the two "
                      "filters need not agree. Empty keeps every key");
  maskFilter->setStringType(eStringTypeSingleLine);
  maskFilter->setDefault("");
  maskFilter->setAnimates(false);
  page->addChild(*maskFilter);

  ChoiceParamDescriptor *maskFilterMode = desc.defineChoiceParam(kMaskFilterModeParam);
  maskFilterMode->setLabels("mask filter mode", "mask filter mode", "mask filter mode");
  maskFilterMode->setHint("what the mask filter is matched against: a key's name, its "
                          "values, or either of them, as for the source filter mode above "
                          "and not as Metadata View means the same labels");
  maskFilterMode->appendOption("keys and values");
  maskFilterMode->appendOption("keys only");
  maskFilterMode->appendOption("values only");
  maskFilterMode->setDefault(eFilterModeKeysAndValues);
  maskFilterMode->setAnimates(false);
  page->addChild(*maskFilterMode);
}

OFX::ImageEffect* MetadataCopyExamplePluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
  return new MetadataCopyPlugin(handle);
}

namespace OFX
{
  namespace Plugin
  {
    void getPluginIDs(OFX::PluginFactoryArray &ids)
    {
      static MetadataCopyExamplePluginFactory p("org.openfx.examples.metadataCopy", 1, 0);
      ids.push_back(&p);
    }
  }
}
