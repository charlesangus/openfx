// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#include "ofxsSupportPrivate.h"
#include "ofxsMetadata.h"

#include <climits>
#include <iomanip>
#include <locale>
#include <sstream>

using namespace OFX::Private;

namespace OFX {

  namespace {

    MetadataTypeEnum mapValueType(OfxMetadataValueType type)
    {
      switch(type) {
      case kOfxMetadataValueTypeInteger : return eMetadataTypeInt;
      case kOfxMetadataValueTypeDouble  : return eMetadataTypeDouble;
      case kOfxMetadataValueTypeString  : return eMetadataTypeString;
      }
      return eMetadataTypeNone;
    }

    OfxStatus collectMetadataEntry(const char *key, OfxMetadataValueType type, int dimension, void *userData)
    {
      // the host calls this through a C function pointer, so nothing may propagate out of it
      try {
        std::map<std::string, MetadataEntry> &entries = *(std::map<std::string, MetadataEntry> *) userData;
        entries[key] = MetadataEntry(key, mapValueType(type), dimension);
      }
      catch(...) {
        return kOfxStatErrMemory;
      }
      return kOfxStatOK;
    }

    /** @brief writes value as the shortest text which reads back as the same double, in
    the classic locale so that the decimal point does not follow whatever locale the host
    happens to have installed. Seventeen digits read back as any finite double, so the
    loop's last text is faithful whether or not a shorter one was, and a nan or an inf,
    which never compare equal, come out at that precision too */
    std::string doubleToString(double value)
    {
      std::string text;

      for(int precision = 15; precision <= 17; ++precision) {
        std::ostringstream os;
        os.imbue(std::locale::classic());
        os << std::setprecision(precision) << value;
        text = os.str();

        std::istringstream is(text);
        is.imbue(std::locale::classic());

        double roundTripped = 0;
        if(is >> roundTripped && roundTripped == value)
          break;
      }

      return text;
    }

    std::string intToString(int value)
    {
      std::ostringstream os;
      os.imbue(std::locale::classic());
      os << value;
      return os.str();
    }

    bool stringToDouble(const std::string &text, double &value)
    {
      std::istringstream is(text);
      is.imbue(std::locale::classic());
      is >> value;
      return !is.fail();
    }

    int doubleToInt(double value, int defaultValue)
    {
      if(value != value)
        return defaultValue;
      if(value >= double(INT_MAX))
        return INT_MAX;
      if(value <= double(INT_MIN))
        return INT_MIN;
      return int(value);
    }

    bool readAsString(OfxPropertySetHandle metadata, const char *key, MetadataTypeEnum type, int index, std::string &value)
    {
      double asDouble = 0;
      int asInt = 0;

      switch(type) {
      case eMetadataTypeString : {
        char *raw = 0;
        if(gPropSuite->propGetString(metadata, key, index, &raw) != kOfxStatOK || !raw)
          return false;
        value = raw;
        return true;
      }

      case eMetadataTypeDouble :
        if(gPropSuite->propGetDouble(metadata, key, index, &asDouble) != kOfxStatOK)
          return false;
        value = doubleToString(asDouble);
        return true;

      case eMetadataTypeInt :
        if(gPropSuite->propGetInt(metadata, key, index, &asInt) != kOfxStatOK)
          return false;
        value = intToString(asInt);
        return true;

      case eMetadataTypeNone :
        break;
      }

      return false;
    }

    bool readAsDouble(OfxPropertySetHandle metadata, const char *key, MetadataTypeEnum type, int index, double &value)
    {
      std::string asString;
      int asInt = 0;

      switch(type) {
      case eMetadataTypeDouble :
        return gPropSuite->propGetDouble(metadata, key, index, &value) == kOfxStatOK;

      case eMetadataTypeInt :
        if(gPropSuite->propGetInt(metadata, key, index, &asInt) != kOfxStatOK)
          return false;
        value = asInt;
        return true;

      case eMetadataTypeString :
        return readAsString(metadata, key, eMetadataTypeString, index, asString) && stringToDouble(asString, value);

      case eMetadataTypeNone :
        break;
      }

      return false;
    }

    bool readAsInt(OfxPropertySetHandle metadata, const char *key, MetadataTypeEnum type, int index, int &value, int defaultValue)
    {
      double asDouble = 0;

      if(type == eMetadataTypeInt)
        return gPropSuite->propGetInt(metadata, key, index, &value) == kOfxStatOK;

      if(!readAsDouble(metadata, key, type, index, asDouble))
        return false;

      value = doubleToInt(asDouble, defaultValue);
      return true;
    }

  } // anonymous namespace

  MetadataSet::MetadataSet(void)
    : _metadataHandle(0)
    , _owned(true)
  {
  }

  MetadataSet::MetadataSet(OfxPropertySetHandle handle, bool owned)
    : _metadataHandle(handle)
    , _owned(owned)
  {
    if(_metadataHandle) {
      OfxStatus stat = refreshEntries();
      if(stat != kOfxStatOK) {
        reset();
        throwSuiteStatusException(stat);
      }
    }
  }

