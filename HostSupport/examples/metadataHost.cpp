// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef OFX_SUPPORTS_METADATA
#error metadataHost has nothing to exercise unless OFX_SUPPORTS_METADATA is defined
#endif

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// ofx
#include "ofxCore.h"
#include "ofxProperty.h"
#include "ofxImageEffect.h"
#include "ofxMessage.h"
#include "ofxPixels.h"
#include "ofxMetadata.h"

// ofx host
#include "ofxhBinary.h"
#include "ofxhPropertySuite.h"
#include "ofxhClip.h"
#include "ofxhParam.h"
#include "ofxhMemory.h"
#include "ofxhImageEffect.h"
#include "ofxhPluginAPICache.h"
#include "ofxhPluginCache.h"
#include "ofxhHost.h"
#include "ofxhImageEffectAPI.h"

// my host
#include "hostDemoHostDescriptor.h"
#include "hostDemoEffectInstance.h"
#include "hostDemoClipInstance.h"
#include "hostDemoParamInstance.h"

#include "metadataHostFixture.h"

#ifndef METADATA_PLUGIN_DIR
#error metadataHost needs the directory holding metadataPlugin.ofx.bundle baked in
#endif

////////////////////////////////////////////////////////////////////////////////
// A headless smoke test that publishes the metadata in metadataHostFixture.h and reads
// it back through the plugin facing C api. Every check is printed on one line ending in
// PASS or FAIL, and it exits non zero if any of them failed.

namespace MyHost {

  /// the clip and key whose published value carries gRevision below
  const char *const kRevisedClip = MetadataFixture::kInputClips[0];
  const char kRevisedKey[] = kOfxMetadataKeyTimecode;

  /// bumping this changes what the clips publish for kRevisedKey, so that a clip can be
  /// made to carry something new without the fixture being edited
  int gRevision = 0;

  /// revision zero publishes the fixture's own value, so the checks that compare what
  /// they read against the fixture hold as long as the revision is back at zero
  std::string revisedValue(const std::string &value, int revision)
  {
    if(revision == 0)
      return value;

    std::ostringstream os;
    os << value << "/r" << revision;
    return os.str();
  }

  /// a clip that publishes the metadata the fixture gives for it at the requested time
  class MetadataClipInstance : public MyClipInstance {
  public :
    explicit MetadataClipInstance(OFX::Host::ImageEffect::ClipDescriptor *desc,
                                  MyEffectInstance *effect = NULL)
      : MyClipInstance(effect, desc)
    {
    }

  protected :
    virtual void fetchMetadata(OfxTime time, OFX::Host::Property::Set &metadata);
  };

  /// an effect whose clips publish the fixture
  class MetadataEffectInstance : public MyEffectInstance {
  public :
    MetadataEffectInstance(OFX::Host::ImageEffect::ImageEffectPlugin *plugin,
                           OFX::Host::ImageEffect::Descriptor &desc,
                           const std::string &context)
      : MyEffectInstance(plugin, desc, context)
    {
    }

    virtual OFX::Host::ImageEffect::ClipInstance *newClipInstance(OFX::Host::ImageEffect::Instance *,
                                                                  OFX::Host::ImageEffect::ClipDescriptor *descriptor,
                                                                  int)
    {
      return new MetadataClipInstance(descriptor, this);
    }
  };

  class MetadataHost : public Host {
  public :
    virtual OFX::Host::ImageEffect::Instance *newInstance(void *,
                                                          OFX::Host::ImageEffect::ImageEffectPlugin *plugin,
                                                          OFX::Host::ImageEffect::Descriptor &desc,
                                                          const std::string &context)
    {
      return new MetadataEffectInstance(plugin, desc, context);
    }
  };

  void MetadataClipInstance::fetchMetadata(OfxTime time, OFX::Host::Property::Set &metadata)
  {
    // an output clip still derives what its effect's inputs carry
    MyClipInstance::fetchMetadata(time, metadata);

    const std::string &clip = getName();

    for(int i = 0; i < MetadataFixture::kEntryCount; ++i) {
      const MetadataFixture::Entry &entry = MetadataFixture::kEntries[i];

      if(clip != entry.clip)
        continue;

      if(entry.time != MetadataFixture::kAnyTime && entry.time != time)
        continue;

      switch(entry.type) {
      case MetadataFixture::eString : {
        const OFX::Host::Property::PropSpec spec = {entry.key, OFX::Host::Property::eString, 1, true, ""};
        const bool revised = clip == kRevisedClip && std::strcmp(entry.key, kRevisedKey) == 0;
        metadata.createProperty(spec);
        metadata.setStringProperty(entry.key, revised ? revisedValue(entry.stringValue, gRevision)
                                                      : std::string(entry.stringValue));
      } break;

      case MetadataFixture::eDouble : {
        const OFX::Host::Property::PropSpec spec = {entry.key, OFX::Host::Property::eDouble, 1, true, "0"};
        metadata.createProperty(spec);
        metadata.setDoubleProperty(entry.key, entry.doubleValue);
      } break;

      case MetadataFixture::eInt : {
        const OFX::Host::Property::PropSpec spec = {entry.key, OFX::Host::Property::eInt, entry.intCount, true, "0"};
        metadata.createProperty(spec);
        metadata.setIntPropertyN(entry.key, entry.intValues, entry.intCount);
      } break;
      }
    }
  }

} // MyHost

namespace {

  const OfxPropertySuiteV2    *gPropSuite = NULL;
  const OfxMetadataSuiteV1    *gMetadataSuite = NULL;
  const OfxImageEffectSuiteV1 *gEffectSuite = NULL;
  const OfxMessageSuiteV2     *gMessageSuite = NULL;

  ////////////////////////////////////////////////////////////////////////////////
  // formatting, shared by the fixture listing and the values read back so that the
  // two are compared as they are printed

