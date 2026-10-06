// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#include <cstring>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

#include "ofxCore.h"
#include "ofxImageEffect.h"
#include "ofxParam.h"
#include "ofxProperty.h"
#include "ofxMetadata.h"

#if defined __APPLE__ || defined __linux__ || defined __FreeBSD__
#  define EXPORT __attribute__((visibility("default")))
#elif defined _WIN32
#  define EXPORT OfxExport
#else
#  error Not building on your operating system quite yet
#endif

////////////////////////////////////////////////////////////////////////////////
// A plugin that does nothing to pixels and exists only to exercise the metadata
// suite from the plugin side of the API. At every frame it is asked to render, it
// fetches the metadata of each of its input clips, enumerates it and reads every key
// back, and fails the render if any of that fails.
//
// It is deliberately written against the plugin facing C api alone, with no
// knowledge of which keys its inputs carry: it reads each key back by the type the
// host reports for it, which is what a plugin that means to use metadata it was not
// told about has to do. It logs nothing, so the harness can hold it up as a plugin
// that reads its clips and says nothing about them.

static const char kSourceClip[] = kOfxImageEffectSimpleSourceClipName;
static const char kMaskClip[]   = "Mask";

// nothing in this plugin reads its parameters: they are declared so that a host's string
// and choice parameter instances are instantiated and can be driven
static const char kNoteParam[]    = "note";
static const char kNoteDefault[]  = "unset";
static const char kDetailParam[]  = "detail";
static const int  kDetailDefault  = 0;

static OfxHost                      *gHost = 0;
static const OfxImageEffectSuiteV1  *gEffectSuite = 0;
static const OfxPropertySuiteV1     *gPropSuite = 0;
static const OfxParameterSuiteV1    *gParamSuite = 0;
static const OfxMetadataSuiteV1     *gMetadataSuite = 0;

struct KeyInfo {
  std::string key;
  OfxMetadataValueType type;
  int dimension;
};

static OfxStatus collectKey(const char *key, OfxMetadataValueType type, int dimension, void *userData)
{
  try {
    KeyInfo info;
    info.key = key;
    info.type = type;
    info.dimension = dimension;
    ((std::vector<KeyInfo> *) userData)->push_back(info);
  }
  catch (...) {
    return kOfxStatErrMemory;
  }
  return kOfxStatOK;
}

/// read a key back by the type and dimension metadataEnumerate reported for it, rather
/// than by knowing in advance what type it should be
static bool readValue(OfxPropertySetHandle metadata, const char *key, OfxMetadataValueType type, int dimension)
{
  if(dimension < 1)
    return false;

  switch(type) {
  case kOfxMetadataValueTypeString : {
    for(int i = 0; i < dimension; ++i) {
      char *v = 0;
      if(gPropSuite->propGetString(metadata, key, i, &v) != kOfxStatOK || !v)
        return false;
    }
    return true;
  }

  case kOfxMetadataValueTypeDouble : {
    std::vector<double> v(dimension);
    return gPropSuite->propGetDoubleN(metadata, key, dimension, &v[0]) == kOfxStatOK;
  }

  case kOfxMetadataValueTypeInteger : {
    std::vector<int> v(dimension);
    return gPropSuite->propGetIntN(metadata, key, dimension, &v[0]) == kOfxStatOK;
  }

  default :
    return false;
  }
}

/// read every key the named clip carries at the given time
static OfxStatus readClip(OfxImageEffectHandle effect, const char *clipName, OfxTime time)
{
  OfxImageClipHandle clip = 0;

  if(gEffectSuite->clipGetHandle(effect, clipName, &clip, 0) != kOfxStatOK)
    return kOfxStatFailed;

  OfxPropertySetHandle metadata = 0;
  const OfxStatus fetched = gMetadataSuite->clipGetMetadata(clip, time, &metadata);

  if(fetched != kOfxStatOK)
    return fetched;

  std::vector<KeyInfo> keys;
  OfxStatus status = gMetadataSuite->metadataEnumerate(metadata, collectKey, &keys);

  for(size_t i = 0; status == kOfxStatOK && i < keys.size(); ++i) {
    if(!readValue(metadata, keys[i].key.c_str(), keys[i].type, keys[i].dimension))
      status = kOfxStatFailed;
  }

  gMetadataSuite->metadataRelease(metadata);

  return status;
}

static OfxStatus render(OfxImageEffectHandle effect, OfxPropertySetHandle inArgs)
{
  OfxTime time = 0;

  if(gPropSuite->propGetDouble(inArgs, kOfxPropTime, 0, &time) != kOfxStatOK)
    return kOfxStatFailed;

  OfxStatus status = readClip(effect, kSourceClip, time);

  if(status == kOfxStatOK)
    status = readClip(effect, kMaskClip, time);

  return status;
}