  MetadataSet::MetadataSet(OfxPropertySetHandle handle)
    : MetadataSet(handle, true)
  {
  }

  MetadataSet::~MetadataSet()
  {
    reset();
  }

  MetadataSet::MetadataSet(MetadataSet &&other) noexcept
    : _metadataHandle(other._metadataHandle)
    , _owned(other._owned)
    , _entries(std::move(other._entries))
  {
    other._metadataHandle = 0;
    other._entries.clear();
  }

  MetadataSet &MetadataSet::operator=(MetadataSet &&other) noexcept
  {
    if(this != &other) {
      reset();
      _metadataHandle = other._metadataHandle;
      _owned = other._owned;
      _entries = std::move(other._entries);
      other._metadataHandle = 0;
      other._entries.clear();
    }
    return *this;
  }

  MetadataSet MetadataSet::fetchFromClip(OfxImageClipHandle clip, OfxTime time)
  {
    if(!gMetadataSuite)
      return MetadataSet();

    OfxPropertySetHandle handle = 0;
    OfxStatus stat = gMetadataSuite->clipGetMetadata(clip, time, &handle);

    if(stat != kOfxStatOK) {
      throwSuiteStatusException(stat);
      return MetadataSet();
    }

    return MetadataSet(handle);
  }

  MetadataSet MetadataSet::fetchFromImage(OfxPropertySetHandle image)
  {
    if(!gMetadataSuite)
      return MetadataSet();

    OfxPropertySetHandle handle = 0;
    OfxStatus stat = gMetadataSuite->imageGetMetadata(image, &handle);

    if(stat != kOfxStatOK) {
      throwSuiteStatusException(stat);
      return MetadataSet();
    }

    return MetadataSet(handle);
  }

  MetadataSet MetadataSet::borrow(OfxPropertySetHandle handle)
  {
    return MetadataSet(handle, false);
  }

  void MetadataSet::reset(void)
  {
    if(_metadataHandle && _owned && gMetadataSuite) {
      OfxStatus stat = gMetadataSuite->metadataRelease(_metadataHandle);
      Log::error(stat != kOfxStatOK, "Failed to release a metadata handle, host returned status %s.", mapStatusToString(stat));
    }
    _metadataHandle = 0;
    _entries.clear();
  }

  OfxStatus MetadataSet::refreshEntries(void) const
  {
    if(!_metadataHandle || !gMetadataSuite)
      return kOfxStatOK;

    std::map<std::string, MetadataEntry> collected;
    OfxStatus stat = gMetadataSuite->metadataEnumerate(_metadataHandle, collectMetadataEntry, &collected);

    if(stat == kOfxStatOK)
      _entries = std::move(collected);

    return stat;
  }

  const MetadataEntry *MetadataSet::findEntry(const std::string &key) const
  {
    if(!_metadataHandle)
      return 0;

    std::map<std::string, MetadataEntry>::const_iterator it = _entries.find(key);
    if(it != _entries.end())
      return &it->second;

    refreshEntries();

    it = _entries.find(key);
    return it != _entries.end() ? &it->second : 0;
  }

  void MetadataSet::forget(const std::string &key)
  {
    _entries.erase(key);
  }

  bool MetadataSet::has(const std::string &key) const
  {
    return findEntry(key) != 0;
  }

  MetadataTypeEnum MetadataSet::getType(const std::string &key) const
  {
    const MetadataEntry *entry = findEntry(key);
    return entry ? entry->type : eMetadataTypeNone;
  }

  int MetadataSet::getDimension(const std::string &key) const
  {
    const MetadataEntry *entry = findEntry(key);
    return entry ? entry->dimension : 0;
  }

  std::string MetadataSet::getString(const std::string &key, int index, const std::string &defaultValue) const
  {
    std::string value;
    const MetadataEntry *entry = index < 0 ? 0 : findEntry(key);

    return entry && readAsString(_metadataHandle, key.c_str(), entry->type, index, value) ? value : defaultValue;
  }

  double MetadataSet::getDouble(const std::string &key, int index, double defaultValue) const
  {
    double value = 0;
    const MetadataEntry *entry = index < 0 ? 0 : findEntry(key);

    return entry && readAsDouble(_metadataHandle, key.c_str(), entry->type, index, value) ? value : defaultValue;
  }

  int MetadataSet::getInt(const std::string &key, int index, int defaultValue) const
  {
    int value = 0;
    const MetadataEntry *entry = index < 0 ? 0 : findEntry(key);

    return entry && readAsInt(_metadataHandle, key.c_str(), entry->type, index, value, defaultValue) ? value : defaultValue;
  }