  std::string formatDouble(double v)
  {
    std::ostringstream os;
    os << std::setprecision(17) << v;
    return os.str();
  }

  std::string formatInts(const int *v, int n)
  {
    std::ostringstream os;
    for(int i = 0; i < n; ++i) {
      if(i)
        os << ',';
      os << v[i];
    }
    return os.str();
  }

  std::string formatTime(OfxTime time)
  {
    if(time == MetadataFixture::kAnyTime)
      return "any";

    std::ostringstream os;
    os << time;
    return os.str();
  }

  const char *typeName(MetadataFixture::ValueType type)
  {
    switch(type) {
    case MetadataFixture::eString : return "string";
    case MetadataFixture::eDouble : return "double";
    case MetadataFixture::eInt    : return "int";
    }
    return "unknown";
  }

  std::string entryValue(const MetadataFixture::Entry &entry)
  {
    switch(entry.type) {
    case MetadataFixture::eString : return entry.stringValue;
    case MetadataFixture::eDouble : return formatDouble(entry.doubleValue);
    case MetadataFixture::eInt    : return formatInts(entry.intValues, entry.intCount);
    }
    return "";
  }

  int entryDimension(const MetadataFixture::Entry &entry)
  {
    return entry.type == MetadataFixture::eInt ? entry.intCount : 1;
  }

  bool entryAppliesAt(const MetadataFixture::Entry &entry, const std::string &clip, OfxTime time)
  {
    if(clip != entry.clip)
      return false;
    return entry.time == MetadataFixture::kAnyTime || entry.time == time;
  }

  /// the value the fixture gives for one key of a clip at a time, false if it gives none
  bool fixtureValue(const std::string &clip, const std::string &key, OfxTime time, std::string &value)
  {
    for(int i = 0; i < MetadataFixture::kEntryCount; ++i) {
      const MetadataFixture::Entry &entry = MetadataFixture::kEntries[i];

      if(key == entry.key && entryAppliesAt(entry, clip, time)) {
        value = entryValue(entry);
        return true;
      }
    }

    return false;
  }

  ////////////////////////////////////////////////////////////////////////////////
  // the fixture listing

  void listFixture()
  {
    std::cout << "fixture frames " << formatTime(MetadataFixture::kFirstFrame)
              << " " << formatTime(MetadataFixture::kLastFrame) << std::endl;

    std::cout << "fixture clips";
    for(int i = 0; i < MetadataFixture::kInputClipCount; ++i)
      std::cout << " " << MetadataFixture::kInputClips[i];
    std::cout << " " << MetadataFixture::kOutputClip << std::endl;

    for(int i = 0; i < MetadataFixture::kEntryCount; ++i) {
      const MetadataFixture::Entry &entry = MetadataFixture::kEntries[i];

      std::cout << "fixture clip=" << entry.clip
                << " time=" << formatTime(entry.time)
                << " key=" << entry.key
                << " type=" << typeName(entry.type)
                << " dim=" << entryDimension(entry)
                << " value=" << entryValue(entry) << std::endl;
    }

    std::cout << "fixture entries " << MetadataFixture::kEntryCount << std::endl;
  }

  ////////////////////////////////////////////////////////////////////////////////
  // the checks

  class Report {
    int _checks;
    int _failures;

  public :
    Report() : _checks(0), _failures(0) {}

    bool check(bool ok, const std::string &what)
    {
      _checks += 1;
      if(!ok)
        _failures += 1;

      std::cout << "check " << what << (ok ? " PASS" : " FAIL") << std::endl;

      return ok;
    }

    int getChecks() const {return _checks;}
    int getFailures() const {return _failures;}
  };

  OfxStatus collectKey(const char *key, void *userData)
  {
    ((std::set<std::string> *) userData)->insert(key);
    return kOfxStatOK;
  }

  std::string joinKeys(const std::set<std::string> &keys)
  {
    std::string joined;
    for(std::set<std::string>::const_iterator it = keys.begin(); it != keys.end(); ++it) {
      if(!joined.empty())
        joined += ",";
      joined += *it;
    }
    return joined;
  }

  /// read a key back the way a plugin has to, by asking the host what type it is rather
  /// than by knowing in advance
  bool readValue(OfxPropertySetHandle metadata, const char *key, std::string &type, std::string &value)
  {
    OfxPropDataType dataType = kOfxPropDataTypeNone;
    int dimension = 0;

    if(gPropSuite->propGetType(metadata, key, &dataType) != kOfxStatOK)
      return false;

    if(gPropSuite->propGetDimension(metadata, key, &dimension) != kOfxStatOK)
      return false;

    switch(dataType) {
    case kOfxPropDataTypeString : {
      char *v = NULL;
      if(dimension != 1 || gPropSuite->propGetString(metadata, key, 0, &v) != kOfxStatOK || !v)
        return false;
      type = "string";
      value = v;
      return true;
    }

    case kOfxPropDataTypeDouble : {
      double v = 0;
      if(dimension != 1 || gPropSuite->propGetDouble(metadata, key, 0, &v) != kOfxStatOK)
        return false;
      type = "double";
      value = formatDouble(v);
      return true;
    }

    case kOfxPropDataTypeInteger : {
      int v[MetadataFixture::kMaxInts];
      if(dimension < 1 || dimension > MetadataFixture::kMaxInts)
        return false;
      if(gPropSuite->propGetIntN(metadata, key, dimension, v) != kOfxStatOK)
        return false;
      type = "int";
      value = formatInts(v, dimension);
      return true;
    }

    default :
      return false;
    }
  }

