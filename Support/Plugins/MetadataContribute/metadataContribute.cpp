// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "ofxsImageEffect.h"
#include "ofxsMetadata.h"

namespace {

  const char kNoteParam[]    = "note";
  const char kModeParam[]    = "mode";
  const char kDropKeyParam[] = "dropKey";

  // the reverse DNS prefix every key this plugin contributes is namespaced under
  const char kKeyPrefix[] = "org.openfx.examples.metadataContribute.";

  enum ModeEnum {
    eModeInheritAll,
    eModeDropOneKey,
    eModeInheritNothing
  };

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
/** @brief contributes a fixed set of metadata keys to its output at every call, controls
what it inherits from its source clip according to a mode param, and passes the image
through untouched */
class MetadataContributePlugin : public OFX::ImageEffect {
protected :
  // do not need to delete these, the ImageEffect is managing them for us
  OFX::Clip *dstClip_;
  OFX::Clip *srcClip_;

  OFX::StringParam *note_;
  OFX::ChoiceParam *mode_;
  OFX::StringParam *dropKey_;

public :
  /** @brief ctor */
  MetadataContributePlugin(OfxImageEffectHandle handle)
    : ImageEffect(handle)
    , dstClip_(0)
    , srcClip_(0)
    , note_(0)
    , mode_(0)
    , dropKey_(0)
  {
    dstClip_ = fetchClip(kOfxImageEffectOutputClipName);
    srcClip_ = fetchClip(kOfxImageEffectSimpleSourceClipName);

    note_    = fetchStringParam(kNoteParam);
    mode_    = fetchChoiceParam(kModeParam);
    dropKey_ = fetchStringParam(kDropKeyParam);
  }

  /* Override the render */
  virtual void render(const OFX::RenderArguments &args);

  /* Override getMetadata */
  virtual bool getMetadata(const OFX::MetadataArguments &args, OFX::MetadataSetBuilder &metadata, OFX::MetadataInheritanceSetter &inheritance);
};

bool
MetadataContributePlugin::getMetadata(const OFX::MetadataArguments &/*args*/, OFX::MetadataSetBuilder &metadata, OFX::MetadataInheritanceSetter &inheritance)
{
  if(!OFX::getImageEffectHostDescription()->supportsMetadata)
    return false;

  std::string note;
  note_->getValue(note);

  // one key through each of the six suite entry points MetadataSetBuilder exposes,
  // plus a framerate that disagrees with the fixture's Source so the two are
  // distinguishable downstream
  metadata.setString(std::string(kKeyPrefix) + "note", note);
  metadata.setInt(std::string(kKeyPrefix) + "revision", 1);
  metadata.setDouble(std::string(kKeyPrefix) + "quality", 0.75);
  metadata.setStringN(std::string(kKeyPrefix) + "tags", std::vector<std::string>({"reviewed", "approved"}));
  metadata.setIntN(std::string(kKeyPrefix) + "renderRegion", std::vector<int>({0, 0, 1280, 720}));
  metadata.setDoubleN(std::string(kKeyPrefix) + "weights", std::vector<double>({1.0, 0.5, 0.25}));
  metadata.setDouble(kOfxMetadataKeyFrameRate, 30.0);

  int mode = eModeInheritAll;
  mode_->getValue(mode);

  switch(ModeEnum(mode)) {
  case eModeDropOneKey : {
    std::string dropKey;
    dropKey_->getValue(dropKey);

    const std::vector<std::string> retained = inheritance.getRetainedKeys(*srcClip_);
    std::vector<std::string> kept;

    for(size_t i = 0; i < retained.size(); i++) {
      if(retained[i] != dropKey)
        kept.push_back(retained[i]);
    }

    inheritance.setRetainedKeys(*srcClip_, kept);
    break;
  }
  case eModeInheritNothing :
    inheritance.setSourceClips(std::vector<std::string>());
    break;
  default :
    // inherit all: leave outArgs untouched
    break;
  }

  return true;
}

// the overridden render function
void
MetadataContributePlugin::render(const OFX::RenderArguments &args)
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

mDeclarePluginFactory(MetadataContributeExamplePluginFactory, {}, {});

using namespace OFX;
void MetadataContributeExamplePluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
  // basic labels
  desc.setLabels("Metadata Contribute", "Metadata Contribute", "Metadata Contribute");
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

void MetadataContributeExamplePluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum /*context*/)
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

  StringParamDescriptor *note = desc.defineStringParam(kNoteParam);
  note->setLabels("note", "note", "note");
  note->setHint("text contributed as org.openfx.examples.metadataContribute.note at every call");
  note->setStringType(eStringTypeSingleLine);
  note->setDefault("");
  note->setAnimates(false);
  page->addChild(*note);

  ChoiceParamDescriptor *mode = desc.defineChoiceParam(kModeParam);
  mode->setLabels("mode", "mode", "mode");
  mode->setHint("what this effect does to the metadata it inherits from Source, on top of what it always contributes");
  mode->appendOption("inherit all");
  mode->appendOption("drop one key");
  mode->appendOption("inherit nothing");
  mode->setDefault(eModeInheritAll);
  mode->setAnimates(false);
  page->addChild(*mode);

  StringParamDescriptor *dropKey = desc.defineStringParam(kDropKeyParam);
  dropKey->setLabels("drop key", "drop key", "drop key");
  dropKey->setHint("the retained key to drop from Source's inherited metadata when mode is 'drop one key'");
  dropKey->setStringType(eStringTypeSingleLine);
  dropKey->setDefault(kOfxMetadataKeySampleType);
  dropKey->setAnimates(false);
  page->addChild(*dropKey);
}

OFX::ImageEffect* MetadataContributeExamplePluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
  return new MetadataContributePlugin(handle);
}

namespace OFX
{
  namespace Plugin
  {
    void getPluginIDs(OFX::PluginFactoryArray &ids)
    {
      static MetadataContributeExamplePluginFactory p("org.openfx.examples.metadataContribute", 1, 0);
      ids.push_back(&p);
    }
  }
}
