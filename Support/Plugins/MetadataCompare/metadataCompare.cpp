// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "ofxsImageEffect.h"
#include "ofxsMetadata.h"

namespace {

  const char kDisplayParam[] = "display";

  const char kSourceClip[] = kOfxImageEffectSimpleSourceClipName;
  const char kMaskClip[]   = "Mask";

  const char kSourceOnlyPrefix[] = "Source only: ";
  const char kMaskOnlyPrefix[]   = "Mask only: ";
  const char kDiffersPrefix[]    = "differs: ";

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

  void appendLine(std::string &text, const std::string &line)
  {
    if(!text.empty())
      text += "\n";

    text += line;
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
/** @brief shows the differences between Source's and Mask's metadata in a read-only
parameter, and passes Source's image through untouched */
class MetadataComparePlugin : public OFX::ImageEffect {
protected :
  // do not need to delete these, the ImageEffect is managing them for us
  OFX::Clip *dstClip_;
  OFX::Clip *srcClip_;
  OFX::Clip *maskClip_;

  OFX::StringParam *display_;

public :
  /** @brief ctor */
  MetadataComparePlugin(OfxImageEffectHandle handle)
    : ImageEffect(handle)
    , dstClip_(0)
    , srcClip_(0)
    , maskClip_(0)
    , display_(0)
  {
    dstClip_  = fetchClip(kOfxImageEffectOutputClipName);
    srcClip_  = fetchClip(kSourceClip);
    maskClip_ = fetchClip(kMaskClip);

    display_ = fetchStringParam(kDisplayParam);
  }

  /* Override the render */
  virtual void render(const OFX::RenderArguments &args);

  /* Override changedParam */
  virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName);

  /* one line per key that differs between Source and Mask at the given time, in
  ascending key order */
  std::string displayText(double time);
};

std::string
MetadataComparePlugin::displayText(double time)
{
  if(!OFX::getImageEffectHostDescription()->supportsMetadata)
    return std::string();

  const OFX::MetadataSet source = srcClip_->getMetadata(time);
  const OFX::MetadataSet mask = maskClip_->getMetadata(time);

  const std::vector<OFX::MetadataEntry> sourceEntries = source.entries();
  const std::vector<OFX::MetadataEntry> maskEntries = mask.entries();

  std::string text;
  size_t si = 0, mi = 0;

  // both entries() lists are already in ascending key order, so this is a merge rather
  // than a re-sort
  while(si < sourceEntries.size() || mi < maskEntries.size()) {
    if(mi == maskEntries.size()
       || (si < sourceEntries.size() && sourceEntries[si].key < maskEntries[mi].key)) {
      appendLine(text, std::string(kSourceOnlyPrefix) + sourceEntries[si].key + "="
                        + valueText(source, sourceEntries[si]));
      si++;
    }
    else if(si == sourceEntries.size() || maskEntries[mi].key < sourceEntries[si].key) {
      appendLine(text, std::string(kMaskOnlyPrefix) + maskEntries[mi].key + "="
                        + valueText(mask, maskEntries[mi]));
      mi++;
    }
    else {
      const std::string sourceValue = valueText(source, sourceEntries[si]);
      const std::string maskValue = valueText(mask, maskEntries[mi]);

      if(sourceValue != maskValue) {
        appendLine(text, std::string(kDiffersPrefix) + sourceEntries[si].key
                          + ": Source=" + sourceValue + " Mask=" + maskValue);
      }

      si++;
      mi++;
    }
  }

  return text;
}

// a render must not write a parameter, so the display is composed here instead, at
// whatever time the host reports the change at. The host re-enters this action with
// eChangePluginEdit for the plugin's own setValue below, so that reason is the echo of
// this call rather than a fresh edit and is ignored
void
MetadataComparePlugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &/*paramName*/)
{
  if(args.reason == OFX::eChangePluginEdit)
    return;

  display_->setValue(displayText(args.time));
}

// the overridden render function
void
MetadataComparePlugin::render(const OFX::RenderArguments &args)
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

mDeclarePluginFactory(MetadataCompareExamplePluginFactory, {}, {});

using namespace OFX;
void MetadataCompareExamplePluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
  // basic labels
  desc.setLabels("Metadata Compare", "Metadata Compare", "Metadata Compare");
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

void MetadataCompareExamplePluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum /*context*/)
{
  // the clip the image comes from
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

  StringParamDescriptor *display = desc.defineStringParam(kDisplayParam);
  display->setLabels("differences", "differences", "differences");
  display->setHint("one line per key that Source and Mask disagree on: a key held by "
                   "only one of them, or held by both with different values. A key both "
                   "hold with the same value is not shown");
  display->setStringType(eStringTypeMultiLine);
  display->setDefault("");
  display->setAnimates(false);
  display->setEnabled(false);
  display->setIsPersistant(false);
  display->setEvaluateOnChange(false);
  page->addChild(*display);
}

OFX::ImageEffect* MetadataCompareExamplePluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
  return new MetadataComparePlugin(handle);
}

namespace OFX
{
  namespace Plugin
  {
    void getPluginIDs(OFX::PluginFactoryArray &ids)
    {
      static MetadataCompareExamplePluginFactory p("org.openfx.examples.metadataCompare", 1, 0);
      ids.push_back(&p);
    }
  }
}