  /// check that a metadata set holds exactly the keys, types and values the fixture
  /// gives for this clip at this time, and return what was read for each key
  void checkAgainstFixture(Report &report,
                           OfxPropertySetHandle metadata,
                           const std::string &clip,
                           OfxTime time,
                           const std::string &where,
                           std::map<std::string, std::string> &read)
  {
    std::set<std::string> expected;
    for(int i = 0; i < MetadataFixture::kEntryCount; ++i) {
      if(entryAppliesAt(MetadataFixture::kEntries[i], clip, time))
        expected.insert(MetadataFixture::kEntries[i].key);
    }

    std::set<std::string> found;
    const OfxStatus st = gMetadataSuite->metadataEnumerate(metadata, collectKey, &found);

    report.check(st == kOfxStatOK && found == expected, where + " keys=" + joinKeys(found));

    for(int i = 0; i < MetadataFixture::kEntryCount; ++i) {
      const MetadataFixture::Entry &entry = MetadataFixture::kEntries[i];

      if(!entryAppliesAt(entry, clip, time))
        continue;

      std::string type = "none";
      std::string value = "none";
      const bool ok = readValue(metadata, entry.key, type, value)
                      && type == typeName(entry.type)
                      && value == entryValue(entry);

      read[entry.key] = value;

      report.check(ok, where + " key=" + entry.key + " type=" + type + " value=" + value);
    }
  }

  /// the keys the fixture gives a different value for at different frames
  void perFrameKeys(const std::string &clip, std::vector<std::string> &keys)
  {
    for(int i = 0; i < MetadataFixture::kEntryCount; ++i) {
      const MetadataFixture::Entry &entry = MetadataFixture::kEntries[i];

      if(clip != entry.clip || entry.time == MetadataFixture::kAnyTime)
        continue;

      if(std::find(keys.begin(), keys.end(), entry.key) == keys.end())
        keys.push_back(entry.key);
    }
  }

  /// the fixture is a table meant to be edited, so check it still carries every case
  /// before checking anything read back from it. An edit that drops the last entry of a
  /// case does not fail any of the checks below, it stops them being made at all, and
  /// the run still ends in RESULT PASS
  void checkFixture(Report &report)
  {
    int strings = 0, doubles = 0, ints = 0, arrays = 0;

    for(int i = 0; i < MetadataFixture::kEntryCount; ++i) {
      const MetadataFixture::Entry &entry = MetadataFixture::kEntries[i];

      switch(entry.type) {
      case MetadataFixture::eString : strings += 1; break;
      case MetadataFixture::eDouble : doubles += 1; break;
      case MetadataFixture::eInt :
        if(entry.intCount > 1)
          arrays += 1;
        else
          ints += 1;
        break;
      }
    }

    std::ostringstream os;
    os << "fixture strings=" << strings << " doubles=" << doubles << " ints=" << ints
       << " intarrays=" << arrays << " clips=" << MetadataFixture::kInputClipCount
       << " frames=" << (MetadataFixture::kLastFrame - MetadataFixture::kFirstFrame + 1);

    report.check(strings > 0 && doubles > 0 && ints > 0 && arrays > 0
                 && MetadataFixture::kInputClipCount > 0
                 && MetadataFixture::kLastFrame > MetadataFixture::kFirstFrame,
                 os.str());
  }

  /// read every frame of a clip through clipGetMetadata
  void checkClip(Report &report, MyHost::MetadataClipInstance &clip)
  {
    for(OfxTime time = MetadataFixture::kFirstFrame; time <= MetadataFixture::kLastFrame; time += 1) {
      std::ostringstream os;
      os << "clip=" << clip.getName() << " time=" << formatTime(time) << " via=clip";
      const std::string where = os.str();

      OfxPropertySetHandle metadata = NULL;
      const OfxStatus st = gMetadataSuite->clipGetMetadata(clip.getHandle(), time, &metadata);

      if(!report.check(st == kOfxStatOK && metadata, where + " fetched"))
        continue;

      std::map<std::string, std::string> read;
      checkAgainstFixture(report, metadata, clip.getName(), time, where, read);

      report.check(gMetadataSuite->metadataRelease(metadata) == kOfxStatOK, where + " released");
    }
  }

  /// check that an image handle carries the metadata of the time it was fetched at
  void checkImageKey(Report &report,
                     const std::string &clip,
                     OfxPropertySetHandle image,
                     const std::string &key,
                     OfxTime time)
  {
    std::ostringstream os;
    os << "clip=" << clip << " time=" << formatTime(time) << " via=liveimages key=" << key;
    const std::string where = os.str();

    std::string expected;

    if(!report.check(fixtureValue(clip, key, time, expected), where + " infixture"))
      return;

    OfxPropertySetHandle metadata = NULL;

    if(!report.check(gMetadataSuite->imageGetMetadata(image, &metadata) == kOfxStatOK && metadata,
                     where + " metadata"))
      return;

    std::string type = "none";
    std::string value = "none";
    const bool ok = readValue(metadata, key.c_str(), type, value);

    report.check(ok && value == expected, where + " value=" + value + " expected=" + expected);

    gMetadataSuite->metadataRelease(metadata);
  }

  /// hold two images of the same clip at once and check each still resolves to the
  /// metadata of its own frame, which it cannot if the clip vends one image object for
  /// both fetches rather than the separate handle per fetch ofxImageEffect.h requires
  void checkLiveImages(Report &report, MyHost::MetadataClipInstance &clip, const std::string &key)
  {
    OfxPropertySetHandle first = NULL;
    OfxPropertySetHandle last = NULL;

    const bool fetched =
      gEffectSuite->clipGetImage(clip.getHandle(), MetadataFixture::kFirstFrame, NULL, &first) == kOfxStatOK
      && first
      && gEffectSuite->clipGetImage(clip.getHandle(), MetadataFixture::kLastFrame, NULL, &last) == kOfxStatOK
      && last;

    if(report.check(fetched, "clip=" + clip.getName() + " liveimages fetched")) {
      report.check(first != last, "clip=" + clip.getName() + " liveimages distinct");

      checkImageKey(report, clip.getName(), first, key, MetadataFixture::kFirstFrame);
      checkImageKey(report, clip.getName(), last, key, MetadataFixture::kLastFrame);
    }

    if(first)
      gEffectSuite->clipReleaseImage(first);
    if(last)
      gEffectSuite->clipReleaseImage(last);
  }

