// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "ofxsImageEffect.h"
#include "ofxsMetadata.h"

namespace {

  const char kOperationsParam[] = "operations";

  const char kSetVerb[]    = "set";
  const char kRemoveVerb[] = "remove";

  /** @brief what a whole operations list resolves to: the keys to contribute, and the
  keys to drop from what is inherited */
  struct Edits {
    std::map<std::string, std::string> set;
    std::set<std::string>              removed;
  };

  bool isSpace(char c)
  {
    return c == ' ' || c == '\t' || c == '\r';
  }

  /** @brief the next whitespace delimited token of line from pos, leaving pos just past it */
  std::string nextToken(const std::string &line, size_t &pos)
  {
    while(pos < line.size() && isSpace(line[pos]))
      pos++;

    const size_t begin = pos;

    while(pos < line.size() && !isSpace(line[pos]))
      pos++;

    return line.substr(begin, pos - begin);
  }

  /** @brief the rest of line from pos, with the whitespace at either end taken off */
  std::string restOfLine(const std::string &line, size_t pos)
  {
    while(pos < line.size() && isSpace(line[pos]))
      pos++;

    size_t end = line.size();

    while(end > pos && isSpace(line[end - 1]))
      end--;

    return line.substr(pos, end - pos);
  }

  /** @brief work the whole list out before either setter is touched

  A host lays what an effect contributes over what it inherits, so a remove which follows
  a set of the same key cannot be expressed by the retained keys list at all: it has to
  take that key back out of the contributions instead. Which means neither setter can be
  called until every line has been read.
  */
  void resolveEdits(const std::string &operations, Edits &edits)
  {
    std::istringstream lines(operations);
    std::string line;

    while(std::getline(lines, line)) {
      size_t pos = 0;

      const std::string verb = nextToken(line, pos);
      const std::string key  = nextToken(line, pos);

      if(key.empty())
        continue;

      if(verb == kSetVerb) {
        edits.set[key] = restOfLine(line, pos);
        edits.removed.erase(key);
      }
      else if(verb == kRemoveVerb) {
        edits.set.erase(key);
        edits.removed.insert(key);
      }
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
/** @brief applies an ordered list of edits to the metadata it inherits from its source
clip, each line of the list either setting a key to a literal value or removing one, and
passes the image through untouched */
class MetadataModifyPlugin : public OFX::ImageEffect {
protected :
  // do not need to delete these, the ImageEffect is managing them for us
  OFX::Clip *dstClip_;
  OFX::Clip *srcClip_;

  OFX::StringParam *operations_;

public :
  /** @brief ctor */
  MetadataModifyPlugin(OfxImageEffectHandle handle)
    : ImageEffect(handle)
    , dstClip_(0)
    , srcClip_(0)
    , operations_(0)
  {
    dstClip_ = fetchClip(kOfxImageEffectOutputClipName);
    srcClip_ = fetchClip(kOfxImageEffectSimpleSourceClipName);

    operations_ = fetchStringParam(kOperationsParam);
  }

  /* Override the render */
  virtual void render(const OFX::RenderArguments &args);

  /* Override getMetadata */
  virtual bool getMetadata(const OFX::MetadataArguments &args, OFX::MetadataSetBuilder &metadata, OFX::MetadataInheritanceSetter &inheritance);
};

bool
MetadataModifyPlugin::getMetadata(const OFX::MetadataArguments &/*args*/, OFX::MetadataSetBuilder &metadata, OFX::MetadataInheritanceSetter &inheritance)
{
  if(!OFX::getImageEffectHostDescription()->supportsMetadata)
    return false;

  std::string operations;
  operations_->getValue(operations);

  Edits edits;
  resolveEdits(operations, edits);

  for(std::map<std::string, std::string>::const_iterator it = edits.set.begin(); it != edits.set.end(); ++it)
    metadata.setString(it->first, it->second);

  if(!edits.removed.empty()) {
    const std::vector<std::string> retained = inheritance.getRetainedKeys(*srcClip_);
    std::vector<std::string> kept;

    // a key the list removes which was never inherited simply is not in here, so
    // removing one is a no-op rather than an error
    for(size_t i = 0; i < retained.size(); i++) {
      if(edits.removed.find(retained[i]) == edits.removed.end())
        kept.push_back(retained[i]);
    }

    inheritance.setRetainedKeys(*srcClip_, kept);
  }

  return true;
}

// the overridden render function
void
MetadataModifyPlugin::render(const OFX::RenderArguments &args)
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

mDeclarePluginFactory(MetadataModifyExamplePluginFactory, {}, {});

using namespace OFX;
void MetadataModifyExamplePluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
  // basic labels
  desc.setLabels("Metadata Modify", "Metadata Modify", "Metadata Modify");
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

void MetadataModifyExamplePluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum /*context*/)
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

  StringParamDescriptor *operations = desc.defineStringParam(kOperationsParam);
  operations->setLabels("operations", "operations", "operations");
  operations->setHint("one operation per line, either 'set <key> <value>' or 'remove <key>', "
                      "applied to the metadata inherited from Source in the order written, the "
                      "last operation on a key winning: 'set k' then 'remove k' leaves k absent, "
                      "'remove k' then 'set k' leaves it present. Removing a key which is not "
                      "there does nothing. A value is the literal rest of the line, not an "
                      "expression, and is contributed as a string. A key is written exactly as "
                      "typed; name your own under 'ofx/' or a reverse DNS prefix.");
  operations->setStringType(eStringTypeMultiLine);
  operations->setDefault("");
  operations->setAnimates(false);
  page->addChild(*operations);
}

OFX::ImageEffect* MetadataModifyExamplePluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
  return new MetadataModifyPlugin(handle);
}

namespace OFX
{
  namespace Plugin
  {
    void getPluginIDs(OFX::PluginFactoryArray &ids)
    {
      static MetadataModifyExamplePluginFactory p("org.openfx.examples.metadataModify", 1, 0);
      ids.push_back(&p);
    }
  }
}
