// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

#include <cmath>
#include <cstring>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>

#include "ofxsImageEffect.h"
#include "ofxsMetadata.h"

namespace {

  const char kStartTimecodeParam[]    = "startTimecode";
  const char kRateParam[]             = "rate";
  const char kRateFromMetadataParam[] = "rateFromMetadata";
  const char kStartFrameParam[]       = "startFrame";
  const char kUseStartFrameParam[]    = "useStartFrame";

  /** @brief the frame the start timecode is read at when useStartFrame is off */
  const int kFixedOrigin = 1;

  /** @brief the rate the frames field counts at, which is the whole number of frames a
  second holds */
  int roundedRate(double rate)
  {
    const int rounded = int(rate + 0.5);

    return rounded < 1 ? 1 : rounded;
  }

  /** @brief the four fields of HH:MM:SS:FF, any non digit separating them, all zero if
  there are fewer than four numbers to read */
  void parseTimecode(const std::string &text, int fields[4])
  {
    int read = 0;
    size_t pos = 0;

    fields[0] = fields[1] = fields[2] = fields[3] = 0;

    while(pos < text.size() && read < 4) {
      if(text[pos] < '0' || text[pos] > '9') {
        pos++;
        continue;
      }

      int value = 0;

      while(pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
        value = value * 10 + (text[pos] - '0');
        pos++;
      }

      fields[read++] = value;
    }

    if(read < 4)
      fields[0] = fields[1] = fields[2] = fields[3] = 0;
  }

  long long timecodeToFrames(const std::string &text, int rate)
  {
    int fields[4];

    parseTimecode(text, fields);

    return (((long long) fields[0] * 60 + fields[1]) * 60 + fields[2]) * rate + fields[3];
  }

  /** @brief the non drop frame HH:MM:SS:FF a whole number of frames stands for, wrapped
  into the twenty four hours a timecode can express */
  std::string framesToTimecode(long long frames, int rate)
  {
    const long long day = 24LL * 60 * 60 * rate;

    frames %= day;

    if(frames < 0)
      frames += day;

    const long long seconds = frames / rate;

    std::ostringstream os;

    os << std::setfill('0')
       << std::setw(2) << (seconds / 3600) << ":"
       << std::setw(2) << ((seconds / 60) % 60) << ":"
       << std::setw(2) << (seconds % 60) << ":"
       << std::setw(2) << (frames % rate);

    return os.str();
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
/** @brief counts a non drop frame timecode on from a start code, one frame at a time, so
that its output carries a different value at every frame, contributes the rate it counted
at alongside it, and passes the image through untouched */
class MetadataTimeCodePlugin : public OFX::ImageEffect {
protected :
  // do not need to delete these, the ImageEffect is managing them for us
  OFX::Clip *dstClip_;
  OFX::Clip *srcClip_;

  OFX::StringParam  *startTimecode_;
  OFX::DoubleParam  *rate_;
  OFX::BooleanParam *rateFromMetadata_;
  OFX::IntParam     *startFrame_;
  OFX::BooleanParam *useStartFrame_;

public :
  /** @brief ctor */
  MetadataTimeCodePlugin(OfxImageEffectHandle handle)
    : ImageEffect(handle)
    , dstClip_(0)
    , srcClip_(0)
    , startTimecode_(0)
    , rate_(0)
    , rateFromMetadata_(0)
    , startFrame_(0)
    , useStartFrame_(0)
  {
    dstClip_ = fetchClip(kOfxImageEffectOutputClipName);
    srcClip_ = fetchClip(kOfxImageEffectSimpleSourceClipName);

    startTimecode_    = fetchStringParam(kStartTimecodeParam);
    rate_             = fetchDoubleParam(kRateParam);
    rateFromMetadata_ = fetchBooleanParam(kRateFromMetadataParam);
    startFrame_       = fetchIntParam(kStartFrameParam);
    useStartFrame_    = fetchBooleanParam(kUseStartFrameParam);
  }

  /* Override the render */
  virtual void render(const OFX::RenderArguments &args);

  /* Override getMetadata */
  virtual bool getMetadata(const OFX::MetadataArguments &args, OFX::MetadataSetBuilder &metadata, OFX::MetadataInheritanceSetter &inheritance);
};

bool
MetadataTimeCodePlugin::getMetadata(const OFX::MetadataArguments &args, OFX::MetadataSetBuilder &metadata, OFX::MetadataInheritanceSetter &/*inheritance*/)
{
  if(!OFX::getImageEffectHostDescription()->supportsMetadata)
    return false;

  double rate = 0;
  rate_->getValue(rate);

  bool rateFromMetadata = false;
  rateFromMetadata_->getValue(rateFromMetadata);

  if(rateFromMetadata) {
    const OFX::MetadataSet source = srcClip_->getMetadata(args.time);

    rate = source.getDouble(kOfxMetadataKeyFrameRate, 0, rate);
  }

  // the start code is read at the same rate the frames field counts at, so a rate taken
  // off the source moves the timecode itself rather than only the rate reported with it
  const int counted = roundedRate(rate);

  bool useStartFrame = false;
  useStartFrame_->getValue(useStartFrame);

  int startFrame = kFixedOrigin;
  startFrame_->getValue(startFrame);

  // the start code lands on absolute frame 1 unless useStartFrame moves it, so a clip
  // or a project which begins elsewhere is still counted from frame 1
  const double origin = useStartFrame ? double(startFrame) : double(kFixedOrigin);

  std::string startTimecode;
  startTimecode_->getValue(startTimecode);

  const long long offset = (long long) std::floor(args.time - origin + 0.5);

  metadata.setString(kOfxMetadataKeyTimecode,
                     framesToTimecode(timecodeToFrames(startTimecode, counted) + offset, counted));
  metadata.setDouble(kOfxMetadataKeyFrameRate, rate);

  return true;
}

// the overridden render function
void
MetadataTimeCodePlugin::render(const OFX::RenderArguments &args)
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

mDeclarePluginFactory(MetadataTimeCodeExamplePluginFactory, {}, {});

using namespace OFX;
void MetadataTimeCodeExamplePluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
  // basic labels
  desc.setLabels("Metadata Time Code", "Metadata Time Code", "Metadata Time Code");
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

void MetadataTimeCodeExamplePluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum /*context*/)
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