  /// read every frame of a clip through the image fetched at that frame, and check that
  /// the keys the fixture varies per frame do come back varying. A host that attached
  /// metadata to the clip rather than to the image would fail this.
  void checkClipImages(Report &report, MyHost::MetadataClipInstance &clip)
  {
    std::vector<std::string> keys;
    perFrameKeys(clip.getName(), keys);

    std::ostringstream perframe;
    perframe << "clip=" << clip.getName() << " perframekeys=" << keys.size();
    report.check(!keys.empty(), perframe.str());

    std::map<std::string, std::set<std::string> > values;
    int frames = 0;

    for(OfxTime time = MetadataFixture::kFirstFrame; time <= MetadataFixture::kLastFrame; time += 1) {
      std::ostringstream os;
      os << "clip=" << clip.getName() << " time=" << formatTime(time) << " via=image";
      const std::string where = os.str();

      OfxPropertySetHandle image = NULL;
      if(!report.check(gEffectSuite->clipGetImage(clip.getHandle(), time, NULL, &image) == kOfxStatOK && image,
                       where + " fetched"))
        continue;

      OfxPropertySetHandle metadata = NULL;

      if(report.check(gMetadataSuite->imageGetMetadata(image, &metadata) == kOfxStatOK && metadata,
                      where + " metadata")) {
        std::map<std::string, std::string> read;
        checkAgainstFixture(report, metadata, clip.getName(), time, where, read);

        frames += 1;
        for(size_t k = 0; k < keys.size(); ++k)
          values[keys[k]].insert(read[keys[k]]);

        gMetadataSuite->metadataRelease(metadata);
      }

      gEffectSuite->clipReleaseImage(image);
    }

    for(size_t k = 0; k < keys.size(); ++k) {
      std::ostringstream os;
      os << "clip=" << clip.getName() << " key=" << keys[k]
         << " frames=" << frames << " distinct=" << values[keys[k]].size();

      report.check(frames > 1 && int(values[keys[k]].size()) == frames, os.str());
    }

    if(!keys.empty())
      checkLiveImages(report, clip, keys[0]);
  }

  /// the same time must give back the set the clip has cached, a different time must not
  void checkCaching(Report &report, MyHost::MetadataClipInstance &clip)
  {
    OfxPropertySetHandle first = NULL;
    OfxPropertySetHandle again = NULL;
    OfxPropertySetHandle other = NULL;

    gMetadataSuite->clipGetMetadata(clip.getHandle(), MetadataFixture::kFirstFrame, &first);
    gMetadataSuite->clipGetMetadata(clip.getHandle(), MetadataFixture::kFirstFrame, &again);
    gMetadataSuite->clipGetMetadata(clip.getHandle(), MetadataFixture::kLastFrame, &other);

    report.check(first && first == again, "clip=" + clip.getName() + " cache sametime shared");
    report.check(other && other != first, "clip=" + clip.getName() + " cache othertime distinct");

    gMetadataSuite->metadataRelease(first);
    gMetadataSuite->metadataRelease(again);
    gMetadataSuite->metadataRelease(other);
  }

  /// read the fixture straight back off a set of unattached clips, with no effect
  /// behind them
  void checkClips(Report &report)
  {
    std::vector<OFX::Host::ImageEffect::ClipDescriptor *> descriptors;
    std::vector<MyHost::MetadataClipInstance *> clips;

    for(int i = 0; i < MetadataFixture::kInputClipCount; ++i)
      descriptors.push_back(new OFX::Host::ImageEffect::ClipDescriptor(MetadataFixture::kInputClips[i]));
    descriptors.push_back(new OFX::Host::ImageEffect::ClipDescriptor(MetadataFixture::kOutputClip));

    for(size_t i = 0; i < descriptors.size(); ++i)
      clips.push_back(new MyHost::MetadataClipInstance(descriptors[i]));

    for(int i = 0; i < MetadataFixture::kInputClipCount; ++i) {
      checkClip(report, *clips[i]);
      checkClipImages(report, *clips[i]);
      checkCaching(report, *clips[i]);
    }

    // the output clip has no fixture entries and, with no effect behind it, nothing to
    // derive from its inputs either
    MyHost::MetadataClipInstance &output = *clips[MetadataFixture::kInputClipCount];
    OfxPropertySetHandle empty = NULL;
    const OfxStatus st = gMetadataSuite->clipGetMetadata(output.getHandle(), MetadataFixture::kFirstFrame, &empty);

    report.check(st == kOfxStatReplyDefault && empty == NULL,
                 "clip=" + output.getName() + " time=" + formatTime(MetadataFixture::kFirstFrame) + " nometadata");

    for(size_t i = 0; i < clips.size(); ++i)
      delete clips[i];
    for(size_t i = 0; i < descriptors.size(); ++i)
      delete descriptors[i];
  }

  ////////////////////////////////////////////////////////////////////////////////
  // the checks that need the plugin

  const char kPluginId[] = "org.openfx.examples.metadataPlugin";

  /// the plugin's parameter, and the two values of it this checks
  const char kOrderParam[] = "compositionOrder";
  const int  kMaskOverSource = 0;
  const int  kSourceOverMask = 1;

  /// the plugin cache has no way to replace the default search path, only to add to
  /// it, and this must load the plugin built alongside it rather than whatever the
  /// machine happens to have installed
  class BuildTreePluginCache : public OFX::Host::PluginCache {
  public :
    explicit BuildTreePluginCache(const std::string &dir)
    {
      _pluginPath.clear();
      addFileToPath(dir, false);
    }
  };

