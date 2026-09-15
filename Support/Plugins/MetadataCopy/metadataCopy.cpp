// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

#include <algorithm>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "ofxsImageEffect.h"
#include "ofxsMetadata.h"

#include "../include/ofxsPixelCopy.H"

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
      else if(p < pattern.size() && OFX::lowerCase(pattern[p]) == OFX::lowerCase(text[t])) {
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
  virtual void getMetadata(const OFX::MetadataArguments &args, OFX::MetadataSetter &metadata, OFX::MetadataInheritanceSetter &inheritance);

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

  // The host pre-fills a retained-keys list only for the first connected input clip in
  // described order and leaves the others empty, so the candidates have to come from
  // the clip's own metadata: filtering what getRetainedKeys reports could contribute
  // nothing at all from the other clip.
  const OFX::MetadataSet metadata = clip.getMetadata(time);
  const std::vector<OFX::MetadataEntry> entries = metadata.entries();

  std::vector<std::string> kept;

  for(size_t i = 0; i < entries.size(); i++) {
    if(matchesFilter(metadata, entries[i], pattern, FilterModeEnum(filterMode)))
      kept.push_back(entries[i].key);
  }

  inheritance.setRetainedKeys(clip, kept);
}

// guide: begin getMetadata
void
MetadataCopyPlugin::getMetadata(const OFX::MetadataArguments &args, OFX::MetadataSetter &/*metadata*/, OFX::MetadataInheritanceSetter &inheritance)
{
  if(!OFX::getImageEffectHostDescription()->supportsMetadata)
    return;

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

  // an unconnected Mask carries nothing, and reading it at all is an error
  if(!maskClip_->isConnected())
    clips.erase(std::remove(clips.begin(), clips.end(), std::string(kMaskClip)), clips.end());

  inheritance.setSourceClips(clips);

  for(size_t i = 0; i < clips.size(); i++) {
    if(clips[i] == kSourceClip)
      retainMatchingKeys(args.time, *srcClip_, *sourceFilter_, *sourceFilterMode_, inheritance);
    else
      retainMatchingKeys(args.time, *maskClip_, *maskFilter_, *maskFilterMode_, inheritance);
  }
}
// guide: end getMetadata

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
  mode->setHint("which input's metadata is kept");
  mode->appendOption("Source only");
  mode->appendOption("Mask only");
  mode->appendOption("Source over Mask");
  mode->appendOption("Mask over Source");
  mode->setDefault(eModeSourceOverMask);
  mode->setAnimates(false);
  page->addChild(*mode);

  StringParamDescriptor *sourceFilter = desc.defineStringParam(kSourceFilterParam);
  sourceFilter->setLabels("source filter", "source filter", "source filter");
  sourceFilter->setHint("a glob filter for Source keys");
  sourceFilter->setStringType(eStringTypeSingleLine);
  sourceFilter->setDefault("");
  sourceFilter->setAnimates(false);
  page->addChild(*sourceFilter);

  ChoiceParamDescriptor *sourceFilterMode = desc.defineChoiceParam(kSourceFilterModeParam);
  sourceFilterMode->setLabels("source filter mode", "source filter mode", "source filter mode");
  sourceFilterMode->setHint("what the filter matches");
  sourceFilterMode->appendOption("keys and values");
  sourceFilterMode->appendOption("keys only");
  sourceFilterMode->appendOption("values only");
  sourceFilterMode->setDefault(eFilterModeKeysAndValues);
  sourceFilterMode->setAnimates(false);
  page->addChild(*sourceFilterMode);

  StringParamDescriptor *maskFilter = desc.defineStringParam(kMaskFilterParam);
  maskFilter->setLabels("mask filter", "mask filter", "mask filter");
  maskFilter->setHint("a glob filter for Mask keys");
  maskFilter->setStringType(eStringTypeSingleLine);
  maskFilter->setDefault("");
  maskFilter->setAnimates(false);
  page->addChild(*maskFilter);

  ChoiceParamDescriptor *maskFilterMode = desc.defineChoiceParam(kMaskFilterModeParam);
  maskFilterMode->setLabels("mask filter mode", "mask filter mode", "mask filter mode");
  maskFilterMode->setHint("what the filter matches");
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
      static MetadataCopyExamplePluginFactory p("net.sf.openfx.metadataCopy", 1, 0);
      ids.push_back(&p);
    }
  }
}
