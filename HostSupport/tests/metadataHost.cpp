// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <list>
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
  const char *const kRevisedKey = MetadataFixture::kTimecodeKey;

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

  /// a string parameter that keeps what is written to it, which the demo host's own
  /// parameters do not, so that a contract can drive a plugin's parameters and read back
  /// what the plugin wrote into them
  class StoredStringInstance : public OFX::Host::Param::StringInstance {
    std::string _value;

  public :
    StoredStringInstance(OFX::Host::Param::Descriptor &descriptor, OFX::Host::Param::SetInstance *effect)
      : OFX::Host::Param::StringInstance(descriptor, effect)
      , _value(descriptor.getProperties().getStringProperty(kOfxParamPropDefault))
    {
    }

    virtual OfxStatus get(std::string &v) { v = _value; return kOfxStatOK; }
    virtual OfxStatus get(OfxTime, std::string &v) { v = _value; return kOfxStatOK; }
    virtual OfxStatus set(const char *v) { _value = v; return kOfxStatOK; }
    virtual OfxStatus set(OfxTime, const char *v) { _value = v; return kOfxStatOK; }
  };

  /// a choice parameter that keeps what is written to it, for the same reason
  class StoredChoiceInstance : public OFX::Host::Param::ChoiceInstance {
    int _value;

  public :
    StoredChoiceInstance(OFX::Host::Param::Descriptor &descriptor, OFX::Host::Param::SetInstance *effect)
      : OFX::Host::Param::ChoiceInstance(descriptor, effect)
      , _value(descriptor.getProperties().getIntProperty(kOfxParamPropDefault))
    {
    }

    virtual OfxStatus get(int &v) { v = _value; return kOfxStatOK; }
    virtual OfxStatus get(OfxTime, int &v) { v = _value; return kOfxStatOK; }
    virtual OfxStatus set(int v) { _value = v; return kOfxStatOK; }
    virtual OfxStatus set(OfxTime, int v) { _value = v; return kOfxStatOK; }
  };

  /// an effect whose clips publish the fixture, whose string and choice parameters keep
  /// their values, and whose messages can be captured rather than printed
  class MetadataEffectInstance : public MyEffectInstance {
  public :
    MetadataEffectInstance(OFX::Host::ImageEffect::ImageEffectPlugin *plugin,
                           OFX::Host::ImageEffect::Descriptor &desc,
                           const std::string &context)
      : MyEffectInstance(plugin, desc, context)
      , _messageCapture(NULL)
    {
    }

    virtual OFX::Host::ImageEffect::ClipInstance *newClipInstance(OFX::Host::ImageEffect::Instance *,
                                                                  OFX::Host::ImageEffect::ClipDescriptor *descriptor,
                                                                  int)
    {
      return new MetadataClipInstance(descriptor, this);
    }

    virtual OFX::Host::Param::Instance *newParam(const std::string &name, OFX::Host::Param::Descriptor &descriptor)
    {
      if(descriptor.getType() == kOfxParamTypeString)
        return new StoredStringInstance(descriptor, this);
      if(descriptor.getType() == kOfxParamTypeChoice)
        return new StoredChoiceInstance(descriptor, this);

      return MyEffectInstance::newParam(name, descriptor);
    }

    /// append what vmessage is handed to *capture, one message to a line, instead of
    /// printing it, so that a render's log can be read back rather than land between
    /// the PASS/FAIL lines; NULL to print again
    void setMessageCapture(std::string *capture) { _messageCapture = capture; }

    virtual OfxStatus vmessage(const char *type, const char *id, const char *format, va_list args)
    {
      if(!_messageCapture)
        return MyEffectInstance::vmessage(type, id, format, args);

      va_list measuring;
      va_copy(measuring, args);
      const int needed = vsnprintf(NULL, 0, format, measuring);
      va_end(measuring);

      std::vector<char> buf(needed > 0 ? size_t(needed) + 1 : 1, '\0');
      vsnprintf(buf.data(), buf.size(), format, args);

      *_messageCapture += type;
      *_messageCapture += " ";
      *_messageCapture += id;
      *_messageCapture += " ";
      *_messageCapture += buf.data();
      *_messageCapture += '\n';

      return kOfxStatOK;
    }

  private :
    std::string *_messageCapture;
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

  const OfxPropertySuiteV1    *gPropSuite = NULL;
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

    /// the check count as it stands
    int mark() const {return _checks;}

    /// the check a contract records on reaching its end, always true, so that a run
    /// which returned early is told from one that asserted everything it meant to
    bool completed(const std::string &what)
    {
      return check(true, what + " completed");
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

  OfxStatus collectKey(const char *key, OfxMetadataValueType /*type*/, int /*dimension*/, void *userData)
  {
    ((std::set<std::string> *) userData)->insert(key);
    return kOfxStatOK;
  }

  struct FindKeyState {
    std::string key;
    bool found;
    OfxMetadataValueType type;
    int dimension;
  };

  OfxStatus captureKeyInfo(const char *key, OfxMetadataValueType type, int dimension, void *userData)
  {
    FindKeyState *state = (FindKeyState *) userData;
    if(!state->found && state->key == key) {
      state->found = true;
      state->type = type;
      state->dimension = dimension;
    }
    return kOfxStatOK;
  }

  /// read a key back the way a plugin has to, by learning its type and dimension from
  /// metadataEnumerate rather than by knowing in advance
  bool readValueN(OfxPropertySetHandle metadata,
                  const char *key,
                  std::string &type,
                  int &dimension,
                  std::string &value)
  {
    dimension = 0;
    value.clear();

    FindKeyState state;
    state.key = key;
    state.found = false;

    if(gMetadataSuite->metadataEnumerate(metadata, captureKeyInfo, &state) != kOfxStatOK || !state.found)
      return false;

    dimension = state.dimension;
    if(dimension < 1)
      return false;

    switch(state.type) {
    case kOfxMetadataValueTypeString : {
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

    case kOfxMetadataValueTypeDouble : {
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

    case kOfxMetadataValueTypeInteger : {
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

  /// the keys the fixture gives a clip at a time
  void fixtureKeySet(const std::string &clip, OfxTime time, std::set<std::string> &keys)
  {
    for(int i = 0; i < MetadataFixture::kEntryCount; ++i) {
      if(entryAppliesAt(MetadataFixture::kEntries[i], clip, time))
        keys.insert(MetadataFixture::kEntries[i].key);
    }
  }

  /// a clip's metadata at a time, fetched under one check; NULL when the fetch failed
  OfxPropertySetHandle fetchMetadata(Report &report,
                                     OFX::Host::ImageEffect::ClipInstance &clip,
                                     OfxTime time,
                                     const std::string &where)
  {
    OfxPropertySetHandle metadata = NULL;
    const OfxStatus st = gMetadataSuite->clipGetMetadata(clip.getHandle(), time, &metadata);

    return report.check(st == kOfxStatOK && metadata, where + " fetched") ? metadata : NULL;
  }

  /// check a set holds exactly the expected keys, and hand back the ones it does hold
  std::set<std::string> checkKeys(Report &report,
                                  OfxPropertySetHandle metadata,
                                  const std::set<std::string> &expected,
                                  const std::string &where)
  {
    std::set<std::string> found;
    const OfxStatus st = gMetadataSuite->metadataEnumerate(metadata, collectKey, &found);

    report.check(st == kOfxStatOK && found == expected,
                 where + " keys=" + joinKeys(found) + " expected=" + joinKeys(expected));

    return found;
  }

  void releaseMetadata(Report &report, OfxPropertySetHandle metadata, const std::string &where)
  {
    report.check(gMetadataSuite->metadataRelease(metadata) == kOfxStatOK, where + " released");
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
    fixtureKeySet(clip, time, expected);
    checkKeys(report, metadata, expected, where);

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
      fixtureValue(MyHost::kRevisedClip, MetadataFixture::kTimecodeKey, MetadataFixture::kFirstFrame, atFirst)
      && fixtureValue(MyHost::kRevisedClip, MetadataFixture::kTimecodeKey, MetadataFixture::kLastFrame, atLast)
      && atFirst != atLast;

    if(report.check(advances, "selfcheck fixture timecode first=" + atFirst + " last=" + atLast)) {
      std::vector<LogRecord> repeated(records);

      for(size_t r = 0; r < repeated.size(); ++r) {
        if(repeated[r].key == MetadataFixture::kTimecodeKey && repeated[r].time == MetadataFixture::kLastFrame)
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

      OfxPropertySetHandle metadata = fetchMetadata(report, clip, time, where);

      if(!metadata)
        continue;

      std::map<std::string, std::string> read;
      checkAgainstFixture(report, metadata, clip.getName(), time, where, read);
      releaseMetadata(report, metadata, where);
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

    // the output clip has no fixture entries, so it has to hand back a set, and an empty one
    MyHost::MetadataClipInstance &output = *clips[MetadataFixture::kInputClipCount];
    const std::string where = "clip=" + output.getName() + " time=" + formatTime(MetadataFixture::kFirstFrame)
                              + " nometadata";
    OfxPropertySetHandle empty = NULL;
    const OfxStatus st = gMetadataSuite->clipGetMetadata(output.getHandle(), MetadataFixture::kFirstFrame, &empty);

    if(report.check(st == kOfxStatOK && empty != NULL, where)) {
      std::set<std::string> found;
      const OfxStatus enumerated = gMetadataSuite->metadataEnumerate(empty, collectKey, &found);

      report.check(enumerated == kOfxStatOK && found.empty(), where + " enumerate");
      report.check(gMetadataSuite->metadataEnumerate(empty, NULL, &found) == kOfxStatErrValue,
                   where + " enumerate nullcallback");
      report.check(gMetadataSuite->metadataRelease(empty) == kOfxStatOK, where + " release");
    }

    for(size_t i = 0; i < clips.size(); ++i)
      delete clips[i];
    for(size_t i = 0; i < descriptors.size(); ++i)
      delete descriptors[i];
  }

  ////////////////////////////////////////////////////////////////////////////////
  // the checks that need the plugin

  const char kPluginId[] = "net.sf.openfx.metadataPlugin";

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

  /// write text through the string or choice parameter a name resolves to, as its scalar
  /// value or, given a time, at that time, so that a check can drive a parameter it knows
  /// only by name and value
  bool setParamValue(OFX::Host::ImageEffect::Instance &instance,
                     const std::string &name,
                     const std::string &value,
                     const OfxTime *time = NULL)
  {
    OFX::Host::Param::Instance *param = instance.getParam(name);

    if(MyHost::StoredStringInstance *text = dynamic_cast<MyHost::StoredStringInstance *>(param))
      return (time ? text->set(*time, value.c_str()) : text->set(value.c_str())) == kOfxStatOK;

    int number = 0;

    if(MyHost::StoredChoiceInstance *choice = dynamic_cast<MyHost::StoredChoiceInstance *>(param))
      return parseInt(value, number) && (time ? choice->set(*time, number) : choice->set(number)) == kOfxStatOK;

    return false;
  }

  bool setParamValue(OFX::Host::ImageEffect::Instance &instance,
                     const std::string &name,
                     OfxTime time,
                     const std::string &value)
  {
    return setParamValue(instance, name, value, &time);
  }

  /// read a string or choice parameter back as text, as its scalar value or, given a
  /// time, at that time; false if the instance holds no such parameter
  bool getParamValue(OFX::Host::ImageEffect::Instance &instance,
                     const std::string &name,
                     std::string &value,
                     const OfxTime *time = NULL)
  {
    OFX::Host::Param::Instance *param = instance.getParam(name);

    if(MyHost::StoredStringInstance *text = dynamic_cast<MyHost::StoredStringInstance *>(param))
      return (time ? text->get(*time, value) : text->get(value)) == kOfxStatOK;

    int number = 0;
    MyHost::StoredChoiceInstance *choice = dynamic_cast<MyHost::StoredChoiceInstance *>(param);

    if(!choice || (time ? choice->get(*time, number) : choice->get(number)) != kOfxStatOK)
      return false;

    value = formatInt(number);
    return true;
  }

  bool getParamValue(OFX::Host::ImageEffect::Instance &instance,
                     const std::string &name,
                     OfxTime time,
                     std::string &value)
  {
    return getParamValue(instance, name, value, &time);
  }

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

  /// change what an input clip publishes and check the clip goes on handing back what it
  /// cached until its sets are dropped, then hands back the new value, and that dropping
  /// every clip's sets through the effect brings the original back
  void checkInvalidation(Report &report, OFX::Host::ImageEffect::Instance &instance)
  {
    OFX::Host::ImageEffect::ClipInstance *input = instance.getClip(MyHost::kRevisedClip);

    if(!report.check(input != NULL, std::string("invalidation clip=") + MyHost::kRevisedClip))
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
    const OfxTime time = MetadataFixture::kFirstFrame;

    std::string was = "none";
    std::string cached = "none";
    std::string is = "none";
    std::string restored = "none";

    const bool readBefore = readClipKey(*input, MyHost::kRevisedKey, time, was);

    MyHost::gRevision = after;

    const bool readCached = readClipKey(*input, MyHost::kRevisedKey, time, cached);

    input->invalidateMetadata();

    const bool readAfter = readClipKey(*input, MyHost::kRevisedKey, time, is);

    MyHost::gRevision = before;
    instance.invalidateMetadata();

    const bool readRestored = readClipKey(*input, MyHost::kRevisedKey, time, restored);

    report.check(readBefore && was == expectedBefore,
                 "invalidation before value=" + was + " expected=" + expectedBefore);
    report.check(expectedBefore != expectedAfter,
                 "invalidation fixture before=" + expectedBefore + " after=" + expectedAfter);
    report.check(readCached && cached == expectedBefore,
                 "invalidation cached value=" + cached + " expected=" + expectedBefore);
    report.check(readAfter && is == expectedAfter,
                 "invalidation after value=" + is + " expected=" + expectedAfter);
    report.check(readRestored && restored == expectedBefore,
                 "invalidation effect value=" + restored + " expected=" + expectedBefore);
  }

  /// the first clip 'instance' described that is not the output clip, in the order the
  /// effect described them; only the fixture's own plugins are guaranteed to have
  /// named it kOfxImageEffectSimpleSourceClipName, so this is what a check driving an
  /// arbitrary plugin has to ask for instead. NULL for a plugin that described no
  /// input clip at all, which a Generator-context effect and an input-less General one
  /// both may do
  OFX::Host::ImageEffect::ClipInstance *firstInputClip(OFX::Host::ImageEffect::Instance &instance)
  {
    const std::vector<OFX::Host::ImageEffect::ClipDescriptor *> &clips =
      instance.getDescriptor().getClipsByOrder();

    for(size_t i = 0; i < clips.size(); ++i) {
      if(!clips[i]->isOutput())
        return instance.getClip(clips[i]->getName());
    }

    return NULL;
  }

  /// how many clips 'instance' described that are not the output clip
  int countInputClips(OFX::Host::ImageEffect::Instance &instance)
  {
    const std::vector<OFX::Host::ImageEffect::ClipDescriptor *> &clips =
      instance.getDescriptor().getClipsByOrder();

    int count = 0;

    for(size_t i = 0; i < clips.size(); ++i) {
      if(!clips[i]->isOutput())
        count += 1;
    }

    return count;
  }

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
  bool checkRender(Report &report, OFX::Host::ImageEffect::Instance &instance, RenderPass *pass = NULL)
  {
    if(!report.check(instance.getClipPreferences(), "render clipprefs"))
      return false;

    MyHost::MetadataEffectInstance *effect = dynamic_cast<MyHost::MetadataEffectInstance *>(&instance);
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

    OFX::Host::ImageEffect::ClipInstance *source = firstInputClip(instance);
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

        // whatever the output clip is holding before this frame renders is leftover
        // from a previous render, so comparing it against this frame's source says
        // nothing about this frame unless the two already differ; a plugin that has
        // never fetched its output image has left nothing to establish that with,
        // which is expected on the first frame and not itself a fault
        MyHost::MyImage *before = output ? output->getOutputImage() : NULL;
        MyHost::MyImage *wanted =
          source ? dynamic_cast<MyHost::MyImage *>(source->getImage(time, NULL)) : NULL;

        if(before && wanted)
          report.check(!imagesEqual(*before, *wanted, renderWindow),
                       "render frame=" + formatTime(time) + " pixels differ");

        stat = instance.renderAction(time, kOfxImageFieldBoth, renderWindow, renderScale,
                                     /*sequential=*/true, /*interactive=*/false, /*draft=*/false);
        report.check(stat == kOfxStatOK || stat == kOfxStatReplyDefault,
                     "render frame=" + formatTime(time));

        // fetch again now that render has had its chance to produce one, rather than
        // trust the pre-render snapshot above: that makes this comparison
        // self-sufficient on every frame, including the first, instead of silently
        // skipped whenever there was nothing to compare beforehand
        MyHost::MyImage *held = output ? output->getOutputImage() : NULL;

        if(held && wanted) {
          const bool identical = imagesEqual(*held, *wanted, renderWindow);

          if(identical)
            passedThrough += 1;

          report.check(identical, "render frame=" + formatTime(time) + " pixels rendered");
        }
        else {
          report.check(true, "render frame=" + formatTime(time) + " pixels not comparable");
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

    return true;
  }

  /// the plugin cache has no way to replace the default search path, only to add to
  /// it, and this must load the plugins built alongside it rather than whatever the
  /// machine happens to have installed. --plugin-dir adds a second directory for the
  /// installed example plugins
  class BuildTreePluginCache : public OFX::Host::PluginCache {
  public :
    explicit BuildTreePluginCache(const std::string &dir)
    {
      _pluginPath.clear();
      addFileToPath(METADATA_PLUGIN_DIR, false);
      if(std::filesystem::weakly_canonical(dir) != std::filesystem::weakly_canonical(METADATA_PLUGIN_DIR))
        addFileToPath(dir, false);
    }
  };

  /// scan both directories into the cache, then refuse a plugin identifier that more
  /// than one bundle supplies at the same major version, since getPluginById would
  /// otherwise pick one by load order
  bool loadPlugins(Report &report,
                   BuildTreePluginCache &cache,
                   OFX::Host::ImageEffect::PluginCache &effectCache)
  {
    cache.setCacheVersion("metadataHostV1");
    effectCache.registerInCache(cache);
    cache.scanPluginFiles();

    std::map<std::string, std::string> bundleByPlugin;
    const std::list<OFX::Host::Plugin *> &plugins = cache.getPlugins();
    bool unique = true;

    for(std::list<OFX::Host::Plugin *>::const_iterator i = plugins.begin(); i != plugins.end(); ++i) {
      const std::string key = (*i)->getIdentifier() + " v" + formatInt((*i)->getVersionMajor());
      const std::string bundle = (*i)->getBinary()->getBundlePath();
      const std::pair<std::map<std::string, std::string>::iterator, bool>
        seen = bundleByPlugin.insert(std::make_pair(key, bundle));

      if(!seen.second && seen.first->second != bundle)
        unique = report.check(false, "plugin id=" + key + " duplicate bundles="
                              + seen.first->second + " " + bundle);
    }

    return unique;
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
  /// selected by name with --check. run answers whether it reached its end, which it
  /// records with Report::completed, rather than returned early
  struct Contract {
    const char *name;
    bool      (*run)(Report &report, OFX::Host::ImageEffect::Instance &instance);
  };

  /// what a contract made of the plugin it was run on: the checks it recorded and how
  /// many of them failed, both zero when the plugin never got as far as the contract
  struct ContractRun {
    int checks = 0;
    int failures = 0;
  };

  /// the frames of the fixture range, which is what the contract below counts in
  const int kFixtureFrames = int(MetadataFixture::kLastFrame - MetadataFixture::kFirstFrame) + 1;

  /// render the fixture range and check every frame came back byte identical to the source
  bool checkPassThroughRender(Report &report,
                              OFX::Host::ImageEffect::Instance &instance,
                              const std::string &where,
                              RenderPass *pass = NULL)
  {
    RenderPass rendered;

    if(!checkRender(report, instance, &rendered))
      return false;

    std::ostringstream os;
    os << where << " passthrough frames=" << rendered.framesRendered
       << " identical=" << rendered.framesPassedThrough;

    report.check(rendered.framesRendered == kFixtureFrames
                 && rendered.framesPassedThrough == rendered.framesRendered,
                 os.str());

    if(pass)
      *pass = rendered;

    return true;
  }

  /// a parameter and the text to drive into it
  struct ParamValue {
    std::string name;
    std::string value;
  };

  /// write each value through setParamValue, stopping at the first the host refuses
  bool setParams(OFX::Host::ImageEffect::Instance &instance, const std::vector<ParamValue> &params)
  {
    for(const ParamValue &param : params) {
      if(!setParamValue(instance, param.name, param.value))
        return false;
    }

    return true;
  }

  /// drive one effect's parameters through the actions a host raises around a user edit
  void paramsChanged(OFX::Host::ImageEffect::Instance &instance,
                     const std::vector<ParamValue> &params,
                     OfxTime time = MetadataFixture::kFirstFrame)
  {
    OfxPointD renderScale;
    renderScale.x = renderScale.y = 1.0;

    instance.beginInstanceChangedAction(kOfxChangeUserEdited);

    for(const ParamValue &param : params)
      instance.paramInstanceChangedAction(param.name, kOfxChangeUserEdited, time, renderScale);

    instance.endInstanceChangedAction(kOfxChangeUserEdited);
  }

  /// set the parameters, report that under 'what', and raise the changed actions around
  /// them; false, with nothing raised, when the host refused one of the sets
  bool driveParams(Report &report,
                   OFX::Host::ImageEffect::Instance &instance,
                   const std::vector<ParamValue> &params,
                   const std::string &what,
                   OfxTime time = MetadataFixture::kFirstFrame)
  {
    if(!report.check(setParams(instance, params), what))
      return false;

    paramsChanged(instance, params, time);

    return true;
  }

  /// the keys the fixture gives a clip at a time, joined in the ascending order a plugin
  /// enumerating them has to impose before it logs them
  std::string fixtureKeys(const std::string &clip, OfxTime time)
  {
    std::set<std::string> keys;

    fixtureKeySet(clip, time, keys);

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
  /// was handed to render, falls down on. Degraded holds the same plugin to an empty log
  /// and the image, which a plugin that reads its clip cannot deliver: CI runs it as the
  /// negative that proves this contract is able to fail
  bool checkMetadataLog(Report &report, OFX::Host::ImageEffect::Instance &instance, bool degraded)
  {
    const std::string clip = kOfxImageEffectSimpleSourceClipName;
    const std::string where = degraded ? "metadata-log-degraded" : "metadata-log";

    RenderPass pass;

    if(!checkPassThroughRender(report, instance, where, &pass))
      return false;

    if(degraded) {
      std::ostringstream logged;
      logged << where << " logrecords=" << pass.records.size();

      report.check(pass.records.empty(), logged.str());

      return report.completed(where);
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
      loggedValue(pass.records, clip, MetadataFixture::kFirstFrame, MetadataFixture::kTimecodeKey, atFirst)
      && loggedValue(pass.records, clip, MetadataFixture::kLastFrame, MetadataFixture::kTimecodeKey, atLast)
      && atFirst != atLast;

    report.check(advances, where + " clip=" + clip + " " + MetadataFixture::kTimecodeKey
                 + " first=" + atFirst + " last=" + atLast);

    return report.completed(where);
  }

  bool checkMetadataLogSupported(Report &report, OFX::Host::ImageEffect::Instance &instance)
  {
    return checkMetadataLog(report, instance, /*degraded=*/false);
  }

  bool checkMetadataLogDegraded(Report &report, OFX::Host::ImageEffect::Instance &instance)
  {
    return checkMetadataLog(report, instance, /*degraded=*/true);
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

  /// the filters swept over: everything, one key of the fixture, a substring two of
  /// its keys share, the same one key in a case the fixture does not hold it in, and
  /// nothing
  const char *const kDisplayFilters[] = {
    "",
    "timecode",
    "frame",
    "TimeCode",
    "nosuchkey"
  };

  const int kDisplayFilterCount = sizeof(kDisplayFilters) / sizeof(kDisplayFilters[0]);

  /// the one case whose display is pinned to a literal rather than composed, so that the
  /// same mistake in this file's substring match and in the plugin's cannot hide in the
  /// agreement between them
  const char kPinnedFilter[]  = "timecode";
  const int  kPinnedMode      = eFilterModeKeysOnly;
  const char kPinnedDisplay[] = "timecode";

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
  /// Degraded holds the same plugin to an empty display whatever it is asked for, which a
  /// plugin that shows its metadata cannot deliver: CI runs it as the negative that
  /// proves this contract is able to fail
  bool checkMetadataDisplay(Report &report, OFX::Host::ImageEffect::Instance &instance, bool degraded)
  {
    const std::string clip = kOfxImageEffectSimpleSourceClipName;
    const OfxTime time = MetadataFixture::kFirstFrame;
    const std::string contract = degraded ? "metadata-display-degraded" : "metadata-display";

    // the pinned display is a self check on this file's own composition, with no plugin
    // in it, so the negative, which expects nothing composed, has nothing to pin
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

        if(!driveParams(report, instance, {{kFilterParam, filter}, {kFilterModeParam, formatInt(mode)}},
                        where + " parameters set", time))
          continue;

        if(!checkPassThroughRender(report, instance, where))
          return false;

        const std::string wanted = degraded ? std::string() : expectedDisplay(clip, time, filter, mode);
        std::string shown = "none";

        const bool read = getParamValue(instance, kDisplayParam, shown);

        report.check(read && shown == wanted,
                     where + " display=" + escapeLines(shown)
                     + " expected=" + escapeLines(wanted));
      }
    }

    return report.completed(contract);
  }

  bool checkMetadataDisplaySupported(Report &report, OFX::Host::ImageEffect::Instance &instance)
  {
    return checkMetadataDisplay(report, instance, /*degraded=*/false);
  }

  bool checkMetadataDisplayDegraded(Report &report, OFX::Host::ImageEffect::Instance &instance)
  {
    return checkMetadataDisplay(report, instance, /*degraded=*/true);
  }

  /// each degraded contract is a negative: CI holds it to the plugin its non-degraded
  /// twin passes, under --expect-failure, which is what shows the contract is able to
  /// fail at all
  const Contract kContractTable[] = {
    {"metadata-log", checkMetadataLogSupported},
    {"metadata-log-degraded", checkMetadataLogDegraded},
    {"metadata-display", checkMetadataDisplaySupported},
    {"metadata-display-degraded", checkMetadataDisplayDegraded}
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
  /// Returns what the contract made of it, nothing if none was asked for or it never got
  /// as far as running
  ContractRun checkGenericPlugin(Report &report,
                         MyHost::MetadataHost &host,
                         const std::string &pluginDir,
                         const std::string &pluginId,
                         const Contract *contract)
  {
    BuildTreePluginCache cache(pluginDir);
    OFX::Host::ImageEffect::PluginCache effectCache(host);

    if(!loadPlugins(report, cache, effectCache))
      return ContractRun();

    OFX::Host::ImageEffect::ImageEffectPlugin *plugin = findPlugin(report, effectCache, pluginId, pluginDir);

    if(!plugin)
      return ContractRun();

    const std::string context = chooseContext(*plugin);

    std::unique_ptr<OFX::Host::ImageEffect::Instance> instance = createPluginInstance(report, plugin, context);

    if(!instance.get())
      return ContractRun();

    report.check(true, "plugin clip=inputclip count=" + formatInt(countInputClips(*instance)));
    report.check(instance->getClip(kOfxImageEffectOutputClipName) != NULL,
                 "plugin clip=" kOfxImageEffectOutputClipName);

    checkRender(report, *instance);

    ContractRun ran;

    if(contract) {
      const int before = report.mark();
      const int failedBefore = report.getFailures();

      const bool completed = contract->run(report, *instance);

      ran.checks = report.mark() - before;
      ran.failures = report.getFailures() - failedBefore;

      report.check(completed, std::string("check=") + contract->name + " completed");
    }

    return ran;
  }

  /// vmessage has to capture a message of any length whole rather than truncate it
  /// silently, so drive one well past a kilobyte and check every byte of it arrives
  void checkLongMessage(Report &report, OFX::Host::ImageEffect::Instance &instance)
  {
    MyHost::MetadataEffectInstance *effect = dynamic_cast<MyHost::MetadataEffectInstance *>(&instance);

    if(!report.check(effect != NULL, "message effect instance"))
      return;

    const std::string body(1200, 'x');

    std::string captured;
    effect->setMessageCapture(&captured);

    if(gMessageSuite)
      gMessageSuite->message(instance.getHandle(), kOfxMessageLog, "metadataHost",
                             "metadataHost longmessage %s", body.c_str());

    effect->setMessageCapture(NULL);

    report.check(captured.find(body) != std::string::npos,
                 "message longmessage length=" + formatInt(int(body.size())));
  }

  /// load the plugin, attach the fixture's clips to it and check the host's own side of
  /// driving it: messages, parameters, metadata invalidation and a render pass, during
  /// which the plugin reads every key of every input clip
  void checkPlugin(Report &report, MyHost::MetadataHost &host, const std::string &pluginDir)
  {
    BuildTreePluginCache cache(pluginDir);
    OFX::Host::ImageEffect::PluginCache effectCache(host);

    if(!loadPlugins(report, cache, effectCache))
      return;

    OFX::Host::ImageEffect::ImageEffectPlugin *plugin = findPlugin(report, effectCache, kPluginId, pluginDir);

    if(!plugin)
      return;

    std::unique_ptr<OFX::Host::ImageEffect::Instance>
      instance = createPluginInstance(report, plugin, kOfxImageEffectContextGeneral);

    if(!instance.get())
      return;

    checkLongMessage(report, *instance);

    report.check(instance->getClip(kOfxImageEffectOutputClipName) != NULL,
                 "plugin clip=" kOfxImageEffectOutputClipName);

    checkParams(report, *instance);
    checkInvalidation(report, *instance);
    checkRender(report, *instance);
  }

  int runChecks(const std::string &pluginDir,
                const std::string &pluginId,
                const Contract *contract,
                bool expectFailure)
  {
    MyHost::MetadataHost host;
    OfxHost *handle = host.getHandle();

    gPropSuite = (const OfxPropertySuiteV1 *) handle->fetchSuite(handle->host, kOfxPropertySuite, 1);
    gMetadataSuite = (const OfxMetadataSuiteV1 *) handle->fetchSuite(handle->host, kOfxMetadataSuite, 1);
    gEffectSuite = (const OfxImageEffectSuiteV1 *) handle->fetchSuite(handle->host, kOfxImageEffectSuite, 1);
    gMessageSuite = (const OfxMessageSuiteV2 *) handle->fetchSuite(handle->host, kOfxMessageSuite, 2);

    if(!gPropSuite || !gEffectSuite || !gMessageSuite) {
      std::cout << "metadataHost the host does not vend the suites this needs" << std::endl;
      std::cout << "RESULT FAIL" << std::endl;
      return 1;
    }

    Report report;
    ContractRun ran;

    report.check(gMetadataSuite != NULL,
                 std::string("host metadatasuite ") + (gMetadataSuite ? "present" : "absent"));

    if(pluginId.empty()) {
      const bool v2 = handle->fetchSuite(handle->host, kOfxPropertySuite, 2) != NULL;
      report.check(!v2, std::string("host propertysuite v2 ") + (v2 ? "present" : "absent"));

      checkFixture(report);
      checkComparators(report);
      checkClips(report);
      checkPlugin(report, host, pluginDir);
    }
    else {
      ran = checkGenericPlugin(report, host, pluginDir, pluginId, contract);

      if(contract)
        report.check(ran.checks > 0, std::string("check=") + contract->name + " ran");
    }

    std::cout << "metadataHost checks=" << report.getChecks()
              << " failures=" << report.getFailures() << std::endl;

    if(!expectFailure) {
      std::cout << "RESULT " << (report.getFailures() ? "FAIL" : "PASS") << std::endl;

      return report.getFailures() ? 1 : 0;
    }

    // only a failure the contract itself recorded counts: a plugin which never loaded,
    // or a contract which never ran, has not been shown to fail anything
    const bool met = ran.checks > 0 && ran.failures > 0;

    std::cout << "metadataHost expect-failure check=" << contract->name
              << " ran=" << ran.checks << " failed=" << ran.failures
              << (met ? " met" : " unmet") << std::endl;
    std::cout << "RESULT " << (met ? "PASS" : "FAIL") << std::endl;

    return met ? 0 : 1;
  }

  void usage(std::ostream &os)
  {
    os << R"(usage: metadataHost [--list] [--plugin-dir <path>] [--plugin-id <id>]
                   [--check <name>] [--expect-failure]
  --list              print the fixture table and exit
  --plugin-dir <path> look for plugin bundles in <path> as well as in
)"
          "                      " METADATA_PLUGIN_DIR "\n"
          R"(  --plugin-id <id>    load <id> from those dirs and check the general
                      preconditions any plugin has to meet - describe,
                      create an instance, expose its clips, render the
                      fixture range - rather than the fixture plugin's own
                      parameter, invalidation and message checks
  --check <name>      hold the plugin --plugin-id names to the named contract
                      as well as to those preconditions, one of:
                        metadata-log               a plugin which logs the
                                                   metadata of its source clip
                        metadata-log-degraded      the same plugin held to an
                                                   empty log: a negative any
                                                   plugin which reads its clip
                                                   must fail
                        metadata-display           a plugin which shows the
                                                   metadata in a parameter
                        metadata-display-degraded  the same plugin held to an
                                                   empty display: a negative
                                                   any plugin which shows its
                                                   metadata must fail
  --expect-failure    run the --check contract as a negative: exit 0 only if
                      the plugin loaded, the contract ran and at least one of
                      its checks failed. A plugin which never loaded, or a
                      contract which never ran or passed outright, exits 1
  with no arguments, publish the fixture through a host, read it back
  through the metadata suite, then drive the metadata plugin through
  the host, with the plugin reading the fixture back as it renders
)";
  }

} // anonymous

int main(int argc, char **argv)
{
  bool list = false;
  std::string pluginDir(METADATA_PLUGIN_DIR);
  std::string pluginId;
  std::string checkName;
  bool expectFailure = false;

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
    else if(arg == "--expect-failure") {
      expectFailure = true;
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

  if(expectFailure && checkName.empty()) {
    std::cerr << "metadataHost --expect-failure needs --check" << std::endl;
    usage(std::cerr);
    return 2;
  }

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

  return runChecks(pluginDir, pluginId, contract, expectFailure);
}