  /// the plugin retains only the keys of the standard vocabulary, so this is the one
  /// thing the harness has to know about it beyond the order it composes in
  bool isStandardKey(const std::string &key)
  {
    return key.compare(0, strlen(kOfxMetadataKeyPrefixStandard), kOfxMetadataKeyPrefixStandard) == 0;
  }

  /// the input clips the plugin nominates for the given order, in increasing precedence
  void sourceClips(int order, std::vector<std::string> &clips)
  {
    clips.push_back(MetadataFixture::kInputClips[order == kMaskOverSource ? 0 : 1]);
    clips.push_back(MetadataFixture::kInputClips[order == kMaskOverSource ? 1 : 0]);
  }

  /// what composing the fixture in that order, retaining only the standard keys, should
  /// leave on the effect's output clip
  void expectedOutput(int order,
                      OfxTime time,
                      std::map<std::string, std::string> &values,
                      std::map<std::string, std::string> &types)
  {
    std::vector<std::string> clips;
    sourceClips(order, clips);

    for(size_t c = 0; c < clips.size(); ++c) {
      for(int i = 0; i < MetadataFixture::kEntryCount; ++i) {
        const MetadataFixture::Entry &entry = MetadataFixture::kEntries[i];

        if(!entryAppliesAt(entry, clips[c], time) || !isStandardKey(entry.key))
          continue;

        values[entry.key] = entryValue(entry);
        types[entry.key] = typeName(entry.type);
      }
    }
  }

  /// read the effect's output clip at one frame and check it against what composing the
  /// fixture the way the plugin nominates should give
  void checkOutput(Report &report,
                   OFX::Host::ImageEffect::ClipInstance &output,
                   int order,
                   OfxTime time,
                   std::map<std::string, std::string> &read)
  {
    std::ostringstream os;
    os << "effect order=" << order << " time=" << formatTime(time);
    const std::string where = os.str();

    OfxPropertySetHandle metadata = NULL;

    if(!report.check(gMetadataSuite->clipGetMetadata(output.getHandle(), time, &metadata) == kOfxStatOK && metadata,
                     where + " fetched"))
      return;

    std::map<std::string, std::string> values;
    std::map<std::string, std::string> types;
    expectedOutput(order, time, values, types);

    std::set<std::string> expected;
    for(std::map<std::string, std::string>::const_iterator it = values.begin(); it != values.end(); ++it)
      expected.insert(it->first);

    std::set<std::string> found;
    const OfxStatus st = gMetadataSuite->metadataEnumerate(metadata, collectKey, &found);

    report.check(st == kOfxStatOK && found == expected, where + " keys=" + joinKeys(found));

    // read what is there rather than what should be there, so that a key the effect
    // was meant to drop is seen rather than passed over
    for(std::set<std::string>::const_iterator it = found.begin(); it != found.end(); ++it) {
      std::string type = "none";
      std::string value = "none";
      const bool ok = readValue(metadata, it->c_str(), type, value)
                      && values.count(*it)
                      && type == types[*it]
                      && value == values[*it];

      read[*it] = value;

      report.check(ok, where + " key=" + *it + " type=" + type + " value=" + value);
    }

    for(std::map<std::string, std::string>::const_iterator it = values.begin(); it != values.end(); ++it)
      report.check(found.count(it->first) != 0, where + " key=" + it->first + " present");

    report.check(gMetadataSuite->metadataRelease(metadata) == kOfxStatOK, where + " released");
  }

  /// the keys the fixture gives both input clips a different value for at the first
  /// frame, which are the ones the composition order decides
  void contestedKeys(std::vector<std::string> &keys)
  {
    std::map<std::string, std::string> first;

    for(int i = 0; i < MetadataFixture::kEntryCount; ++i) {
      const MetadataFixture::Entry &entry = MetadataFixture::kEntries[i];

      if(!isStandardKey(entry.key))
        continue;

      if(entryAppliesAt(entry, MetadataFixture::kInputClips[0], MetadataFixture::kFirstFrame))
        first[entry.key] = entryValue(entry);
    }

    for(int i = 0; i < MetadataFixture::kEntryCount; ++i) {
      const MetadataFixture::Entry &entry = MetadataFixture::kEntries[i];

      if(!entryAppliesAt(entry, MetadataFixture::kInputClips[1], MetadataFixture::kFirstFrame))
        continue;

      std::map<std::string, std::string>::const_iterator it = first.find(entry.key);

      if(it != first.end() && it->second != entryValue(entry))
        keys.push_back(entry.key);
    }
  }

  /// the keys whose composed value the fixture changes at every frame of its range, in
  /// the given order. A host that derived the output's metadata once and kept it, rather
  /// than per frame, would hand back the same value for these at every frame
  void composedPerFrameKeys(int order, std::vector<std::string> &keys)
  {
    std::map<std::string, std::set<std::string> > seen;
    int frames = 0;

    for(OfxTime time = MetadataFixture::kFirstFrame; time <= MetadataFixture::kLastFrame; time += 1) {
      std::map<std::string, std::string> values;
      std::map<std::string, std::string> types;
      expectedOutput(order, time, values, types);

      for(std::map<std::string, std::string>::const_iterator it = values.begin(); it != values.end(); ++it)
        seen[it->first].insert(it->second);

      frames += 1;
    }

    for(std::map<std::string, std::set<std::string> >::const_iterator it = seen.begin(); it != seen.end(); ++it) {
      if(int(it->second.size()) == frames)
        keys.push_back(it->first);
    }
  }