  std::vector<std::string> MetadataSet::getStringN(const std::string &key) const
  {
    std::vector<std::string> values;
    const MetadataEntry *entry = findEntry(key);

    if(!entry)
      return values;

    for(int i = 0; i < entry->dimension; ++i) {
      std::string value;
      if(!readAsString(_metadataHandle, key.c_str(), entry->type, i, value))
        return std::vector<std::string>();
      values.push_back(value);
    }

    return values;
  }

  std::vector<double> MetadataSet::getDoubleN(const std::string &key) const
  {
    std::vector<double> values;
    const MetadataEntry *entry = findEntry(key);

    if(!entry)
      return values;

    for(int i = 0; i < entry->dimension; ++i) {
      double value = 0;
      if(!readAsDouble(_metadataHandle, key.c_str(), entry->type, i, value))
        return std::vector<double>();
      values.push_back(value);
    }

    return values;
  }

  std::vector<int> MetadataSet::getIntN(const std::string &key) const
  {
    std::vector<int> values;
    const MetadataEntry *entry = findEntry(key);

    if(!entry)
      return values;

    for(int i = 0; i < entry->dimension; ++i) {
      int value = 0;
      if(!readAsInt(_metadataHandle, key.c_str(), entry->type, i, value, 0))
        return std::vector<int>();
      values.push_back(value);
    }

    return values;
  }

  std::vector<MetadataEntry> MetadataSet::entries(void) const
  {
    std::vector<MetadataEntry> result;

    if(!_metadataHandle || !gMetadataSuite)
      return result;

    OfxStatus stat = refreshEntries();

    if(stat != kOfxStatOK) {
      throwSuiteStatusException(stat);
      return result;
    }

    result.reserve(_entries.size());
    for(std::map<std::string, MetadataEntry>::const_iterator it = _entries.begin(); it != _entries.end(); ++it)
      result.push_back(it->second);

    return result;
  }

  std::vector<std::string> MetadataSet::keys(void) const
  {
    const std::vector<MetadataEntry> all = entries();

    std::vector<std::string> result;
    result.reserve(all.size());
    for(size_t i = 0; i < all.size(); ++i)
      result.push_back(all[i].key);

    return result;
  }

  MetadataSetBuilder::MetadataSetBuilder(OfxPropertySetHandle handle)
    : _metadataHandle(handle)
    , _didSomething(false)
    , _contents(MetadataSet::borrow(handle))
  {
  }

  void MetadataSetBuilder::record(const std::string &key, OfxStatus stat)
  {
    Log::error(stat != kOfxStatOK, "Failed to set metadata key %s, host returned status %s.", key.c_str(), mapStatusToString(stat));

    if(stat == kOfxStatOK) {
      _didSomething = true;
      _contents.forget(key);
    }
  }

  void MetadataSetBuilder::setString(const std::string &key, const std::string &value)
  {
    setStringN(key, std::vector<std::string>(1, value));
  }

  void MetadataSetBuilder::setDouble(const std::string &key, double value)
  {
    setDoubleN(key, std::vector<double>(1, value));
  }

  void MetadataSetBuilder::setInt(const std::string &key, int value)
  {
    setIntN(key, std::vector<int>(1, value));
  }

  void MetadataSetBuilder::setStringN(const std::string &key, const std::vector<std::string> &values)
  {
    if(!_metadataHandle || !gMetadataSuite)
      return;

    std::vector<const char *> raw;
    raw.reserve(values.size());
    for(size_t i = 0; i < values.size(); ++i)
      raw.push_back(values[i].c_str());

    record(key, gMetadataSuite->metadataSetStringN(_metadataHandle, key.c_str(), static_cast<int>(values.size()), raw.data()));
  }

  void MetadataSetBuilder::setDoubleN(const std::string &key, const std::vector<double> &values)
  {
    if(!_metadataHandle || !gMetadataSuite)
      return;

    record(key, gMetadataSuite->metadataSetDoubleN(_metadataHandle, key.c_str(), static_cast<int>(values.size()), values.data()));
  }

  void MetadataSetBuilder::setIntN(const std::string &key, const std::vector<int> &values)
  {
    if(!_metadataHandle || !gMetadataSuite)
      return;

    record(key, gMetadataSuite->metadataSetIntN(_metadataHandle, key.c_str(), static_cast<int>(values.size()), values.data()));
  }

  void MetadataSetBuilder::copyFrom(const MetadataSet &source)
  {
    const std::vector<MetadataEntry> allEntries = source.entries();

    for(size_t i = 0; i < allEntries.size(); ++i) {
      const MetadataEntry &entry = allEntries[i];

      switch(entry.type) {
      case eMetadataTypeString :
        setStringN(entry.key, source.getStringN(entry.key));
        break;

      case eMetadataTypeDouble :
        setDoubleN(entry.key, source.getDoubleN(entry.key));
        break;

      case eMetadataTypeInt :
        setIntN(entry.key, source.getIntN(entry.key));
        break;

      case eMetadataTypeNone :
        break;
      }
    }
  }

};
