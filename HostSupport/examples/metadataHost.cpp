// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#include <algorithm>
#include <cctype>
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

#ifdef OFX_SUPPORTS_METADATA

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

#else

  typedef Host MetadataHost;

#endif // OFX_SUPPORTS_METADATA

} // MyHost

namespace {

  const OfxPropertySuiteV2    *gPropSuite = NULL;
  const OfxMetadataSuiteV1    *gMetadataSuite = NULL;
  const OfxImageEffectSuiteV1 *gEffectSuite = NULL;
  const OfxMessageSuiteV2     *gMessageSuite = NULL;

  /// the metadata suite is vended only by a host built with OFX_SUPPORTS_METADATA
#ifdef OFX_SUPPORTS_METADATA
  const bool kMetadataSuiteExpected = true;
#else
  const bool kMetadataSuiteExpected = false;
#endif // OFX_SUPPORTS_METADATA

  ////////////////////////////////////////////////////////////////////////////////
  // formatting, shared by the fixture listing and the values read back so that the
  // two are compared as they are printed

  std::string formatDouble(double v)
  {
    std::ostringstream os;
    os << std::setprecision(17) << v;
    return os.str();
  }

  std::string formatInt(int v)
  {
    std::ostringstream os;
    os << v;
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

    /// the check count as it stands, to be handed back to ranAtLeast()
    int mark() const {return _checks;}

    /// check that at least least checks have been made since mark() gave since, so that
    /// a run which returned early is reported as a failure rather than as a clean run
    /// which happened to assert nothing
    void ranAtLeast(int since, int least, const std::string &what)
    {
      std::ostringstream os;
      os << what << " ran=" << (_checks - since) << " least=" << least;

      check(_checks - since >= least, os.str());
    }

    int getChecks() const {return _checks;}
    int getFailures() const {return _failures;}
  };

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

#ifdef OFX_SUPPORTS_METADATA

  OfxStatus collectKey(const char *key, void *userData)
  {
    ((std::set<std::string> *) userData)->insert(key);
    return kOfxStatOK;
  }

  /// read a key back the way a plugin has to, by asking the host what type and dimension
  /// it has rather than by knowing in advance
  bool readValueN(OfxPropertySetHandle metadata,
                  const char *key,
                  std::string &type,
                  int &dimension,
                  std::string &value)
  {
    OfxPropDataType dataType = kOfxPropDataTypeNone;

    dimension = 0;
    value.clear();

    if(gPropSuite->propGetType(metadata, key, &dataType) != kOfxStatOK)
      return false;

    if(gPropSuite->propGetDimension(metadata, key, &dimension) != kOfxStatOK || dimension < 1)
      return false;

    switch(dataType) {
    case kOfxPropDataTypeString : {
      std::vector<char *> v(dimension, (char *) NULL);
      if(gPropSuite->propGetStringN(metadata, key, dimension, &v[0]) != kOfxStatOK)
        return false;
      type = "string";
      for(int i = 0; i < dimension; ++i) {
        if(!v[i])
          return false;
        value += (i ? "," : "");
        value += v[i];
      }
      return true;
    }

    case kOfxPropDataTypeDouble : {
      std::vector<double> v(dimension, 0.0);
      if(gPropSuite->propGetDoubleN(metadata, key, dimension, &v[0]) != kOfxStatOK)
        return false;
      type = "double";
      for(int i = 0; i < dimension; ++i) {
        value += (i ? "," : "");
        value += formatDouble(v[i]);
      }
      return true;
    }

    case kOfxPropDataTypeInteger : {
      std::vector<int> v(dimension, 0);
      if(gPropSuite->propGetIntN(metadata, key, dimension, &v[0]) != kOfxStatOK)
        return false;
      type = "int";
      value = formatInts(&v[0], dimension);
      return true;
    }

    default :
      return false;
    }
  }

  /// read a key the fixture describes, which is a single value unless it is an array of
  /// at most kMaxInts ints
  bool readValue(OfxPropertySetHandle metadata, const char *key, std::string &type, std::string &value)
  {
    int dimension = 0;

    if(!readValueN(metadata, key, type, dimension, value))
      return false;

    return type == "int" ? dimension <= MetadataFixture::kMaxInts : dimension == 1;
  }

#endif // OFX_SUPPORTS_METADATA

  ////////////////////////////////////////////////////////////////////////////////
  // what a plugin logs, and the pixels it renders

  bool parseInt(const std::string &text, int &value)
  {
    std::istringstream is(text);
    is >> value;
    return !is.fail() && is.eof();
  }

  bool parseDouble(const std::string &text, double &value)
  {
    std::istringstream is(text);
    is >> value;
    return !is.fail() && is.eof();
  }

  bool parseInts(const std::string &text, std::vector<int> &values)
  {
    std::istringstream is(text);
    std::string field;

    while(std::getline(is, field, ',')) {
      int value = 0;
      if(!parseInt(field, value))
        return false;
      values.push_back(value);
    }

    return !values.empty();
  }

  /// one metadata key as a plugin logs it, in the grammar
  ///   clip=<name> frame=<n> key=<key> type=<type> value=<value>
  struct LogRecord {
    std::string clip;
    OfxTime     time;
    std::string key;
    std::string type;
    std::string value;

    LogRecord() : time(MetadataFixture::kAnyTime) {}
  };

  /// pull the log records out of captured message text. Tokens which are none of the
  /// grammar's are skipped, so the type and id vmessage prepends need not be accounted
  /// for, and a line carrying no key= is not a record at all, so a plugin may log a
  /// header. value= is last and everything after it is the value, so a value may hold
  /// spaces
  void parseLogRecords(const std::string &text, std::vector<LogRecord> &records)
  {
    std::istringstream lines(text);
    std::string line;

    while(std::getline(lines, line)) {
      std::string head = line;
      std::string value;

      const std::string::size_type valueAt = line.find("value=");

      if(valueAt != std::string::npos) {
        head = line.substr(0, valueAt);
        value = line.substr(valueAt + strlen("value="));
      }

      if(head.find("key=") == std::string::npos)
        continue;

      LogRecord record;
      record.value = value;

      std::istringstream tokens(head);
      std::string token;

      while(tokens >> token) {
        if(token.compare(0, 5, "clip=") == 0)
          record.clip = token.substr(5);
        else if(token.compare(0, 6, "frame=") == 0)
          parseDouble(token.substr(6), record.time);
        else if(token.compare(0, 4, "key=") == 0)
          record.key = token.substr(4);
        else if(token.compare(0, 5, "type=") == 0)
          record.type = token.substr(5);
      }

      records.push_back(record);
    }
  }

  /// strings have to be logged literally, but a double or an int is only required to
  /// parse back to what the fixture holds, so that a plugin is not held to the
  /// formatting formatDouble happens to use
  bool logValueMatches(const MetadataFixture::Entry &entry, const std::string &value)
  {
    switch(entry.type) {
    case MetadataFixture::eString :
      return value == entry.stringValue;

    case MetadataFixture::eDouble : {
      double parsed = 0;
      return parseDouble(value, parsed) && parsed == entry.doubleValue;
    }

    case MetadataFixture::eInt : {
      std::vector<int> parsed;

      if(!parseInts(value, parsed) || int(parsed.size()) != entry.intCount)
        return false;

      for(int i = 0; i < entry.intCount; ++i) {
        if(parsed[i] != entry.intValues[i])
          return false;
      }

      return true;
    }
    }

    return false;
  }

  std::string recordId(const std::string &clip, OfxTime time, const std::string &key)
  {
    return clip + " " + formatTime(time) + " " + key;
  }