  /// the keys the fixture has on a nominated clip but which the plugin does not retain,
  /// so that they must not reach the output
  void droppedKeys(std::vector<std::string> &keys)
  {
    for(int i = 0; i < MetadataFixture::kEntryCount; ++i) {
      const MetadataFixture::Entry &entry = MetadataFixture::kEntries[i];

      if(isStandardKey(entry.key))
        continue;

      if(!entryAppliesAt(entry, MetadataFixture::kInputClips[0], MetadataFixture::kFirstFrame))
        continue;

      if(std::find(keys.begin(), keys.end(), entry.key) == keys.end())
        keys.push_back(entry.key);
    }
  }

  /// read one string key of a clip's metadata at the given time
  bool readClipKey(OFX::Host::ImageEffect::ClipInstance &clip,
                   const char *key,
                   OfxTime time,
                   std::string &value)
  {
    OfxPropertySetHandle metadata = NULL;

    if(gMetadataSuite->clipGetMetadata(clip.getHandle(), time, &metadata) != kOfxStatOK || !metadata)
      return false;

    std::string type = "none";
    const bool ok = readValue(metadata, key, type, value) && type == "string";

    gMetadataSuite->metadataRelease(metadata);

    return ok;
  }

  /// change what an input clip publishes, drop that one clip's cached sets, and check
  /// the effect's output clip, which holds copies derived from them, gives back the new
  /// value. A host whose reader reloads one input has only that clip to invalidate
  void checkInvalidation(Report &report, OFX::Host::ImageEffect::Instance &instance)
  {
    OFX::Host::ImageEffect::ClipInstance *input = instance.getClip(MyHost::kRevisedClip);
    OFX::Host::ImageEffect::ClipInstance *output = instance.getClip(kOfxImageEffectOutputClipName);

    if(!report.check(input != NULL && output != NULL,
                     std::string("invalidation clip=") + MyHost::kRevisedClip))
      return;

    std::string published;

    if(!report.check(fixtureValue(MyHost::kRevisedClip,
                                 MyHost::kRevisedKey,
                                 MetadataFixture::kFirstFrame,
                                 published),
                     std::string("invalidation fixture clip=") + MyHost::kRevisedClip
                     + " key=" + MyHost::kRevisedKey))
      return;

    const int before = MyHost::gRevision;
    const int after = before + 1;
    const std::string expectedBefore = MyHost::revisedValue(published, before);
    const std::string expectedAfter = MyHost::revisedValue(published, after);

    std::string was = "none";
    std::string is = "none";

    const bool readBefore = readClipKey(*output, MyHost::kRevisedKey, MetadataFixture::kFirstFrame, was);

    MyHost::gRevision = after;
    input->invalidateMetadata();

    const bool readAfter = readClipKey(*output, MyHost::kRevisedKey, MetadataFixture::kFirstFrame, is);

    MyHost::gRevision = before;
    instance.invalidateMetadata();

    report.check(readBefore && was == expectedBefore,
                 "invalidation before value=" + was + " expected=" + expectedBefore);
    report.check(expectedBefore != expectedAfter,
                 "invalidation fixture before=" + expectedBefore + " after=" + expectedAfter);
    report.check(readAfter && is == expectedAfter,
                 "invalidation after value=" + is + " expected=" + expectedAfter);
  }

  /// render every frame of the fixture range through the plugin, the way a host that
  /// meant to produce output would, and capture anything logged through the message
  /// suite instead of letting it land between the PASS/FAIL lines above
  void checkRender(Report &report, OFX::Host::ImageEffect::Instance &instance)
  {
    report.check(instance.getClipPreferences(), "render clipprefs");

    MyHost::MyEffectInstance *effect = dynamic_cast<MyHost::MyEffectInstance *>(&instance);
    report.check(effect != NULL, "render effect instance");

    std::string captured;
    if(effect)
      effect->setMessageCapture(&captured);

    OfxPointD renderScale;
    renderScale.x = renderScale.y = 1.0;

    OfxRectI renderWindow;
    renderWindow.x1 = renderWindow.y1 = 0;
    renderWindow.x2 = renderWindow.y2 = 4;

    OfxRectD roi;
    roi.x1 = roi.y1 = 0;
    roi.x2 = roi.y2 = 4;

    const OfxTime first = MetadataFixture::kFirstFrame;
    const OfxTime last  = MetadataFixture::kLastFrame;

    OfxStatus stat = instance.beginRenderAction(first, last, 1.0, false, renderScale,
                                                /*sequential=*/true, /*interactive=*/false);
    report.check(stat == kOfxStatOK || stat == kOfxStatReplyDefault, "render beginsequence");

    int rendered = 0;

    for(OfxTime time = first; time <= last; time += 1) {
      if(gMessageSuite)
        gMessageSuite->message(instance.getHandle(), kOfxMessageLog, "metadataHost",
                               "metadataHost rendered frame %g", time);

      std::map<OFX::Host::ImageEffect::ClipInstance *, OfxRectD> rois;
      stat = instance.getRegionOfInterestAction(time, renderScale, roi, rois);
      report.check(stat == kOfxStatOK || stat == kOfxStatReplyDefault,
                   "render frame=" + formatTime(time) + " roi");

      stat = instance.renderAction(time, kOfxImageFieldBoth, renderWindow, renderScale,
                                   /*sequential=*/true, /*interactive=*/false, /*draft=*/false);
      report.check(stat == kOfxStatOK || stat == kOfxStatReplyDefault,
                   "render frame=" + formatTime(time));

      rendered += 1;
    }

    stat = instance.endRenderAction(first, last, 1.0, false, renderScale,
                                    /*sequential=*/true, /*interactive=*/false);
    report.check(stat == kOfxStatOK || stat == kOfxStatReplyDefault, "render endsequence");

    std::ostringstream framesWhere;
    framesWhere << "render framesrendered=" << rendered;
    report.check(rendered == int(last - first + 1), framesWhere.str());

    if(effect)
      effect->setMessageCapture(NULL);

    std::cout << "metadataHost captured messages begin" << std::endl;
    std::cout << captured;
    std::cout << "metadataHost captured messages end" << std::endl;

    report.check(captured.find("metadataHost rendered frame") != std::string::npos,
                 "render messagecapture");
  }

