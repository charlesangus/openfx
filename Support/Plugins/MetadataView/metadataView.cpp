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

  const char kFilterParam[]     = "filter";
  const char kFilterModeParam[] = "filterMode";
  const char kDisplayParam[]    = "display";

  enum FilterModeEnum {
    eFilterModeKeysAndValues,
    eFilterModeKeysOnly,
    eFilterModeValuesOnly
  };

  char lowerCase(char c)
  {
    return char(tolower((unsigned char) c));
  }

  /** @brief does text hold needle, ignoring case? An empty needle is held by anything */
  bool containsNoCase(const std::string &text, const std::string &needle)
  {
    if(needle.size() > text.size())
      return false;

    for(size_t at = 0; at + needle.size() <= text.size(); at++) {
      size_t i = 0;

      while(i < needle.size() && lowerCase(text[at + i]) == lowerCase(needle[i]))
        i++;

      if(i == needle.size())
        return true;
    }

    return false;
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
/** @brief shows the metadata of its source clip whose keys hold a filter string, and
passes the image through untouched */
class MetadataViewPlugin : public OFX::ImageEffect {
protected :
  // do not need to delete these, the ImageEffect is managing them for us
  OFX::Clip *dstClip_;
  OFX::Clip *srcClip_;

  OFX::StringParam *filter_;
  OFX::ChoiceParam *filterMode_;
  OFX::StringParam *display_;

public :
  /** @brief ctor */
  MetadataViewPlugin(OfxImageEffectHandle handle)
    : ImageEffect(handle)
    , dstClip_(0)
    , srcClip_(0)
    , filter_(0)
    , filterMode_(0)
    , display_(0)
  {
    dstClip_ = fetchClip(kOfxImageEffectOutputClipName);
    srcClip_ = fetchClip(kOfxImageEffectSimpleSourceClipName);

    filter_ = fetchStringParam(kFilterParam);
    filterMode_ = fetchChoiceParam(kFilterModeParam);
    display_ = fetchStringParam(kDisplayParam);
  }

  /* Override the render */
  virtual void render(const OFX::RenderArguments &args);

  /* Override changedParam */
  virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName);

  /* the matching metadata of the source clip at the given time, one entry per line */
  std::string displayText(double time);
};

std::string
MetadataViewPlugin::displayText(double time)
{
  if(!OFX::getImageEffectHostDescription()->supportsMetadata)
    return std::string();

  const OFX::MetadataSet metadata = srcClip_->getMetadata(time);
  const std::vector<OFX::MetadataEntry> entries = metadata.entries();

  std::string filter;
  int mode = eFilterModeKeysAndValues;

  filter_->getValue(filter);
  filterMode_->getValue(mode);

  std::string text;

  for(size_t i = 0; i < entries.size(); i++) {
    if(!containsNoCase(entries[i].key, filter))
      continue;

    if(!text.empty())
      text += "\n";

    switch(FilterModeEnum(mode)) {
    case eFilterModeKeysOnly   : text += entries[i].key; break;
    case eFilterModeValuesOnly : text += valueText(metadata, entries[i]); break;
    default                    : text += entries[i].key + "=" + valueText(metadata, entries[i]); break;
    }
  }

  return text;
}

// a render must not write a parameter, so the display is composed here instead, at
// whatever time the host reports the change at
void
MetadataViewPlugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName)
{
  if(paramName == kFilterParam || paramName == kFilterModeParam)
    display_->setValue(displayText(args.time));
}

// the overridden render function
void
MetadataViewPlugin::render(const OFX::RenderArguments &args)
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

mDeclarePluginFactory(MetadataViewExamplePluginFactory, {}, {});

using namespace OFX;
void MetadataViewExamplePluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
  // basic labels
  desc.setLabels("Metadata View", "Metadata View", "Metadata View");
  desc.setPluginGrouping("OFX Example (Support)");

  // add the supported contexts, only filter at the moment
  desc.addSupportedContext(eContextFilter);

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

void MetadataViewExamplePluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum /*context*/)
{
  // Source clip only in the filter context
  // create the mandated source clip
  ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
  srcClip->addSupportedComponent(ePixelComponentRGBA);
  srcClip->addSupportedComponent(ePixelComponentAlpha);
  srcClip->setTemporalClipAccess(false);
  srcClip->setSupportsTiles(true);
  srcClip->setIsMask(false);

  // create the mandated output clip
  ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
  dstClip->addSupportedComponent(ePixelComponentRGBA);
  dstClip->addSupportedComponent(ePixelComponentAlpha);
  dstClip->setSupportsTiles(true);

  PageParamDescriptor *page = desc.definePageParam("Controls");

  StringParamDescriptor *filter = desc.defineStringParam(kFilterParam);
  filter->setLabels("filter", "filter", "filter");
  filter->setHint("show only the keys holding this text, ignoring case; empty shows every key");
  filter->setStringType(eStringTypeSingleLine);
  filter->setDefault("");
  filter->setAnimates(false);
  page->addChild(*filter);

  ChoiceParamDescriptor *mode = desc.defineChoiceParam(kFilterModeParam);
  mode->setLabels("filter mode", "filter mode", "filter mode");
  mode->setHint("what to show for each key the filter matches");
  mode->appendOption("keys and values");
  mode->appendOption("keys only");
  mode->appendOption("values only");
  mode->setDefault(eFilterModeKeysAndValues);
  mode->setAnimates(false);
  page->addChild(*mode);

  StringParamDescriptor *display = desc.defineStringParam(kDisplayParam);
  display->setLabels("metadata", "metadata", "metadata");
  display->setStringType(eStringTypeMultiLine);
  display->setDefault("");
  display->setAnimates(false);
  display->setEnabled(false);
  display->setIsPersistant(false);
  display->setEvaluateOnChange(false);
  page->addChild(*display);
}

OFX::ImageEffect* MetadataViewExamplePluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
  return new MetadataViewPlugin(handle);
}

namespace OFX
{
  namespace Plugin
  {
    void getPluginIDs(OFX::PluginFactoryArray &ids)
    {
      static MetadataViewExamplePluginFactory p("org.openfx.examples.metadataView", 1, 0);
      ids.push_back(&p);
    }
  }
}
