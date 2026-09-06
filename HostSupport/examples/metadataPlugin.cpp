// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#include <cstring>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

#include "ofxCore.h"
#include "ofxImageEffect.h"
#include "ofxMessage.h"
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
// action from the plugin side of the API. It composes the metadata of its two
// input clips, in an order its 'compositionOrder' parameter selects, and retains
// from each clip only the keys in the standard 'ofx/' namespace, dropping
// everything else.
//
// It is deliberately written against the plugin facing C api alone, with no
// knowledge of which keys its inputs carry: it enumerates them and reads each one
// back by the type the host reports for it, which is what a plugin that means to
// pass metadata through has to do.
//
// It also writes a handful of keys of its own into the set the host hands it, which
// the host has to put over whatever the same key inherited. One value of its
// 'compositionOrder' parameter makes it write into everything the action can write
// into and then report the action untrapped, which a host has to ignore in full, and
// another makes it nominate no source clip at all.

// the host composes this property's name by post pending the clip's name, and the
// api defines the prefix in prose rather than as a macro
static const char kRetainedKeysPropPrefix[] = "OfxImageClipPropMetadataRetainedKeys_";

static const char kSourceClip[] = kOfxImageEffectSimpleSourceClipName;
static const char kMaskClip[]   = "Mask";
static const char kOrderParam[] = "compositionOrder";

// the value of the composition order parameter which selects the path that writes and
// then reports the action untrapped
static const int kOrderUntrapped = 2;

// the value which selects the path nominating no source clip at all
static const int kOrderNoSource = 3;

// the keys the plugin writes into the set it is handed, under its own reverse DNS prefix
// as the standard requires of a key it does not define itself. The frame rate is one the
// plugin also retains from Source, so that the host putting one over the other is
// observable, and the last is named after the property the host reads the composition
// order out of, which lives in the action's out args and so cannot be confused with a key
// of that name
static const char   kContributedNoteKey[]    = "org.openfx.examples.metadataPlugin.note";
static const char   kContributedGainKey[]    = "org.openfx.examples.metadataPlugin.gain";
static const char   kContributedPassesKey[]  = "org.openfx.examples.metadataPlugin.passes";
static const char   kContributedWindowKey[]  = "org.openfx.examples.metadataPlugin.window";

static const double kContributedGain         = 1.75;
static const int    kContributedPasses       = 5;
static const int    kContributedWindow[]     = {12, 24, 1908, 1056};
static const double kContributedFrameRate    = 48.0;
static const char   kContributedSourceClip[] = "contributed";

static const char kNoteParam[]    = "note";
static const char kNoteDefault[]  = "unset";

// nothing in this plugin reads this one: it is declared so that a host's choice parameter
// instance is instantiated and can be driven
static const char kDetailParam[]  = "detail";
static const int  kDetailDefault  = 0;

static OfxHost                      *gHost = 0;
static const OfxImageEffectSuiteV1  *gEffectSuite = 0;
static const OfxPropertySuiteV2     *gPropSuite = 0;
static const OfxParameterSuiteV1    *gParamSuite = 0;
static const OfxMetadataSuiteV1     *gMetadataSuite = 0;
static const OfxMessageSuiteV2      *gMessageSuite = 0;

static OfxStatus collectKey(const char *key, void *userData)
{
  try {
    ((std::vector<std::string> *) userData)->push_back(key);
  }
  catch (...) {
    return kOfxStatErrMemory;
  }
  return kOfxStatOK;
}

/// read a key back by the type the host reports for it, rather than by knowing in
/// advance what type it should be
static bool readValue(OfxPropertySetHandle metadata, const char *key)
{
  OfxPropDataType type = kOfxPropDataTypeNone;
  int dimension = 0;

  if(gPropSuite->propGetType(metadata, key, &type) != kOfxStatOK)
    return false;

  if(gPropSuite->propGetDimension(metadata, key, &dimension) != kOfxStatOK || dimension < 1)
    return false;

  switch(type) {
  case kOfxPropDataTypeString : {
    for(int i = 0; i < dimension; ++i) {
      char *v = 0;
      if(gPropSuite->propGetString(metadata, key, i, &v) != kOfxStatOK || !v)
        return false;
    }
    return true;
  }

  case kOfxPropDataTypeDouble : {
    std::vector<double> v(dimension);
    return gPropSuite->propGetDoubleN(metadata, key, dimension, &v[0]) == kOfxStatOK;
  }

  case kOfxPropDataTypeInteger : {
    std::vector<int> v(dimension);
    return gPropSuite->propGetIntN(metadata, key, dimension, &v[0]) == kOfxStatOK;
  }

  default :
    return false;
  }
}