  StringParamDescriptor *startTimecode = desc.defineStringParam(kStartTimecodeParam);
  startTimecode->setLabels("start timecode", "start timecode", "start timecode");
  startTimecode->setHint("the non drop frame HH:MM:SS:FF the count starts from, carried at the "
                         "origin frame and counted on by one frame for each frame after it, "
                         "wrapping back to 00:00:00:00 after 23:59:59:FF");
  startTimecode->setStringType(eStringTypeSingleLine);
  startTimecode->setDefault("01:00:00:00");
  startTimecode->setAnimates(false);
  page->addChild(*startTimecode);

  DoubleParamDescriptor *rate = desc.defineDoubleParam(kRateParam);
  rate->setLabels("rate", "rate", "rate");
  rate->setHint("the frames per second the count runs at, used when the rate is not being "
                "taken from the source's metadata. The frames field counts to the nearest "
                "whole number of it");
  rate->setDefault(24);
  rate->setRange(1, 1000);
  rate->setDisplayRange(1, 120);
  rate->setAnimates(false);
  page->addChild(*rate);

  BooleanParamDescriptor *rateFromMetadata = desc.defineBooleanParam(kRateFromMetadataParam);
  rateFromMetadata->setLabels("rate from metadata", "rate from metadata", "rate from metadata");
  rateFromMetadata->setHint("read the frame rate off Source's metadata rather than from the rate "
                            "parameter, falling back to the parameter where Source carries none. "
                            "The rate read is what the frames field counts to and what the start "
                            "timecode is read at, so it moves the timecode as well as the rate "
                            "reported with it");
  rateFromMetadata->setDefault(true);
  rateFromMetadata->setAnimates(false);
  page->addChild(*rateFromMetadata);

  IntParamDescriptor *startFrame = desc.defineIntParam(kStartFrameParam);
  startFrame->setLabels("start frame", "start frame", "start frame");
  startFrame->setHint("the frame the start timecode lands on, used only when 'use start frame' "
                      "is on. Frames before it count backwards from the start timecode");
  startFrame->setDefault(1);
  startFrame->setAnimates(false);
  page->addChild(*startFrame);

  BooleanParamDescriptor *useStartFrame = desc.defineBooleanParam(kUseStartFrameParam);
  useStartFrame->setLabels("use start frame", "use start frame", "use start frame");
  useStartFrame->setHint("count from the start frame rather than from frame 1. With this off the "
                         "start timecode always lands on frame 1 and the start frame parameter is "
                         "ignored, whatever frame the clip or the project begins at");
  useStartFrame->setDefault(false);
  useStartFrame->setAnimates(false);
  page->addChild(*useStartFrame);
}

OFX::ImageEffect* MetadataTimeCodeExamplePluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
  return new MetadataTimeCodePlugin(handle);
}

namespace OFX
{
  namespace Plugin
  {
    void getPluginIDs(OFX::PluginFactoryArray &ids)
    {
      static MetadataTimeCodeExamplePluginFactory p("org.openfx.examples.metadataTimeCode", 1, 0);
      ids.push_back(&p);
    }
  }
}
