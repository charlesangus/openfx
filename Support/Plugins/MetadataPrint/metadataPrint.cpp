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

  const char *typeName(OFX::MetadataTypeEnum type)
  {
    switch(type) {
    case OFX::eMetadataTypeInt    : return "int";
    case OFX::eMetadataTypeDouble : return "double";
    case OFX::eMetadataTypeString : return "string";
    case OFX::eMetadataTypeNone   : break;
    }
    return "none";
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
/** @brief writes the metadata of its source clip to the host's log and passes the
image through untouched */
class MetadataPrintPlugin : public OFX::ImageEffect {
protected :
  // do not need to delete these, the ImageEffect is managing them for us
  OFX::Clip *dstClip_;
  OFX::Clip *srcClip_;

public :
  /** @brief ctor */
  MetadataPrintPlugin(OfxImageEffectHandle handle)
    : ImageEffect(handle)
    , dstClip_(0)
    , srcClip_(0)
  {
    dstClip_ = fetchClip(kOfxImageEffectOutputClipName);
    srcClip_ = fetchClip(kOfxImageEffectSimpleSourceClipName);
  }

  /* Override the render */
  virtual void render(const OFX::RenderArguments &args);

  /* log what the source clip carries at the given time */
  void logMetadata(double time);
};

void
MetadataPrintPlugin::logMetadata(double time)
{
  if(!OFX::getImageEffectHostDescription()->supportsMetadata)
    return;

  const OFX::MetadataSet metadata = srcClip_->getMetadata(time);
  const std::vector<OFX::MetadataEntry> entries = metadata.entries();

  // one message per key rather than one per clip: a host's log takes a line at a time,
  // and a whole table of them would be cut off at whatever length it buffers
  for(size_t i = 0; i < entries.size(); i++) {
    std::ostringstream line;

    line << "clip=" << srcClip_->name()
         << " frame=" << time
         << " key=" << entries[i].key
         << " type=" << typeName(entries[i].type)
         << " value=" << valueText(metadata, entries[i]);

    sendMessage(OFX::Message::eMessageLog, "", line.str());
  }
}

// the overridden render function
void
MetadataPrintPlugin::render(const OFX::RenderArguments &args)
{
  logMetadata(args.time);

  std::unique_ptr<OFX::Image> dst(dstClip_->fetchImage(args.time));
  std::unique_ptr<OFX::Image> src(srcClip_->fetchImage(args.time));

  if(!dst.get() || !src.get())
    return;

  if(src->getPixelDepth() != dst->getPixelDepth()
     || src->getPixelComponents() != dst->getPixelComponents())
    OFX::throwSuiteStatusException(kOfxStatErrImageFormat);

  copyPixels(*src, *dst, args.renderWindow);
}

mDeclarePluginFactory(MetadataPrintExamplePluginFactory, {}, {});

using namespace OFX;
void MetadataPrintExamplePluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
  // basic labels
  desc.setLabels("Metadata Print", "Metadata Print", "Metadata Print");
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

void MetadataPrintExamplePluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum /*context*/)
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
}

OFX::ImageEffect* MetadataPrintExamplePluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
  return new MetadataPrintPlugin(handle);
}

namespace OFX
{
  namespace Plugin
  {
    void getPluginIDs(OFX::PluginFactoryArray &ids)
    {
      static MetadataPrintExamplePluginFactory p("org.openfx.examples.metadataPrint", 1, 0);
      ids.push_back(&p);
    }
  }
}