static OfxStatus describeInContext(OfxImageEffectHandle effect, OfxPropertySetHandle /*inArgs*/)
{
  OfxPropertySetHandle props;

  gEffectSuite->clipDefine(effect, kOfxImageEffectOutputClipName, &props);
  gPropSuite->propSetString(props, kOfxImageEffectPropSupportedComponents, 0, kOfxImageComponentRGBA);

  gEffectSuite->clipDefine(effect, kSourceClip, &props);
  gPropSuite->propSetString(props, kOfxImageEffectPropSupportedComponents, 0, kOfxImageComponentRGBA);

  gEffectSuite->clipDefine(effect, kMaskClip, &props);
  gPropSuite->propSetString(props, kOfxImageEffectPropSupportedComponents, 0, kOfxImageComponentRGBA);

  OfxParamSetHandle paramSet;
  OfxPropertySetHandle paramProps;

  if(gEffectSuite->getParamSet(effect, &paramSet) != kOfxStatOK)
    return kOfxStatFailed;
  if(gParamSuite->paramDefine(paramSet, kOfxParamTypeString, kNoteParam, &paramProps) != kOfxStatOK)
    return kOfxStatFailed;

  gPropSuite->propSetString(paramProps, kOfxParamPropDefault, 0, kNoteDefault);
  gPropSuite->propSetString(paramProps, kOfxPropLabel, 0, "Note");

  if(gParamSuite->paramDefine(paramSet, kOfxParamTypeChoice, kDetailParam, &paramProps) != kOfxStatOK)
    return kOfxStatFailed;

  gPropSuite->propSetInt(paramProps, kOfxParamPropDefault, 0, kDetailDefault);
  gPropSuite->propSetString(paramProps, kOfxParamPropChoiceOption, 0, "terse");
  gPropSuite->propSetString(paramProps, kOfxParamPropChoiceOption, 1, "verbose");
  gPropSuite->propSetString(paramProps, kOfxParamPropChoiceOption, 2, "full");
  gPropSuite->propSetString(paramProps, kOfxPropLabel, 0, "Detail");

  return kOfxStatOK;
}

static OfxStatus describe(OfxImageEffectHandle effect)
{
  OfxPropertySetHandle effectProps;

  gEffectSuite->getPropertySet(effect, &effectProps);

  gPropSuite->propSetInt(effectProps, kOfxImageEffectPropSupportsMultipleClipDepths, 0, 0);
  gPropSuite->propSetString(effectProps, kOfxImageEffectPropSupportedPixelDepths, 0, kOfxBitDepthByte);
  gPropSuite->propSetString(effectProps, kOfxPropLabel, 0, "OFX Metadata Example");
  gPropSuite->propSetString(effectProps, kOfxImageEffectPluginPropGrouping, 0, "OFX Example");
  gPropSuite->propSetString(effectProps, kOfxImageEffectPropSupportedContexts, 0, kOfxImageEffectContextGeneral);

  return kOfxStatOK;
}

static OfxStatus onLoad(void)
{
  if(!gHost)
    return kOfxStatErrMissingHostFeature;

  gEffectSuite   = (const OfxImageEffectSuiteV1 *) gHost->fetchSuite(gHost->host, kOfxImageEffectSuite, 1);
  gPropSuite     = (const OfxPropertySuiteV1 *)    gHost->fetchSuite(gHost->host, kOfxPropertySuite, 1);
  gParamSuite    = (const OfxParameterSuiteV1 *)   gHost->fetchSuite(gHost->host, kOfxParameterSuite, 1);
  gMetadataSuite = (const OfxMetadataSuiteV1 *)    gHost->fetchSuite(gHost->host, kOfxMetadataSuite, 1);

  if(!gEffectSuite || !gPropSuite || !gParamSuite || !gMetadataSuite)
    return kOfxStatErrMissingHostFeature;

  return kOfxStatOK;
}

static OfxStatus pluginMain(const char *action,
                            const void *handle,
                            OfxPropertySetHandle inArgs,
                            OfxPropertySetHandle /*outArgs*/)
{
  try {
    OfxImageEffectHandle effect = (OfxImageEffectHandle) handle;

    if(strcmp(action, kOfxActionLoad) == 0)
      return onLoad();
    else if(strcmp(action, kOfxActionDescribe) == 0)
      return describe(effect);
    else if(strcmp(action, kOfxImageEffectActionDescribeInContext) == 0)
      return describeInContext(effect, inArgs);
    else if(strcmp(action, kOfxImageEffectActionRender) == 0)
      return render(effect, inArgs);
  }
  catch (const std::bad_alloc &) {
    return kOfxStatErrMemory;
  }
  catch (const std::exception &) {
    return kOfxStatErrUnknown;
  }
  catch (...) {
    return kOfxStatErrUnknown;
  }

  return kOfxStatReplyDefault;
}

static void setHostFunc(OfxHost *hostStruct)
{
  gHost = hostStruct;
}

static OfxPlugin metadataPlugin =
{
  kOfxImageEffectPluginApi,
  1,
  "net.sf.openfx.metadataPlugin",
  1,
  0,
  setHostFunc,
  pluginMain
};

EXPORT OfxPlugin *
OfxGetPlugin(int nth)
{
  if(nth == 0)
    return &metadataPlugin;
  return 0;
}

EXPORT int
OfxGetNumberOfPlugins(void)
{
  return 1;
}
