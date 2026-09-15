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

    /** @brief writes value at the fewest of 15, 16 or 17 significant digits which reads
    back as the same double, else at 17, in the classic locale so that the decimal point
    does not follow whatever locale the host happens to have installed */
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
  {
  }

  MetadataSet::MetadataSet(OfxPropertySetHandle handle)
    : _metadataHandle(handle)
  {
    if(_metadataHandle) {
      OfxStatus stat = refreshEntries();
      if(stat != kOfxStatOK) {
        reset();
        throwSuiteStatusException(stat);
      }
    }
  }

  MetadataSet::~MetadataSet()
  {
    reset();
  }

  MetadataSet::MetadataSet(MetadataSet &&other) noexcept
    : _metadataHandle(other._metadataHandle)
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

  void MetadataSet::reset(void)
  {
    if(_metadataHandle && gMetadataSuite) {
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

};
