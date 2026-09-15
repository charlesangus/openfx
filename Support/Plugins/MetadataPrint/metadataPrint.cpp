// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "ofxsImageEffect.h"
#include "ofxsMetadata.h"

#include "../include/ofxsPixelCopy.H"

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
      static MetadataPrintExamplePluginFactory p("net.sf.openfx.metadataPrint", 1, 0);
      ids.push_back(&p);
    }
  }
}