/// list in outArgs the keys retained from the named clip, which are the ones in the
/// standard namespace, having read every key the clip carries to check it can be
static OfxStatus setRetainedKeys(OfxImageEffectHandle effect,
                                 OfxPropertySetHandle outArgs,
                                 const char *clipName,
                                 OfxTime time)
{
  OfxImageClipHandle clip = 0;

  if(gEffectSuite->clipGetHandle(effect, clipName, &clip, 0) != kOfxStatOK)
    return kOfxStatFailed;

  OfxPropertySetHandle metadata = 0;
  const OfxStatus fetched = gMetadataSuite->clipGetMetadata(clip, time, &metadata);

  // on anything but kOfxStatOK the host has set the handle to NULL and there is
  // nothing to release
  if(fetched == kOfxStatReplyDefault)
    return kOfxStatOK;
  if(fetched != kOfxStatOK)
    return fetched;

  std::vector<std::string> keys;
  OfxStatus status = gMetadataSuite->metadataEnumerate(metadata, collectKey, &keys);

  std::vector<const char *> retained;
  const size_t standardLen = strlen(kOfxMetadataKeyPrefixStandard);

  for(size_t i = 0; status == kOfxStatOK && i < keys.size(); ++i) {
    if(!readValue(metadata, keys[i].c_str())) {
      status = kOfxStatFailed;
      break;
    }

    if(keys[i].compare(0, standardLen, kOfxMetadataKeyPrefixStandard) == 0)
      retained.push_back(keys[i].c_str());
  }

  if(status == kOfxStatOK) {
    const std::string propName = std::string(kRetainedKeysPropPrefix) + clipName;

    status = gPropSuite->propSetStringN(outArgs,
                                        propName.c_str(),
                                        int(retained.size()),
                                        retained.empty() ? 0 : &retained[0]);
  }

  gMetadataSuite->metadataRelease(metadata);

  return status;
}

static OfxStatus getMetadata(OfxImageEffectHandle effect,
                             OfxPropertySetHandle inArgs,
                             OfxPropertySetHandle outArgs)
{
  OfxTime time = 0;

  if(gPropSuite->propGetDouble(inArgs, kOfxPropTime, 0, &time) != kOfxStatOK)
    return kOfxStatFailed;

  void *vended = 0;

  if(gPropSuite->propGetPointer(inArgs, kOfxImageEffectPropMetadataSet, 0, &vended) != kOfxStatOK || !vended)
    return kOfxStatFailed;

  std::vector<std::string> written;

  if(gMetadataSuite->metadataEnumerate((OfxPropertySetHandle) vended, collectKey, &written) != kOfxStatOK)
    return kOfxStatFailed;

  // the set arrives empty, and the log line is how a host driving this plugin sees that
  // it did
  gMessageSuite->message(effect, kOfxMessageLog, "metadataPlugin",
                         "metadataPlugin metadataset present keys=%d", int(written.size()));

  OfxParamSetHandle paramSet = 0;
  OfxParamHandle order = 0;
  OfxParamHandle noteParam = 0;
  int reversed = 0;
  char *note = 0;

  if(gEffectSuite->getParamSet(effect, &paramSet) != kOfxStatOK)
    return kOfxStatFailed;
  if(gParamSuite->paramGetHandle(paramSet, kOrderParam, &order, 0) != kOfxStatOK)
    return kOfxStatFailed;
  if(gParamSuite->paramGetValueAtTime(order, time, &reversed) != kOfxStatOK)
    return kOfxStatFailed;
  if(gParamSuite->paramGetHandle(paramSet, kNoteParam, &noteParam, 0) != kOfxStatOK)
    return kOfxStatFailed;
  if(gParamSuite->paramGetValueAtTime(noteParam, time, &note) != kOfxStatOK || !note)
    return kOfxStatFailed;

  OfxPropertySetHandle contribution = (OfxPropertySetHandle) vended;
  const int window = int(sizeof(kContributedWindow) / sizeof(kContributedWindow[0]));

  if(gMetadataSuite->metadataSetString(contribution, kContributedNoteKey, note) != kOfxStatOK)
    return kOfxStatFailed;
  if(gMetadataSuite->metadataSetDouble(contribution, kContributedGainKey, kContributedGain) != kOfxStatOK)
    return kOfxStatFailed;
  if(gMetadataSuite->metadataSetInt(contribution, kContributedPassesKey, kContributedPasses) != kOfxStatOK)
    return kOfxStatFailed;
  if(gMetadataSuite->metadataSetIntN(contribution, kContributedWindowKey, window, kContributedWindow) != kOfxStatOK)
    return kOfxStatFailed;
  if(gMetadataSuite->metadataSetDouble(contribution, kOfxMetadataKeyFrameRate, kContributedFrameRate) != kOfxStatOK)
    return kOfxStatFailed;
  if(gMetadataSuite->metadataSetString(contribution, kOfxImageEffectPropMetadataSourceClip,
                                       kContributedSourceClip) != kOfxStatOK)
    return kOfxStatFailed;

  // none of what this path writes is what the host arrives at on its own: it nominates
  // the clip the host does not default to and drops the keys retained from the one it
  // does, on top of the keys already contributed above, and then reports the action
  // untrapped so that every one of those writes has to be ignored
  if(reversed == kOrderUntrapped) {
    const char *nominated = kMaskClip;
    const std::string retained = std::string(kRetainedKeysPropPrefix) + kSourceClip;

    if(gPropSuite->propSetStringN(outArgs, kOfxImageEffectPropMetadataSourceClip, 1, &nominated) != kOfxStatOK)
      return kOfxStatFailed;
    if(gPropSuite->propSetStringN(outArgs, retained.c_str(), 0, 0) != kOfxStatOK)
      return kOfxStatFailed;

    return kOfxStatReplyDefault;
  }

  // nominating no clip at all leaves the output nothing to inherit, so it carries only
  // what was contributed above
  if(reversed == kOrderNoSource)
    return gPropSuite->propSetStringN(outArgs, kOfxImageEffectPropMetadataSourceClip, 0, 0);

  // the list is read in increasing precedence, so the clip named last wins
  const char *sources[2];
  sources[0] = reversed ? kMaskClip   : kSourceClip;
  sources[1] = reversed ? kSourceClip : kMaskClip;

  OfxStatus status = gPropSuite->propSetStringN(outArgs, kOfxImageEffectPropMetadataSourceClip, 2, sources);

  if(status == kOfxStatOK)
    status = setRetainedKeys(effect, outArgs, kSourceClip, time);
  if(status == kOfxStatOK)
    status = setRetainedKeys(effect, outArgs, kMaskClip, time);

  return status;
}