  /// find a plugin by id in an already scanned cache, reporting the one precondition
  /// both modes need before anything else can be checked
  OFX::Host::ImageEffect::ImageEffectPlugin *findPlugin(Report &report,
                                                        OFX::Host::ImageEffect::PluginCache &effectCache,
                                                        const std::string &pluginId,
                                                        const std::string &pluginDir)
  {
    OFX::Host::ImageEffect::ImageEffectPlugin *plugin = effectCache.getPluginById(pluginId);

    report.check(plugin != NULL, "plugin id=" + pluginId + " dir=" + pluginDir);

    return plugin;
  }

  /// create an instance of a plugin in the given context, reporting the preconditions
  /// shared by both modes: that the context can be instantiated at all, and that
  /// createInstance itself succeeds
  std::unique_ptr<OFX::Host::ImageEffect::Instance>
  createPluginInstance(Report &report,
                       OFX::Host::ImageEffect::ImageEffectPlugin *plugin,
                       const std::string &context)
  {
    std::unique_ptr<OFX::Host::ImageEffect::Instance> instance(plugin->createInstance(context, NULL));

    if(!report.check(instance.get() != NULL, "plugin instance context=" + context))
      return instance;

    const OfxStatus created = instance->createInstanceAction();
    report.check(created == kOfxStatOK || created == kOfxStatReplyDefault, "plugin createinstance");

    return instance;
  }

  /// the context a plugin driven by --plugin-id is instantiated in: the one Filter
  /// context read-only plugins are expected to declare, falling back to whatever the
  /// plugin does declare so the scratch composition plugin, which predates that
  /// convention and only declares General, can still be driven through this mode
  std::string chooseContext(OFX::Host::ImageEffect::ImageEffectPlugin &plugin)
  {
    const std::set<std::string> &contexts = plugin.getContexts();

    if(contexts.count(kOfxImageEffectContextFilter))
      return kOfxImageEffectContextFilter;
    if(contexts.count(kOfxImageEffectContextGeneral))
      return kOfxImageEffectContextGeneral;

    return contexts.empty() ? std::string() : *contexts.begin();
  }

  /// load an arbitrary plugin by id and drive it far enough to prove the contract any
  /// plugin has to meet, regardless of what it does: it resolves, describes, creates an
  /// instance exposing the clips its context guarantees, and completes a render pass.
  /// It asserts nothing about composition order or retained keys, which a read-only
  /// plugin implements neither of
  void checkGenericPlugin(Report &report,
                          MyHost::MetadataHost &host,
                          const std::string &pluginDir,
                          const std::string &pluginId)
  {
    BuildTreePluginCache cache(pluginDir);
    OFX::Host::ImageEffect::PluginCache effectCache(host);

    cache.setCacheVersion("metadataHostV1");
    effectCache.registerInCache(cache);
    cache.scanPluginFiles();

    OFX::Host::ImageEffect::ImageEffectPlugin *plugin = findPlugin(report, effectCache, pluginId, pluginDir);

    if(!plugin)
      return;

    const std::string context = chooseContext(*plugin);

    std::unique_ptr<OFX::Host::ImageEffect::Instance> instance = createPluginInstance(report, plugin, context);

    if(!instance.get())
      return;

    report.check(instance->getClip(kOfxImageEffectSimpleSourceClipName) != NULL,
                 "plugin clip=" kOfxImageEffectSimpleSourceClipName);
    report.check(instance->getClip(kOfxImageEffectOutputClipName) != NULL,
                 "plugin clip=" kOfxImageEffectOutputClipName);

    checkRender(report, *instance);
  }