  /// why a log is not exactly what the fixture gives for the clips it names, over the
  /// whole fixture range, once each and in ascending key order within a clip and frame.
  /// The empty string means it is
  std::string logMismatch(const std::vector<LogRecord> &records)
  {
    if(records.empty())
      return "norecords";

    std::set<std::string> clips;
    for(size_t r = 0; r < records.size(); ++r)
      clips.insert(records[r].clip);

    std::set<std::string> expected;

    for(std::set<std::string>::const_iterator it = clips.begin(); it != clips.end(); ++it) {
      for(OfxTime time = MetadataFixture::kFirstFrame; time <= MetadataFixture::kLastFrame; time += 1) {
        for(int i = 0; i < MetadataFixture::kEntryCount; ++i) {
          if(entryAppliesAt(MetadataFixture::kEntries[i], *it, time))
            expected.insert(recordId(*it, time, MetadataFixture::kEntries[i].key));
        }
      }
    }

    std::set<std::string> seen;

    for(size_t r = 0; r < records.size(); ++r) {
      const LogRecord &record = records[r];
      const std::string id = recordId(record.clip, record.time, record.key);
      const MetadataFixture::Entry *entry = NULL;

      for(int i = 0; i < MetadataFixture::kEntryCount && !entry; ++i) {
        if(record.key == MetadataFixture::kEntries[i].key
           && entryAppliesAt(MetadataFixture::kEntries[i], record.clip, record.time))
          entry = &MetadataFixture::kEntries[i];
      }

      if(!entry)
        return "notinfixture " + id;

      if(record.type != typeName(entry->type))
        return "type " + id + " logged=" + record.type + " fixture=" + typeName(entry->type);

      if(!logValueMatches(*entry, record.value))
        return "value " + id + " logged=" + record.value + " fixture=" + entryValue(*entry);

      if(!seen.insert(id).second)
        return "repeated " + id;
    }

    for(std::set<std::string>::const_iterator it = expected.begin(); it != expected.end(); ++it) {
      if(!seen.count(*it))
        return "missing " + *it;
    }

    for(size_t r = 1; r < records.size(); ++r) {
      if(records[r].clip == records[r - 1].clip
         && records[r].time == records[r - 1].time
         && records[r].key <= records[r - 1].key)
        return "unsorted " + recordId(records[r].clip, records[r].time, records[r].key);
    }

    return "";
  }

  /// check what a plugin logged against what the fixture gives for the clips it named
  void checkLogAgainstFixture(Report &report,
                              const std::vector<LogRecord> &records,
                              const std::string &where)
  {
    const std::string why = logMismatch(records);

    std::ostringstream os;
    os << where << " logrecords=" << records.size();

    report.check(why.empty(), why.empty() ? os.str() : os.str() + " " + why);
  }

  /// the side of the window rendered through, and of the window pixels are compared
  /// over, which has to be wide enough to hold more than one image's worth of detail
  const int kRenderWindowSize = 64;

  /// true if two images carry the same pixels over the window, which has to hold at
  /// least one pixel of both of them
  bool imagesEqual(const MyHost::MyImage &a, const MyHost::MyImage &b, const OfxRectI &window)
  {
    if(window.x2 <= window.x1 || window.y2 <= window.y1)
      return false;

    for(int y = window.y1; y < window.y2; ++y) {
      for(int x = window.x1; x < window.x2; ++x) {
        const OfxRGBAColourB *pa = a.pixel(x, y);
        const OfxRGBAColourB *pb = b.pixel(x, y);

        if(!pa || !pb)
          return false;

        if(pa->r != pb->r || pa->g != pb->g || pa->b != pb->b || pa->a != pb->a)
          return false;
      }
    }

    return true;
  }

#ifdef OFX_SUPPORTS_METADATA

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

  /// what a plugin would log for an entry. A double is written the way printf's %f
  /// writes it rather than the way formatDouble does, so that the record set below is
  /// only accepted if doubles are compared by what they parse to
  std::string loggedValue(const MetadataFixture::Entry &entry)
  {
    if(entry.type != MetadataFixture::eDouble)
      return entryValue(entry);

    std::ostringstream os;
    os << std::fixed << std::setprecision(6) << entry.doubleValue;
    return os.str();
  }

  /// the whole fixture written out the way a plugin reading it would log it, behind a
  /// header line and the type and id vmessage prepends
  std::string fixtureLog()
  {
    std::ostringstream os;

    os << "log fixture reading the fixture" << std::endl;

    for(int c = 0; c < MetadataFixture::kInputClipCount; ++c) {
      const std::string clip = MetadataFixture::kInputClips[c];

      for(OfxTime time = MetadataFixture::kFirstFrame; time <= MetadataFixture::kLastFrame; time += 1) {
        std::map<std::string, const MetadataFixture::Entry *> keys;

        for(int i = 0; i < MetadataFixture::kEntryCount; ++i) {
          if(entryAppliesAt(MetadataFixture::kEntries[i], clip, time))
            keys[MetadataFixture::kEntries[i].key] = &MetadataFixture::kEntries[i];
        }

        for(std::map<std::string, const MetadataFixture::Entry *>::const_iterator it = keys.begin();
            it != keys.end(); ++it) {
          os << "log fixture clip=" << clip
             << " frame=" << formatTime(time)
             << " key=" << it->first
             << " type=" << typeName(it->second->type)
             << " value=" << loggedValue(*it->second) << std::endl;
        }
      }
    }

    return os.str();
  }