static OfxStatus describeInContext(OfxImageEffectHandle effect, OfxPropertySetHandle /*inArgs*/)
{
  OfxPropertySetHandle props;

  gEffectSuite->clipDefine(effect, kOfxImageEffectOutputClipName, &props);
  gPropSuite->propSetString(props, kOfxImageEffectPropSupportedComponents, 0, kOfxImageComponentRGBA);

  // the clips are declared in this order, so Source is the one whose metadata the
  // host offers by default
  gEffectSuite->clipDefine(effect, kSourceClip, &props);
  gPropSuite->propSetString(props, kOfxImageEffectPropSupportedComponents, 0, kOfxImageComponentRGBA);

  gEffectSuite->clipDefine(effect, kMaskClip, &props);
  gPropSuite->propSetString(props, kOfxImageEffectPropSupportedComponents, 0, kOfxImageComponentRGBA);

  OfxParamSetHandle paramSet;
  OfxPropertySetHandle paramProps;

  if(gEffectSuite->getParamSet(effect, &paramSet) != kOfxStatOK)
    return kOfxStatFailed;
  if(gParamSuite->paramDefine(paramSet, kOfxParamTypeInteger, kOrderParam, &paramProps) != kOfxStatOK)
    return kOfxStatFailed;

  gPropSuite->propSetInt(paramProps, kOfxParamPropDefault, 0, 0);
  gPropSuite->propSetString(paramProps, kOfxPropLabel, 0, "Composition Order");
  gPropSuite->propSetString(paramProps, kOfxParamPropHint, 0,
                            "0 composes Mask over Source, 1 composes Source over Mask, "
                            "2 writes and then reports the action untrapped, "
                            "3 nominates no source clip at all");

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
  // v2 of the property suite is the one carrying propGetType, which reading a key of
  // an unknown type needs
  gPropSuite     = (const OfxPropertySuiteV2 *)    gHost->fetchSuite(gHost->host, kOfxPropertySuite, 2);
  gParamSuite    = (const OfxParameterSuiteV1 *)   gHost->fetchSuite(gHost->host, kOfxParameterSuite, 1);
  gMetadataSuite = (const OfxMetadataSuiteV1 *)    gHost->fetchSuite(gHost->host, kOfxMetadataSuite, 1);
  gMessageSuite  = (const OfxMessageSuiteV2 *)     gHost->fetchSuite(gHost->host, kOfxMessageSuite, 2);

  if(!gEffectSuite || !gPropSuite || !gParamSuite || !gMetadataSuite || !gMessageSuite)
    return kOfxStatErrMissingHostFeature;

  return kOfxStatOK;
}

static OfxStatus pluginMain(const char *action,
                            const void *handle,
                            OfxPropertySetHandle inArgs,
                            OfxPropertySetHandle outArgs)
{
  try {
    OfxImageEffectHandle effect = (OfxImageEffectHandle) handle;

    if(strcmp(action, kOfxActionLoad) == 0)
      return onLoad();
    else if(strcmp(action, kOfxActionDescribe) == 0)
      return describe(effect);
    else if(strcmp(action, kOfxImageEffectActionDescribeInContext) == 0)
      return describeInContext(effect, inArgs);
    else if(strcmp(action, kOfxImageEffectActionGetMetadata) == 0)
      return getMetadata(effect, inArgs, outArgs);
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
  "org.openfx.examples.metadataPlugin",
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