  /// load the plugin, attach the fixture's clips to it and read its output clip in both
  /// composition orders
  void checkPlugin(Report &report, MyHost::MetadataHost &host, const std::string &pluginDir)
  {
    BuildTreePluginCache cache(pluginDir);
    OFX::Host::ImageEffect::PluginCache effectCache(host);

    cache.setCacheVersion("metadataHostV1");
    effectCache.registerInCache(cache);
    cache.scanPluginFiles();

    OFX::Host::ImageEffect::ImageEffectPlugin *plugin = findPlugin(report, effectCache, kPluginId, pluginDir);

    if(!plugin)
      return;

    std::unique_ptr<OFX::Host::ImageEffect::Instance>
      instance = createPluginInstance(report, plugin, kOfxImageEffectContextGeneral);

    if(!instance.get())
      return;

    OFX::Host::ImageEffect::ClipInstance *output = instance->getClip(kOfxImageEffectOutputClipName);
    MyHost::MyIntegerInstance *order =
      dynamic_cast<MyHost::MyIntegerInstance *>(instance->getParam(kOrderParam));

    if(!report.check(output != NULL, "plugin clip=" kOfxImageEffectOutputClipName))
      return;
    if(!report.check(order != NULL, std::string("plugin param=") + kOrderParam))
      return;

    std::vector<std::string> contested;
    contestedKeys(contested);
    report.check(!contested.empty(), "fixture contestedkeys=" + joinKeys(std::set<std::string>(contested.begin(), contested.end())));

    std::vector<std::string> dropped;
    droppedKeys(dropped);
    report.check(!dropped.empty(), "fixture droppedkeys=" + joinKeys(std::set<std::string>(dropped.begin(), dropped.end())));

    const int orders[] = {kMaskOverSource, kSourceOverMask};
    std::map<int, std::map<std::string, std::string> > atFirstFrame;

    for(size_t o = 0; o < sizeof(orders) / sizeof(orders[0]); ++o) {
      const int which = orders[o];

      std::ostringstream os;
      os << "effect order=" << which;
      const std::string where = os.str();

      report.check(order->set(which) == kOfxStatOK, where + " parameter set");

      // the metadata is derived from the effect's state, so the sets already cached
      // for the old value of the parameter have to go
      instance->invalidateMetadata();

      std::vector<std::string> perFrame;
      composedPerFrameKeys(which, perFrame);
      report.check(!perFrame.empty(),
                   where + " perframekeys=" + joinKeys(std::set<std::string>(perFrame.begin(), perFrame.end())));

      std::map<std::string, std::set<std::string> > seen;
      int frames = 0;

      for(OfxTime time = MetadataFixture::kFirstFrame; time <= MetadataFixture::kLastFrame; time += 1) {
        std::map<std::string, std::string> read;
        checkOutput(report, *output, which, time, read);

        if(time == MetadataFixture::kFirstFrame)
          atFirstFrame[which] = read;

        frames += 1;
        for(size_t k = 0; k < perFrame.size(); ++k)
          seen[perFrame[k]].insert(read[perFrame[k]]);
      }

      for(size_t k = 0; k < perFrame.size(); ++k) {
        std::ostringstream ps;
        ps << where << " key=" << perFrame[k] << " frames=" << frames
           << " distinct=" << seen[perFrame[k]].size();

        report.check(frames > 1 && int(seen[perFrame[k]].size()) == frames, ps.str());
      }

      for(size_t k = 0; k < dropped.size(); ++k) {
        report.check(atFirstFrame[which].find(dropped[k]) == atFirstFrame[which].end(),
                     where + " dropped=" + dropped[k]);
      }
    }

    // the same key composed the other way round must give the other clip's value
    for(size_t k = 0; k < contested.size(); ++k) {
      const std::string &key = contested[k];

      report.check(atFirstFrame[kMaskOverSource][key] != atFirstFrame[kSourceOverMask][key],
                   "effect key=" + key
                   + " maskoversource=" + atFirstFrame[kMaskOverSource][key]
                   + " sourceovermask=" + atFirstFrame[kSourceOverMask][key]);
    }

    checkInvalidation(report, *instance);

    checkRender(report, *instance);
  }

  int runChecks(const std::string &pluginDir, const std::string &pluginId)
  {
    MyHost::MetadataHost host;
    OfxHost *handle = host.getHandle();

    gPropSuite = (const OfxPropertySuiteV2 *) handle->fetchSuite(handle->host, kOfxPropertySuite, 2);
    gMetadataSuite = (const OfxMetadataSuiteV1 *) handle->fetchSuite(handle->host, kOfxMetadataSuite, 1);
    gEffectSuite = (const OfxImageEffectSuiteV1 *) handle->fetchSuite(handle->host, kOfxImageEffectSuite, 1);
    gMessageSuite = (const OfxMessageSuiteV2 *) handle->fetchSuite(handle->host, kOfxMessageSuite, 2);

    if(!gPropSuite || !gMetadataSuite || !gEffectSuite || !gMessageSuite) {
      std::cout << "metadataHost the host does not vend the suites this needs" << std::endl;
      std::cout << "RESULT FAIL" << std::endl;
      return 1;
    }

    Report report;

    if(pluginId.empty()) {
      checkFixture(report);
      checkClips(report);
      checkPlugin(report, host, pluginDir);
    }
    else {
      checkGenericPlugin(report, host, pluginDir, pluginId);
    }

    std::cout << "metadataHost checks=" << report.getChecks()
              << " failures=" << report.getFailures() << std::endl;
    std::cout << "RESULT " << (report.getFailures() ? "FAIL" : "PASS") << std::endl;

    return report.getFailures() ? 1 : 0;
  }

  void usage(std::ostream &os)
  {
    os << "usage: metadataHost [--list] [--plugin-dir <path>] [--plugin-id <id>]" << std::endl;
    os << "  --list              print the fixture table and exit" << std::endl;
    os << "  --plugin-dir <path> look for the plugin bundle in <path> rather than in"
       << std::endl;
    os << "                      " << METADATA_PLUGIN_DIR << std::endl;
    os << "  --plugin-id <id>    load <id> from --plugin-dir and check the general"
       << std::endl;
    os << "                      preconditions any plugin has to meet - describe,"
       << std::endl;
    os << "                      create an instance, expose its clips, render the"
       << std::endl;
    os << "                      fixture range - rather than the scratch composition"
       << std::endl;
    os << "                      plugin's own composition order and retained-key checks"
       << std::endl;
    os << "  with no arguments, publish the fixture through a host, read it back" << std::endl;
    os << "  through the metadata suite, then run it through the metadata plugin and" << std::endl;
    os << "  check what comes back" << std::endl;
  }

} // anonymous

int main(int argc, char **argv)
{
  bool list = false;
  std::string pluginDir(METADATA_PLUGIN_DIR);
  std::string pluginId;

  for(int i = 1; i < argc; ++i) {
    const std::string arg(argv[i]);

    if(arg == "--list") {
      list = true;
    }
    else if(arg == "--plugin-dir") {
      if(i + 1 >= argc) {
        std::cerr << "metadataHost --plugin-dir needs a path" << std::endl;
        usage(std::cerr);
        return 2;
      }
      pluginDir = argv[++i];
    }
    else if(arg == "--plugin-id") {
      if(i + 1 >= argc) {
        std::cerr << "metadataHost --plugin-id needs an id" << std::endl;
        usage(std::cerr);
        return 2;
      }
      pluginId = argv[++i];
    }
    else if(arg == "--help" || arg == "-h") {
      usage(std::cout);
      return 0;
    }
    else {
      std::cerr << "metadataHost unknown argument " << arg << std::endl;
      usage(std::cerr);
      return 2;
    }
  }

  if(list) {
    listFixture();
    return 0;
  }

  return runChecks(pluginDir, pluginId);
}