  /// the comparators the plugin checks rest on are worth nothing unless they reject what
  /// they are meant to, so build a record set the fixture itself gives, check it is
  /// accepted, then mutate it one way at a time and check each mutation is caught
  void checkComparators(Report &report)
  {
    std::vector<LogRecord> records;
    parseLogRecords(fixtureLog(), records);

    std::ostringstream parsed;
    parsed << "selfcheck logparsed=" << records.size();

    if(!report.check(records.size() > 1, parsed.str()))
      return;

    report.check(logMismatch(records).empty(), "selfcheck log accepted");

    std::vector<LogRecord> dropped(records);
    dropped.erase(dropped.begin());
    report.check(!logMismatch(dropped).empty(), "selfcheck log droppedrecord");

    std::vector<LogRecord> wrongValue(records);
    wrongValue[0].value += "-wrong";
    report.check(!logMismatch(wrongValue).empty(), "selfcheck log wrongvalue");

    std::vector<LogRecord> wrongType(records);
    wrongType[0].type = wrongType[0].type == "int" ? "string" : "int";
    report.check(!logMismatch(wrongType).empty(), "selfcheck log wrongtype");

    std::string atFirst = "none";
    std::string atLast = "none";
    const bool advances =
      fixtureValue(MyHost::kRevisedClip, kOfxMetadataKeyTimecode, MetadataFixture::kFirstFrame, atFirst)
      && fixtureValue(MyHost::kRevisedClip, kOfxMetadataKeyTimecode, MetadataFixture::kLastFrame, atLast)
      && atFirst != atLast;

    if(report.check(advances, "selfcheck fixture timecode first=" + atFirst + " last=" + atLast)) {
      std::vector<LogRecord> repeated(records);

      for(size_t r = 0; r < repeated.size(); ++r) {
        if(repeated[r].key == kOfxMetadataKeyTimecode && repeated[r].time == MetadataFixture::kLastFrame)
          repeated[r].value = atFirst;
      }

      report.check(!logMismatch(repeated).empty(), "selfcheck log repeatedtimecode");
    }

    size_t swapAt = 0;

    for(size_t r = 1; r < records.size() && !swapAt; ++r) {
      if(records[r].clip == records[r - 1].clip && records[r].time == records[r - 1].time)
        swapAt = r;
    }

    if(report.check(swapAt != 0, "selfcheck log sortable")) {
      std::vector<LogRecord> unsorted(records);
      std::swap(unsorted[swapAt], unsorted[swapAt - 1]);
      report.check(!logMismatch(unsorted).empty(), "selfcheck log unsortedkeys");
    }

    OFX::Host::ImageEffect::ClipDescriptor descriptor(MetadataFixture::kInputClips[0]);
    MyHost::MetadataClipInstance clip(&descriptor);

    MyHost::MyImage first(clip, MetadataFixture::kFirstFrame);
    MyHost::MyImage last(clip, MetadataFixture::kLastFrame);

    OfxRectI window;
    window.x1 = window.y1 = 0;
    window.x2 = window.y2 = kRenderWindowSize;

    report.check(imagesEqual(first, first, window), "selfcheck image sameframe");
    report.check(!imagesEqual(first, last, window),
                 "selfcheck image frames=" + formatTime(MetadataFixture::kFirstFrame)
                 + "," + formatTime(MetadataFixture::kLastFrame));
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
  // the write path

  /// the keys this writes, in a namespace of the harness's own since the standard
  /// prefix is reserved for the standard vocabulary
  const char kWrittenString[]  = "org.openfx.metadataHost/string";
  const char kWrittenDouble[]  = "org.openfx.metadataHost/double";
  const char kWrittenInt[]     = "org.openfx.metadataHost/int";
  const char kWrittenStringN[] = "org.openfx.metadataHost/stringn";
  const char kWrittenDoubleN[] = "org.openfx.metadataHost/doublen";
  const char kWrittenIntN[]    = "org.openfx.metadataHost/intn";
  const char kWrittenNever[]   = "org.openfx.metadataHost/never";

  /// the values the writes in here write
  const char *const kWriteStrings[] = {"one", "two", "three"};
  const double      kWriteDoubles[] = {1.25, 2.5};
  const int         kWriteInts[]    = {11, 22, 33, 44};

  /// the six metadataSet entry points, in the order callSetter takes them
  const char *const kSetterNames[] = {"string", "double", "int", "stringn", "doublen", "intn"};
  const int kSetterCount = int(sizeof(kSetterNames) / sizeof(kSetterNames[0]));

  /// call one metadataSet entry point with arguments which are valid in themselves, so
  /// that what the call reports is down to the handle and the key it is given
  OfxStatus callSetter(int which, OfxPropertySetHandle metadata, const char *key)
  {
    switch(which) {
    case 0 : return gMetadataSuite->metadataSetString(metadata, key, kWriteStrings[0]);
    case 1 : return gMetadataSuite->metadataSetDouble(metadata, key, kWriteDoubles[0]);
    case 2 : return gMetadataSuite->metadataSetInt(metadata, key, kWriteInts[0]);
    case 3 : return gMetadataSuite->metadataSetStringN(metadata, key, 3, kWriteStrings);
    case 4 : return gMetadataSuite->metadataSetDoubleN(metadata, key, 2, kWriteDoubles);
    case 5 : return gMetadataSuite->metadataSetIntN(metadata, key, 4, kWriteInts);
    }

    return kOfxStatFailed;
  }

  /// the status every one of the six gives for a handle and a key, one check each
  void checkSetters(Report &report,
                    OfxPropertySetHandle metadata,
                    const char *key,
                    OfxStatus expected,
                    const std::string &where)
  {
    for(int i = 0; i < kSetterCount; ++i) {
      const OfxStatus st = callSetter(i, metadata, key);

      report.check(st == expected, where + " set=" + kSetterNames[i]
                   + " status=" + formatInt(st) + " expected=" + formatInt(expected));
    }
  }

  /// what one write reported, and what the key holds once it has been made
  void checkWritten(Report &report,
                    OfxPropertySetHandle metadata,
                    const char *key,
                    OfxStatus st,
                    const std::string &type,
                    int dimension,
                    const std::string &expected)
  {
    const std::string where = std::string("write key=") + key;

    if(!report.check(st == kOfxStatOK, where + " written status=" + formatInt(st)))
      return;

    std::string readType;
    std::string value;
    int readDimension = 0;
    const bool ok = readValueN(metadata, key, readType, readDimension, value);

    std::ostringstream os;
    os << where << " type=" << readType << " dim=" << readDimension << " value=" << value
       << " expected " << type << " dim=" << dimension << " value=" << expected;

    report.check(ok && readType == type && readDimension == dimension && value == expected, os.str());
  }

  /// drive the six metadataSet entry points and the statuses they owe against sets the
  /// harness makes itself, since a plugin sees a writable set only inside the get
  /// metadata action and would have to misbehave to reach the cases which must fail
  void checkMetadataWrites(Report &report)
  {
    OFX::Host::ImageEffect::MetadataSet *set = new OFX::Host::ImageEffect::MetadataSet(true, false);
    OfxPropertySetHandle writable = set->getPropHandle();

    checkWritten(report, writable, kWrittenString,
                 gMetadataSuite->metadataSetString(writable, kWrittenString, kWriteStrings[0]),
                 "string", 1, "one");

    checkWritten(report, writable, kWrittenDouble,
                 gMetadataSuite->metadataSetDouble(writable, kWrittenDouble, kWriteDoubles[0]),
                 "double", 1, "1.25");

    checkWritten(report, writable, kWrittenInt,
                 gMetadataSuite->metadataSetInt(writable, kWrittenInt, kWriteInts[0]),
                 "int", 1, "11");

    checkWritten(report, writable, kWrittenStringN,
                 gMetadataSuite->metadataSetStringN(writable, kWrittenStringN, 3, kWriteStrings),
                 "string", 3, "one,two,three");

    checkWritten(report, writable, kWrittenDoubleN,
                 gMetadataSuite->metadataSetDoubleN(writable, kWrittenDoubleN, 2, kWriteDoubles),
                 "double", 2, "1.25,2.5");

    checkWritten(report, writable, kWrittenIntN,
                 gMetadataSuite->metadataSetIntN(writable, kWrittenIntN, 4, kWriteInts),
                 "int", 4, "11,22,33,44");

    // a key written again takes the dimension it is written with, whatever it had before
    checkWritten(report, writable, kWrittenIntN,
                 gMetadataSuite->metadataSetIntN(writable, kWrittenIntN, 2, kWriteInts),
                 "int", 2, "11,22");

    // a set a clip vends is read only, and refuses every one of them
    OFX::Host::ImageEffect::ClipDescriptor descriptor(MetadataFixture::kInputClips[0]);
    MyHost::MetadataClipInstance clip(&descriptor);
    OfxPropertySetHandle readOnly = NULL;
    const OfxStatus fetched = gMetadataSuite->clipGetMetadata(clip.getHandle(),
                                                              MetadataFixture::kFirstFrame,
                                                              &readOnly);

    if(report.check(fetched == kOfxStatOK && readOnly, "write readonly fetched")) {
      checkSetters(report, readOnly, kWrittenString, kOfxStatErrValue, "write readonly");
      gMetadataSuite->metadataRelease(readOnly);
    }

    // a property set which is not a metadata set at all is a bad handle, as is no key
    OFX::Host::Property::Set plain;

    checkSetters(report, plain.getHandle(), kWrittenString, kOfxStatErrBadHandle, "write plainset");
    checkSetters(report, writable, NULL, kOfxStatErrBadHandle, "write nullkey");
    checkSetters(report, writable, "", kOfxStatErrValue, "write emptykey");

    report.check(gMetadataSuite->metadataSetStringN(writable, kWrittenStringN, 0, kWriteStrings) == kOfxStatErrValue,
                 "write nocount set=stringn");
    report.check(gMetadataSuite->metadataSetDoubleN(writable, kWrittenDoubleN, 0, kWriteDoubles) == kOfxStatErrValue,
                 "write nocount set=doublen");
    report.check(gMetadataSuite->metadataSetIntN(writable, kWrittenIntN, 0, kWriteInts) == kOfxStatErrValue,
                 "write nocount set=intn");

    report.check(gMetadataSuite->metadataSetStringN(writable, kWrittenStringN, 3, NULL) == kOfxStatErrValue,
                 "write novalues set=stringn");
    report.check(gMetadataSuite->metadataSetDoubleN(writable, kWrittenDoubleN, 2, NULL) == kOfxStatErrValue,
                 "write novalues set=doublen");
    report.check(gMetadataSuite->metadataSetIntN(writable, kWrittenIntN, 4, NULL) == kOfxStatErrValue,
                 "write novalues set=intn");

    // a NULL string is refused rather than dereferenced, and a refused write leaves the
    // key as it was, whether it was there or not
    const char *const nulled[] = {"one", NULL, "three"};

    report.check(gMetadataSuite->metadataSetString(writable, kWrittenNever, NULL) == kOfxStatErrValue,
                 "write nullstring set=string");
    report.check(gMetadataSuite->metadataSetStringN(writable, kWrittenNever, 3, nulled) == kOfxStatErrValue,
                 "write nullstring set=stringn");

    std::string neverType;
    std::string neverValue;
    int neverDimension = 0;

    report.check(!readValueN(writable, kWrittenNever, neverType, neverDimension, neverValue),
                 std::string("write nullstring absent key=") + kWrittenNever);

    report.check(gMetadataSuite->metadataSetStringN(writable, kWrittenStringN, 3, nulled) == kOfxStatErrValue,
                 "write nullstring set=stringn existing");

    std::string keptType;
    std::string keptValue;
    int keptDimension = 0;
    const bool kept = readValueN(writable, kWrittenStringN, keptType, keptDimension, keptValue);

    report.check(kept && keptType == "string" && keptDimension == 3 && keptValue == "one,two,three",
                 std::string("write nullstring unchanged key=") + kWrittenStringN
                 + " type=" + keptType + " value=" + keptValue);

    // the host owns this one, so a plugin releasing it is refused and the set it is
    // about to read is still there
    report.check(gMetadataSuite->metadataRelease(writable) == kOfxStatErrValue, "write release refused");

    std::string type;
    std::string value;
    int dimension = 0;
    const bool readable = readValueN(writable, kWrittenString, type, dimension, value);

    report.check(readable && type == "string" && dimension == 1 && value == "one",
                 std::string("write release readable key=") + kWrittenString
                 + " type=" + type + " value=" + value);

    set->releaseReference();
  }

  ////////////////////////////////////////////////////////////////////////////////
  // the checks that need the plugin

  const char kPluginId[] = "org.openfx.examples.metadataPlugin";

  /// the plugin's composition order parameter, and the two values of it this checks
  const char kOrderParam[] = "compositionOrder";
  const int  kMaskOverSource = 0;
  const int  kSourceOverMask = 1;

  /// the value of that parameter which makes the plugin write into everything the action
  /// can write into and then report the action untrapped
  const int  kUntrappedOrder = 2;

  /// the value which makes it nominate no source clip at all, so that there is nothing to
  /// inherit and the output carries only what it contributed
  const int  kNoSourceOrder = 3;

  /// the plugin's string and choice parameters, the defaults it declares for them and
  /// the values this writes through them
  const char kNoteParam[]   = "note";
  const char kNoteDefault[] = "unset";
  const char kNoteScalar[]  = "scalar";
  const char kNoteAtTime[]  = "attime";
  const char kDetailParam[] = "detail";
  const int  kDetailDefault = 0;
  const int  kDetailScalar  = 2;
  const int  kDetailAtTime  = 1;

  /// the keys the plugin writes under its own reverse DNS prefix, and the values it
  /// writes into them, the string one being whatever the note parameter holds
  const char kContributedNote[]   = "org.openfx.examples.metadataPlugin.note";
  const char kContributedGain[]   = "org.openfx.examples.metadataPlugin.gain";
  const char kContributedPasses[] = "org.openfx.examples.metadataPlugin.passes";
  const char kContributedWindow[] = "org.openfx.examples.metadataPlugin.window";

  const double kContributedGainValue     = 1.75;
  const int    kContributedPassesValue   = 5;
  const int    kContributedWindowValue[] = {12, 24, 1908, 1056};
  const int    kContributedWindowCount   =
    int(sizeof(kContributedWindowValue) / sizeof(kContributedWindowValue[0]));

  /// the value it writes into the one key it also retains from Source, and into the one
  /// named after the property the composition order is nominated in
  const double kContributedFrameRate    = 48.0;
  const char   kContributedSourceClip[] = "contributed";

  /// a key the plugin writes into the set the host hands it, and what it has to read
  /// back as on the output clip once the host has put it over what was inherited
  struct Contributed {
    std::string key;
    std::string type;
    int         dimension;
    std::string value;
  };

  void addContributed(std::vector<Contributed> &contributed,
                      const std::string &key,
                      const std::string &type,
                      int dimension,
                      const std::string &value)
  {
    Contributed one;

    one.key = key;
    one.type = type;
    one.dimension = dimension;
    one.value = value;

    contributed.push_back(one);
  }

  /// everything the plugin contributes when the note parameter holds the given value,
  /// composed the way the fixture's own entries are rather than written out as literals
  void contributedKeys(const std::string &note, std::vector<Contributed> &contributed)
  {
    addContributed(contributed, kContributedNote, "string", 1, note);
    addContributed(contributed, kContributedGain, "double", 1, formatDouble(kContributedGainValue));
    addContributed(contributed, kContributedPasses, "int", 1, formatInt(kContributedPassesValue));
    addContributed(contributed, kContributedWindow, "int", kContributedWindowCount,
                   formatInts(kContributedWindowValue, kContributedWindowCount));
    addContributed(contributed, kOfxMetadataKeyFrameRate, "double", 1, formatDouble(kContributedFrameRate));
    addContributed(contributed, kOfxImageEffectPropMetadataSourceClip, "string", 1, kContributedSourceClip);
  }

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
  /// leave on the effect's output clip. In the order the plugin does not trap the action
  /// in, what the host offered it stands instead
  void expectedOutput(int order,
                      OfxTime time,
                      const std::string &note,
                      std::map<std::string, std::string> &values,
                      std::map<std::string, std::string> &types)
  {
    if(order == kUntrappedOrder) {
      for(int i = 0; i < MetadataFixture::kEntryCount; ++i) {
        const MetadataFixture::Entry &entry = MetadataFixture::kEntries[i];

        if(!entryAppliesAt(entry, MetadataFixture::kInputClips[0], time))
          continue;

        values[entry.key] = entryValue(entry);
        types[entry.key] = typeName(entry.type);
      }

      return;
    }

    if(order != kNoSourceOrder) {
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

    std::vector<Contributed> contributed;
    contributedKeys(note, contributed);

    for(size_t c = 0; c < contributed.size(); ++c) {
      values[contributed[c].key] = contributed[c].value;
      types[contributed[c].key] = contributed[c].type;
    }
  }

  /// read the effect's output clip at one frame and check it against what composing the
  /// fixture the way the plugin nominates should give
  void checkOutput(Report &report,
                   OFX::Host::ImageEffect::ClipInstance &output,
                   int order,
                   OfxTime time,
                   const std::string &note,
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
    expectedOutput(order, time, note, values, types);

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

    // in the order the plugin does not trap the action in there is nothing of what it
    // contributed to read back, and the key set checked above is what says so
    if(order != kUntrappedOrder) {
      std::vector<Contributed> contributed;
      contributedKeys(note, contributed);

      for(size_t c = 0; c < contributed.size(); ++c) {
        const Contributed &one = contributed[c];

        std::string type = "none";
        std::string value = "none";
        int dimension = 0;

        const bool ok = readValueN(metadata, one.key.c_str(), type, dimension, value)
                        && type == one.type
                        && dimension == one.dimension
                        && value == one.value;

        report.check(ok, where + " contributed=" + one.key + " type=" + type
                     + " dimension=" + formatInt(dimension) + " value=" + value);
      }
    }

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
  void composedPerFrameKeys(int order, const std::string &note, std::vector<std::string> &keys)
  {
    std::map<std::string, std::set<std::string> > seen;
    int frames = 0;

    for(OfxTime time = MetadataFixture::kFirstFrame; time <= MetadataFixture::kLastFrame; time += 1) {
      std::map<std::string, std::string> values;
      std::map<std::string, std::string> types;
      expectedOutput(order, time, note, values, types);

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

#endif // OFX_SUPPORTS_METADATA

  /// write text through whichever of the host's parameter instances a name resolves to,
  /// so that a check can drive a parameter it knows only by name and value
  bool setParamValue(OFX::Host::ImageEffect::Instance &instance,
                     const std::string &name,
                     const std::string &value)
  {
    OFX::Host::Param::Instance *param = instance.getParam(name);

    if(MyHost::MyStringInstance *text = dynamic_cast<MyHost::MyStringInstance *>(param))
      return text->set(value.c_str()) == kOfxStatOK;

    int number = 0;

    if(!parseInt(value, number))
      return false;

    if(MyHost::MyChoiceInstance *choice = dynamic_cast<MyHost::MyChoiceInstance *>(param))
      return choice->set(number) == kOfxStatOK;
    if(MyHost::MyIntegerInstance *integer = dynamic_cast<MyHost::MyIntegerInstance *>(param))
      return integer->set(number) == kOfxStatOK;

    return false;
  }

  /// the same, at a time rather than as the parameter's scalar value
  bool setParamValue(OFX::Host::ImageEffect::Instance &instance,
                     const std::string &name,
                     OfxTime time,
                     const std::string &value)
  {
    OFX::Host::Param::Instance *param = instance.getParam(name);

    if(MyHost::MyStringInstance *text = dynamic_cast<MyHost::MyStringInstance *>(param))
      return text->set(time, value.c_str()) == kOfxStatOK;

    int number = 0;

    if(!parseInt(value, number))
      return false;

    if(MyHost::MyChoiceInstance *choice = dynamic_cast<MyHost::MyChoiceInstance *>(param))
      return choice->set(time, number) == kOfxStatOK;
    if(MyHost::MyIntegerInstance *integer = dynamic_cast<MyHost::MyIntegerInstance *>(param))
      return integer->set(time, number) == kOfxStatOK;

    return false;
  }

  /// read a parameter back as text, false if the instance holds no such parameter or it
  /// is of a type this cannot drive
  bool getParamValue(OFX::Host::ImageEffect::Instance &instance,
                     const std::string &name,
                     std::string &value)
  {
    OFX::Host::Param::Instance *param = instance.getParam(name);

    if(MyHost::MyStringInstance *text = dynamic_cast<MyHost::MyStringInstance *>(param))
      return text->get(value) == kOfxStatOK;

    int number = 0;

    if(MyHost::MyChoiceInstance *choice = dynamic_cast<MyHost::MyChoiceInstance *>(param)) {
      if(choice->get(number) != kOfxStatOK)
        return false;
      value = formatInt(number);
      return true;
    }

    if(MyHost::MyIntegerInstance *integer = dynamic_cast<MyHost::MyIntegerInstance *>(param)) {
      if(integer->get(number) != kOfxStatOK)
        return false;
      value = formatInt(number);
      return true;
    }

    return false;
  }

  /// the same, at a time rather than as the parameter's scalar value
  bool getParamValue(OFX::Host::ImageEffect::Instance &instance,
                     const std::string &name,
                     OfxTime time,
                     std::string &value)
  {
    OFX::Host::Param::Instance *param = instance.getParam(name);

    if(MyHost::MyStringInstance *text = dynamic_cast<MyHost::MyStringInstance *>(param))
      return text->get(time, value) == kOfxStatOK;

    int number = 0;

    if(MyHost::MyChoiceInstance *choice = dynamic_cast<MyHost::MyChoiceInstance *>(param)) {
      if(choice->get(time, number) != kOfxStatOK)
        return false;
      value = formatInt(number);
      return true;
    }

    if(MyHost::MyIntegerInstance *integer = dynamic_cast<MyHost::MyIntegerInstance *>(param)) {
      if(integer->get(time, number) != kOfxStatOK)
        return false;
      value = formatInt(number);
      return true;
    }

    return false;
  }

#ifdef OFX_SUPPORTS_METADATA

  /// write a value through the host's string and choice parameter instances and read it
  /// straight back, in both the scalar and the at-a-time form. A host that dropped what
  /// was written, or that answered with the declared default instead of it, fails these
  void checkParams(Report &report, OFX::Host::ImageEffect::Instance &instance)
  {
    std::string text = "none";
    std::string option = "none";

    if(!report.check(getParamValue(instance, kNoteParam, text), std::string("plugin param=") + kNoteParam))
      return;
    if(!report.check(getParamValue(instance, kDetailParam, option), std::string("plugin param=") + kDetailParam))
      return;

    const OfxTime time = MetadataFixture::kLastFrame;
    const std::string noteWhere = std::string("param=") + kNoteParam;
    const std::string noteAtTime = noteWhere + " time=" + formatTime(time);

    report.check(text == kNoteDefault, noteWhere + " default=" + text);

    report.check(setParamValue(instance, kNoteParam, kNoteScalar), noteWhere + " set=" + kNoteScalar);

    text = "none";
    bool ok = getParamValue(instance, kNoteParam, text) && text == kNoteScalar;
    report.check(ok, noteWhere + " value=" + text);

    report.check(setParamValue(instance, kNoteParam, time, kNoteAtTime), noteAtTime + " set=" + kNoteAtTime);

    text = "none";
    ok = getParamValue(instance, kNoteParam, time, text) && text == kNoteAtTime;
    report.check(ok, noteAtTime + " value=" + text);

    const std::string choiceWhere = std::string("param=") + kDetailParam;
    const std::string choiceAtTime = choiceWhere + " time=" + formatTime(time);

    report.check(option == formatInt(kDetailDefault), choiceWhere + " default=" + option);

    report.check(setParamValue(instance, kDetailParam, formatInt(kDetailScalar)),
                 choiceWhere + " set=" + formatInt(kDetailScalar));

    option = "none";
    ok = getParamValue(instance, kDetailParam, option) && option == formatInt(kDetailScalar);
    report.check(ok, choiceWhere + " value=" + option);

    report.check(setParamValue(instance, kDetailParam, time, formatInt(kDetailAtTime)),
                 choiceAtTime + " set=" + formatInt(kDetailAtTime));

    option = "none";
    ok = getParamValue(instance, kDetailParam, time, option) && option == formatInt(kDetailAtTime);
    report.check(ok, choiceAtTime + " value=" + option);
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

#endif // OFX_SUPPORTS_METADATA

  /// what a render pass produced, for a caller with more to say about it than checkRender
  /// says on its own
  struct RenderPass {
    std::vector<LogRecord> records;
    int framesRendered;
    int framesPassedThrough; ///< frames whose output came back byte identical to the source

    RenderPass() : framesRendered(0), framesPassedThrough(0) {}
  };

  /// render every frame of the fixture range through the plugin, the way a host that
  /// meant to produce output would, and capture anything logged through the message
  /// suite instead of letting it land between the PASS/FAIL lines above
  void checkRender(Report &report, OFX::Host::ImageEffect::Instance &instance, RenderPass *pass = NULL)
  {
    if(!report.check(instance.getClipPreferences(), "render clipprefs"))
      return;

    MyHost::MyEffectInstance *effect = dynamic_cast<MyHost::MyEffectInstance *>(&instance);
    report.check(effect != NULL, "render effect instance");

    std::string captured;
    if(effect)
      effect->setMessageCapture(&captured);

    OfxPointD renderScale;
    renderScale.x = renderScale.y = 1.0;

    OfxRectI renderWindow;
    renderWindow.x1 = renderWindow.y1 = 0;
    renderWindow.x2 = renderWindow.y2 = kRenderWindowSize;

    OfxRectD roi;
    roi.x1 = roi.y1 = 0;
    roi.x2 = roi.y2 = 4;

    MyHost::MyClipInstance *source =
      dynamic_cast<MyHost::MyClipInstance *>(instance.getClip(kOfxImageEffectSimpleSourceClipName));
    MyHost::MyClipInstance *output =
      dynamic_cast<MyHost::MyClipInstance *>(instance.getClip(kOfxImageEffectOutputClipName));

    const OfxTime first = MetadataFixture::kFirstFrame;
    const OfxTime last  = MetadataFixture::kLastFrame;

    OfxStatus stat = instance.beginRenderAction(first, last, 1.0, false, renderScale,
                                                /*sequential=*/true, /*interactive=*/false);

    int rendered = 0;
    int passedThrough = 0;

    // a plugin that refused to begin the sequence must not then be issued the frames
    // of one, or the end of one
    if(report.check(stat == kOfxStatOK || stat == kOfxStatReplyDefault, "render beginsequence")) {
      for(OfxTime time = first; time <= last; time += 1) {
        if(gMessageSuite)
          gMessageSuite->message(instance.getHandle(), kOfxMessageLog, "metadataHost",
                                 "metadataHost rendered frame %g", time);

        std::map<OFX::Host::ImageEffect::ClipInstance *, OfxRectD> rois;
        stat = instance.getRegionOfInterestAction(time, renderScale, roi, rois);
        report.check(stat == kOfxStatOK || stat == kOfxStatReplyDefault,
                     "render frame=" + formatTime(time) + " roi");

        // a plugin which has yet to fetch its output image has left nothing to compare,
        // and comparing what it renders against what it started from says nothing unless
        // the two differed to begin with
        MyHost::MyImage *held = output ? output->getOutputImage() : NULL;
        MyHost::MyImage *wanted =
          held && source ? dynamic_cast<MyHost::MyImage *>(source->getImage(time, NULL)) : NULL;

        const bool comparable = wanted && !imagesEqual(*held, *wanted, renderWindow);

        if(wanted)
          report.check(comparable, "render frame=" + formatTime(time) + " pixels differ");

        stat = instance.renderAction(time, kOfxImageFieldBoth, renderWindow, renderScale,
                                     /*sequential=*/true, /*interactive=*/false, /*draft=*/false);
        report.check(stat == kOfxStatOK || stat == kOfxStatReplyDefault,
                     "render frame=" + formatTime(time));

        if(comparable) {
          const bool identical = imagesEqual(*held, *wanted, renderWindow);

          if(identical)
            passedThrough += 1;

          report.check(identical, "render frame=" + formatTime(time) + " pixels rendered");
        }

        if(wanted)
          wanted->releaseReference();

        rendered += 1;
      }

      stat = instance.endRenderAction(first, last, 1.0, false, renderScale,
                                      /*sequential=*/true, /*interactive=*/false);
      report.check(stat == kOfxStatOK || stat == kOfxStatReplyDefault, "render endsequence");

      std::ostringstream framesWhere;
      framesWhere << "render framesrendered=" << rendered;
      report.check(rendered == int(last - first + 1), framesWhere.str());
    }

    if(effect)
      effect->setMessageCapture(NULL);

    std::cout << "metadataHost captured messages begin" << std::endl;
    std::cout << captured;
    std::cout << "metadataHost captured messages end" << std::endl;

    report.check(captured.find("metadataHost rendered frame") != std::string::npos,
                 "render messagecapture");

    std::vector<LogRecord> records;
    parseLogRecords(captured, records);

    // a plugin which logs nothing in the grammar has nothing to hold to the fixture
    if(!records.empty())
      checkLogAgainstFixture(report, records, "render");

    if(pass) {
      pass->records = records;
      pass->framesRendered = rendered;
      pass->framesPassedThrough = passedThrough;
    }
  }

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

    if(!report.check(created == kOfxStatOK || created == kOfxStatReplyDefault, "plugin createinstance"))
      instance.reset();

    return instance;
  }

  /// the context a plugin driven by --plugin-id is instantiated in: Filter is preferred,
  /// General is the fallback for a plugin that declares only it, and failing both,
  /// whatever the plugin does declare
  std::string chooseContext(OFX::Host::ImageEffect::ImageEffectPlugin &plugin)
  {
    const std::set<std::string> &contexts = plugin.getContexts();

    if(contexts.count(kOfxImageEffectContextFilter))
      return kOfxImageEffectContextFilter;
    if(contexts.count(kOfxImageEffectContextGeneral))
      return kOfxImageEffectContextGeneral;

    return contexts.empty() ? std::string() : *contexts.begin();
  }

  /// what a plugin named by --plugin-id is held to beyond the generic preconditions,
  /// selected by name with --check. leastChecks is what the contract has to assert
  /// before it can be said to have run at all
  struct Contract {
    const char *name;
    int         leastChecks;
    void      (*run)(Report &report, OFX::Host::ImageEffect::Instance &instance);
  };

  /// the frames of the fixture range, which is what the contract below counts in
  const int kFixtureFrames = int(MetadataFixture::kLastFrame - MetadataFixture::kFirstFrame) + 1;

  /// the keys the fixture gives a clip at a time, joined in the ascending order a plugin
  /// enumerating them has to impose before it logs them
  std::string fixtureKeys(const std::string &clip, OfxTime time)
  {
    std::set<std::string> keys;

    for(int i = 0; i < MetadataFixture::kEntryCount; ++i) {
      if(entryAppliesAt(MetadataFixture::kEntries[i], clip, time))
        keys.insert(MetadataFixture::kEntries[i].key);
    }

    return joinKeys(keys);
  }

  /// the keys a log carries for a clip at a time, joined in the order they were logged
  std::string loggedKeys(const std::vector<LogRecord> &records, const std::string &clip, OfxTime time)
  {
    std::string joined;

    for(size_t r = 0; r < records.size(); ++r) {
      if(records[r].clip != clip || records[r].time != time)
        continue;

      if(!joined.empty())
        joined += ",";
      joined += records[r].key;
    }

    return joined;
  }

  /// what a log gives for one key of a clip at a time, false if it gives none
  bool loggedValue(const std::vector<LogRecord> &records,
                   const std::string &clip,
                   OfxTime time,
                   const std::string &key,
                   std::string &value)
  {
    for(size_t r = 0; r < records.size(); ++r) {
      if(records[r].clip == clip && records[r].time == time && records[r].key == key) {
        value = records[r].value;
        return true;
      }
    }

    return false;
  }

  /// hold a plugin which reads the metadata of its source clip and logs it to what the
  /// fixture gives that clip: every key of every frame, once each, with the fixture's
  /// type and value, in ascending order, and the image passed through untouched. The
  /// timecode check is what a plugin that read its clip once, rather than at the time it
  /// was handed to render, falls down on. Degraded is the same plugin on a host with no
  /// metadata suite, where it has nothing to read and so owes an empty log and the image
  void checkMetadataLog(Report &report, OFX::Host::ImageEffect::Instance &instance, bool degraded)
  {
    const std::string clip = kOfxImageEffectSimpleSourceClipName;
    const std::string where = degraded ? "metadata-log-degraded" : "metadata-log";

    RenderPass pass;
    checkRender(report, instance, &pass);

    std::ostringstream pixels;
    pixels << where << " passthrough frames=" << pass.framesRendered
           << " identical=" << pass.framesPassedThrough;

    report.check(pass.framesRendered == kFixtureFrames
                 && pass.framesPassedThrough == pass.framesRendered,
                 pixels.str());

    if(degraded) {
      std::ostringstream logged;
      logged << where << " logrecords=" << pass.records.size();

      report.check(pass.records.empty(), logged.str());
      return;
    }

    checkLogAgainstFixture(report, pass.records, where);

    for(OfxTime time = MetadataFixture::kFirstFrame; time <= MetadataFixture::kLastFrame; time += 1) {
      const std::string logged = loggedKeys(pass.records, clip, time);

      report.check(logged == fixtureKeys(clip, time),
                   where + " clip=" + clip + " frame=" + formatTime(time) + " keys=" + logged);
    }

    std::string atFirst = "none";
    std::string atLast = "none";

    const bool advances =
      loggedValue(pass.records, clip, MetadataFixture::kFirstFrame, kOfxMetadataKeyTimecode, atFirst)
      && loggedValue(pass.records, clip, MetadataFixture::kLastFrame, kOfxMetadataKeyTimecode, atLast)
      && atFirst != atLast;

    report.check(advances, where + " clip=" + clip + " " kOfxMetadataKeyTimecode
                 " first=" + atFirst + " last=" + atLast);
  }

  void checkMetadataLogSupported(Report &report, OFX::Host::ImageEffect::Instance &instance)
  {
    checkMetadataLog(report, instance, /*degraded=*/false);
  }

  void checkMetadataLogDegraded(Report &report, OFX::Host::ImageEffect::Instance &instance)
  {
    checkMetadataLog(report, instance, /*degraded=*/true);
  }

  /// the parameters a plugin which shows metadata has to expose for the contract below
  /// to drive it, and the values of its mode
  const char kFilterParam[]     = "filter";
  const char kFilterModeParam[] = "filterMode";
  const char kDisplayParam[]    = "display";

  enum FilterModeEnum {
    eFilterModeKeysAndValues,
    eFilterModeKeysOnly,
    eFilterModeValuesOnly,
    eFilterModeCount
  };

  /// the filters swept over: everything, one key of the fixture, the whole standard
  /// namespace, the same one key in a case the fixture does not hold it in, and nothing
  const char *const kDisplayFilters[] = {
    "",
    "timecode",
    kOfxMetadataKeyPrefixStandard,
    "TimeCode",
    "nosuchkey"
  };

  const int kDisplayFilterCount = sizeof(kDisplayFilters) / sizeof(kDisplayFilters[0]);

  /// the one case whose display is pinned to a literal rather than composed, so that the
  /// same mistake in this file's substring match and in the plugin's cannot hide in the
  /// agreement between them
  const char kPinnedFilter[]  = "timecode";
  const int  kPinnedMode      = eFilterModeKeysOnly;
  const char kPinnedDisplay[] = kOfxMetadataKeyTimecode;

  /// a display written as one line, so that a check stays on the line it is printed on
  /// and a stray newline is visible in it rather than laid out as one
  std::string escapeLines(const std::string &text)
  {
    std::string escaped;

    for(size_t i = 0; i < text.size(); ++i) {
      if(text[i] == '\n')
        escaped += "\\n";
      else
        escaped += text[i];
    }

    return escaped;
  }

  char lowerCase(char c)
  {
    return char(tolower((unsigned char) c));
  }

  /// does text hold needle, ignoring case. Written by lowering both and searching rather
  /// than the way a plugin would write it, so that the two do not share a mistake
  bool holdsNoCase(const std::string &text, const std::string &needle)
  {
    std::string haystack = text;
    std::string wanted = needle;

    std::transform(haystack.begin(), haystack.end(), haystack.begin(), lowerCase);
    std::transform(wanted.begin(), wanted.end(), wanted.begin(), lowerCase);

    return haystack.find(wanted) != std::string::npos;
  }

  /// what a plugin writing a value into text gives for an entry. A double goes through
  /// the stream default rather than formatDouble's seventeen digits, which is what a
  /// plugin that simply streams the number out produces
  std::string displayValue(const MetadataFixture::Entry &entry)
  {
    if(entry.type != MetadataFixture::eDouble)
      return entryValue(entry);

    std::ostringstream os;
    os << entry.doubleValue;
    return os.str();
  }

  /// the display the fixture owes for a clip at a time under a filter and a mode: the
  /// keys holding the filter, in ascending order, one to a line with no line after the
  /// last
  std::string expectedDisplay(const std::string &clip,
                              OfxTime time,
                              const std::string &filter,
                              int mode)
  {
    std::map<std::string, const MetadataFixture::Entry *> keys;

    for(int i = 0; i < MetadataFixture::kEntryCount; ++i) {
      if(entryAppliesAt(MetadataFixture::kEntries[i], clip, time))
        keys[MetadataFixture::kEntries[i].key] = &MetadataFixture::kEntries[i];
    }

    std::string text;

    for(std::map<std::string, const MetadataFixture::Entry *>::const_iterator it = keys.begin();
        it != keys.end(); ++it) {
      if(!holdsNoCase(it->first, filter))
        continue;

      if(!text.empty())
        text += "\n";

      switch(mode) {
      case eFilterModeKeysOnly   : text += it->first; break;
      case eFilterModeValuesOnly : text += displayValue(*it->second); break;
      default                    : text += it->first + "=" + displayValue(*it->second); break;
      }
    }

    return text;
  }

  /// hold a plugin which shows the metadata of its source clip in a parameter to what
  /// the fixture gives that clip, over every mode and a sweep of filters, with the image
  /// still passed through untouched. Each display is compared byte for byte, so the
  /// separator, the line order and the absence of a line after the last one are all held.
  /// Degraded is the same plugin on a host with no metadata suite, where every display it
  /// composes is empty whatever it is asked for
  void checkMetadataDisplay(Report &report, OFX::Host::ImageEffect::Instance &instance, bool degraded)
  {
    const std::string clip = kOfxImageEffectSimpleSourceClipName;
    const OfxTime time = MetadataFixture::kFirstFrame;
    const std::string contract = degraded ? "metadata-display-degraded" : "metadata-display";

    OfxPointD renderScale;
    renderScale.x = renderScale.y = 1.0;

    // the pinned display is a self check on this file's own composition, with no plugin
    // in it, so a host with nothing to compose from has nothing to pin
    if(!degraded) {
      const std::string pinned = expectedDisplay(clip, time, kPinnedFilter, kPinnedMode);

      report.check(pinned == kPinnedDisplay,
                   contract + " pinned filter=" + kPinnedFilter
                   + " expected=" + escapeLines(pinned) + " literal=" + kPinnedDisplay);
    }

    for(int mode = 0; mode < eFilterModeCount; ++mode) {
      for(int f = 0; f < kDisplayFilterCount; ++f) {
        const std::string filter = kDisplayFilters[f];

        std::ostringstream os;
        os << contract << " mode=" << mode << " filter=" << filter;
        const std::string where = os.str();

        const bool driven = setParamValue(instance, kFilterParam, filter)
                            && setParamValue(instance, kFilterModeParam, formatInt(mode));

        if(!report.check(driven, where + " parameters set"))
          continue;

        instance.beginInstanceChangedAction(kOfxChangeUserEdited);
        instance.paramInstanceChangedAction(kFilterParam, kOfxChangeUserEdited, time, renderScale);
        instance.paramInstanceChangedAction(kFilterModeParam, kOfxChangeUserEdited, time, renderScale);
        instance.endInstanceChangedAction(kOfxChangeUserEdited);

        RenderPass pass;
        checkRender(report, instance, &pass);

        std::ostringstream pixels;
        pixels << where << " passthrough frames=" << pass.framesRendered
               << " identical=" << pass.framesPassedThrough;

        report.check(pass.framesRendered == kFixtureFrames
                     && pass.framesPassedThrough == pass.framesRendered,
                     pixels.str());

        const std::string wanted = degraded ? std::string() : expectedDisplay(clip, time, filter, mode);
        std::string shown = "none";

        const bool read = getParamValue(instance, kDisplayParam, shown);

        report.check(read && shown == wanted,
                     where + " display=" + escapeLines(shown)
                     + " expected=" + escapeLines(wanted));
      }
    }
  }

  void checkMetadataDisplaySupported(Report &report, OFX::Host::ImageEffect::Instance &instance)
  {
    checkMetadataDisplay(report, instance, /*degraded=*/false);
  }

  void checkMetadataDisplayDegraded(Report &report, OFX::Host::ImageEffect::Instance &instance)
  {
    checkMetadataDisplay(report, instance, /*degraded=*/true);
  }

  /// the degraded contracts are registered in both builds on purpose: each pair is held
  /// to a host which cannot meet it in the build the other pair passes in, which is what
  /// shows either of them is able to fail at all
  const Contract kContractTable[] = {
    {"metadata-log", kFixtureFrames + 3, checkMetadataLogSupported},
    {"metadata-log-degraded", kFixtureFrames + 2, checkMetadataLogDegraded},
    {"metadata-display", eFilterModeCount * kDisplayFilterCount * 2 + 1, checkMetadataDisplaySupported},
    {"metadata-display-degraded", eFilterModeCount * kDisplayFilterCount * 2, checkMetadataDisplayDegraded}
  };

  const Contract *const kContracts = kContractTable;
  const int kContractCount = sizeof(kContractTable) / sizeof(kContractTable[0]);

  /// the contract of that name, NULL if there is none
  const Contract *findContract(const std::string &name)
  {
    for(int i = 0; i < kContractCount; ++i) {
      if(name == kContracts[i].name)
        return &kContracts[i];
    }

    return NULL;
  }

  /// load an arbitrary plugin by id and drive it far enough to prove the contract any
  /// plugin has to meet, regardless of what it does: it resolves, describes, creates an
  /// instance exposing the clips its context guarantees, and completes a render pass.
  /// It asserts nothing about composition order or retained keys, which a read-only
  /// plugin implements neither of. Returns the number of checks the contract made, zero
  /// if none was asked for or it never got as far as running
  int checkGenericPlugin(Report &report,
                         MyHost::MetadataHost &host,
                         const std::string &pluginDir,
                         const std::string &pluginId,
                         const Contract *contract)
  {
    BuildTreePluginCache cache(pluginDir);
    OFX::Host::ImageEffect::PluginCache effectCache(host);

    cache.setCacheVersion("metadataHostV1");
    effectCache.registerInCache(cache);
    cache.scanPluginFiles();

    OFX::Host::ImageEffect::ImageEffectPlugin *plugin = findPlugin(report, effectCache, pluginId, pluginDir);

    if(!plugin)
      return 0;

    const std::string context = chooseContext(*plugin);

    std::unique_ptr<OFX::Host::ImageEffect::Instance> instance = createPluginInstance(report, plugin, context);

    if(!instance.get())
      return 0;

    report.check(instance->getClip(kOfxImageEffectSimpleSourceClipName) != NULL,
                 "plugin clip=" kOfxImageEffectSimpleSourceClipName);
    report.check(instance->getClip(kOfxImageEffectOutputClipName) != NULL,
                 "plugin clip=" kOfxImageEffectOutputClipName);

    checkRender(report, *instance);

    if(!contract)
      return 0;

    const int before = report.mark();

    contract->run(report, *instance);

    const int ran = report.mark() - before;

    report.ranAtLeast(before, contract->leastChecks, std::string("check=") + contract->name);

    return ran;
  }

#ifdef OFX_SUPPORTS_METADATA

  /// the number of non overlapping occurrences of what in text
  int countOccurrences(const std::string &text, const std::string &what)
  {
    int found = 0;
    std::string::size_type at = text.find(what);

    while(at != std::string::npos) {
      found += 1;
      at = text.find(what, at + what.size());
    }

    return found;
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
    std::string orderValue = "none";

    if(!report.check(output != NULL, "plugin clip=" kOfxImageEffectOutputClipName))
      return;
    if(!report.check(getParamValue(*instance, kOrderParam, orderValue),
                     std::string("plugin param=") + kOrderParam))
      return;

    checkParams(report, *instance);

    // the plugin derives one of the keys it contributes from this, so what it holds now
    // is what the output has to come back carrying
    std::string note = "none";
    const bool noted = getParamValue(*instance, kNoteParam, note) && !note.empty();

    report.check(noted, std::string("effect param=") + kNoteParam + " contributed=" + note);

    OfxPointD renderScale;
    renderScale.x = renderScale.y = 1.0;

    const int defaultOrder = kMaskOverSource;
    report.check(orderValue == formatInt(defaultOrder),
                 std::string("plugin param=") + kOrderParam + " default=" + orderValue);

    // nothing has touched the instance yet, so this is the composition a param change
    // later on has to be shown moving the output away from
    std::map<std::string, std::string> baseline;
    checkOutput(report, *output, defaultOrder, MetadataFixture::kFirstFrame, note, baseline);

    std::vector<std::string> contested;
    contestedKeys(contested);
    report.check(!contested.empty(), "fixture contestedkeys=" + joinKeys(std::set<std::string>(contested.begin(), contested.end())));

    std::vector<std::string> dropped;
    droppedKeys(dropped);
    report.check(!dropped.empty(), "fixture droppedkeys=" + joinKeys(std::set<std::string>(dropped.begin(), dropped.end())));

    const int orders[] = {kMaskOverSource, kSourceOverMask};
    std::map<int, std::map<std::string, std::string> > atFirstFrame;

    // what the plugin logs about the set it was handed to write into, which is the only
    // way back from inside the action
    MyHost::MyEffectInstance *effect = dynamic_cast<MyHost::MyEffectInstance *>(instance.get());
    std::string captured;
    int actions = 0;

    if(effect)
      effect->setMessageCapture(&captured);

    // in this order the plugin writes into everything the action can write into and then
    // reports the action untrapped, so the output has to carry what the host offered it
    // and nothing of what was written
    report.check(setParamValue(*instance, kOrderParam, formatInt(kUntrappedOrder)),
                 "effect order=" + formatInt(kUntrappedOrder) + " parameter set");

    instance->beginInstanceChangedAction(kOfxChangeUserEdited);
    instance->paramInstanceChangedAction(kOrderParam, kOfxChangeUserEdited, MetadataFixture::kFirstFrame, renderScale);
    instance->endInstanceChangedAction(kOfxChangeUserEdited);

    for(OfxTime time = MetadataFixture::kFirstFrame; time <= MetadataFixture::kLastFrame; time += 1) {
      std::map<std::string, std::string> read;
      checkOutput(report, *output, kUntrappedOrder, time, note, read);
      actions += 1;

      if(time == MetadataFixture::kFirstFrame)
        atFirstFrame[kUntrappedOrder] = read;
    }

    // nothing here calls invalidateMetadata directly: the changed action above is the
    // only thing that could have moved the read below off the baseline composition
    bool movedFromDefault = baseline.size() != atFirstFrame[kUntrappedOrder].size();

    for(std::map<std::string, std::string>::const_iterator it = baseline.begin();
        it != baseline.end() && !movedFromDefault; ++it) {
      std::map<std::string, std::string>::const_iterator found = atFirstFrame[kUntrappedOrder].find(it->first);
      movedFromDefault = found == atFirstFrame[kUntrappedOrder].end() || found->second != it->second;
    }

    report.check(movedFromDefault,
                 "effect param=" + std::string(kOrderParam) + " order=" + formatInt(kUntrappedOrder)
                 + " composition=updated withoutmanualinvalidate=true");

    // in this one the plugin nominates no clip at all, so there is nothing to inherit and
    // the output has to carry the keys it contributed and none besides
    report.check(setParamValue(*instance, kOrderParam, formatInt(kNoSourceOrder)),
                 "effect order=" + formatInt(kNoSourceOrder) + " parameter set");

    instance->beginInstanceChangedAction(kOfxChangeUserEdited);
    instance->paramInstanceChangedAction(kOrderParam, kOfxChangeUserEdited, MetadataFixture::kFirstFrame, renderScale);
    instance->endInstanceChangedAction(kOfxChangeUserEdited);

    std::vector<Contributed> contributed;
    contributedKeys(note, contributed);

    for(OfxTime time = MetadataFixture::kFirstFrame; time <= MetadataFixture::kLastFrame; time += 1) {
      std::map<std::string, std::string> read;
      checkOutput(report, *output, kNoSourceOrder, time, note, read);
      actions += 1;

      int inherited = 0;

      for(std::map<std::string, std::string>::const_iterator it = read.begin(); it != read.end(); ++it) {
        bool contributes = false;

        for(size_t c = 0; c < contributed.size(); ++c)
          contributes = contributes || contributed[c].key == it->first;

        if(!contributes)
          inherited += 1;
      }

      std::ostringstream ns;
      ns << "effect order=" << kNoSourceOrder << " time=" << formatTime(time)
         << " keys=" << read.size() << " contributed=" << contributed.size()
         << " inherited=" << inherited;

      report.check(read.size() == contributed.size() && inherited == 0, ns.str());
    }

    for(size_t o = 0; o < sizeof(orders) / sizeof(orders[0]); ++o) {
      const int which = orders[o];

      std::ostringstream os;
      os << "effect order=" << which;
      const std::string where = os.str();

      report.check(setParamValue(*instance, kOrderParam, formatInt(which)), where + " parameter set");

      instance->beginInstanceChangedAction(kOfxChangeUserEdited);
      instance->paramInstanceChangedAction(kOrderParam, kOfxChangeUserEdited, MetadataFixture::kFirstFrame, renderScale);
      instance->endInstanceChangedAction(kOfxChangeUserEdited);

      std::vector<std::string> perFrame;
      composedPerFrameKeys(which, note, perFrame);
      report.check(!perFrame.empty(),
                   where + " perframekeys=" + joinKeys(std::set<std::string>(perFrame.begin(), perFrame.end())));

      std::map<std::string, std::set<std::string> > seen;
      int frames = 0;

      for(OfxTime time = MetadataFixture::kFirstFrame; time <= MetadataFixture::kLastFrame; time += 1) {
        std::map<std::string, std::string> read;
        checkOutput(report, *output, which, time, note, read);
        actions += 1;

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

    if(effect)
      effect->setMessageCapture(NULL);

    const int handed = countOccurrences(captured, "metadataPlugin metadataset present");
    const int emptied = countOccurrences(captured, "metadataPlugin metadataset present keys=0");

    std::ostringstream hs;
    hs << "effect metadataset handed=" << handed << " actions=" << actions;
    report.check(actions > 0 && handed == actions, hs.str());

    std::ostringstream es;
    es << "effect metadataset empty=" << emptied << " handed=" << handed;
    report.check(handed > 0 && emptied == handed, es.str());

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

#endif // OFX_SUPPORTS_METADATA

  int runChecks(const std::string &pluginDir, const std::string &pluginId, const Contract *contract)
  {
    MyHost::MetadataHost host;
    OfxHost *handle = host.getHandle();

    gPropSuite = (const OfxPropertySuiteV2 *) handle->fetchSuite(handle->host, kOfxPropertySuite, 2);
    gMetadataSuite = (const OfxMetadataSuiteV1 *) handle->fetchSuite(handle->host, kOfxMetadataSuite, 1);
    gEffectSuite = (const OfxImageEffectSuiteV1 *) handle->fetchSuite(handle->host, kOfxImageEffectSuite, 1);
    gMessageSuite = (const OfxMessageSuiteV2 *) handle->fetchSuite(handle->host, kOfxMessageSuite, 2);

    if(!gPropSuite || !gEffectSuite || !gMessageSuite) {
      std::cout << "metadataHost the host does not vend the suites this needs" << std::endl;
      std::cout << "RESULT FAIL" << std::endl;
      return 1;
    }

    Report report;

    // the suite the host vends is what its build option decides, so its absence is a
    // check like any other rather than a reason not to run
    const bool suite = gMetadataSuite != NULL;
    report.check(suite == kMetadataSuiteExpected,
                 std::string("host metadatasuite ") + (suite ? "present" : "absent"));

    if(pluginId.empty()) {
#ifdef OFX_SUPPORTS_METADATA
      checkFixture(report);
      checkComparators(report);
      checkClips(report);
      checkMetadataWrites(report);
      checkPlugin(report, host, pluginDir);
#else
      std::cerr << "metadataHost this build has no metadata suite, so --plugin-id is required"
                << std::endl;
      return 2;
#endif // OFX_SUPPORTS_METADATA
    }
    else {
      const int ran = checkGenericPlugin(report, host, pluginDir, pluginId, contract);

      if(contract)
        report.check(ran > 0, std::string("check=") + contract->name + " ran");
    }

    std::cout << "metadataHost checks=" << report.getChecks()
              << " failures=" << report.getFailures() << std::endl;
    std::cout << "RESULT " << (report.getFailures() ? "FAIL" : "PASS") << std::endl;

    return report.getFailures() ? 1 : 0;
  }

  void usage(std::ostream &os)
  {
    os << "usage: metadataHost [--list] [--plugin-dir <path>] [--plugin-id <id>]" << std::endl;
    os << "                   [--check <name>]" << std::endl;
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
    os << "  --check <name>      hold the plugin --plugin-id names to the named contract"
       << std::endl;
    os << "                      as well as to those preconditions, one of:" << std::endl;
    os << "                        metadata-log               a plugin which logs the"
       << std::endl;
    os << "                                                   metadata of its source clip"
       << std::endl;
    os << "                        metadata-log-degraded      the same plugin on a host"
       << std::endl;
    os << "                                                   with no metadata suite"
       << std::endl;
    os << "                        metadata-display           a plugin which shows the"
       << std::endl;
    os << "                                                   metadata in a parameter"
       << std::endl;
    os << "                        metadata-display-degraded  the same plugin on a host"
       << std::endl;
    os << "                                                   with no metadata suite"
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
  std::string checkName;

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
    else if(arg == "--check") {
      if(i + 1 >= argc) {
        std::cerr << "metadataHost --check needs a name" << std::endl;
        usage(std::cerr);
        return 2;
      }
      checkName = argv[++i];
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

  const Contract *contract = NULL;

  if(!checkName.empty()) {
    if(pluginId.empty()) {
      std::cerr << "metadataHost --check needs --plugin-id" << std::endl;
      usage(std::cerr);
      return 2;
    }

    contract = findContract(checkName);

    if(!contract) {
      std::cerr << "metadataHost unknown check " << checkName << std::endl;
      usage(std::cerr);
      return 2;
    }
  }

  if(list) {
    listFixture();
    return 0;
  }

  return runChecks(pluginDir, pluginId, contract);
}