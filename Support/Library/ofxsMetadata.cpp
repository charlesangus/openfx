// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#include "ofxsSupportPrivate.h"
#include "ofxsMetadata.h"

#include <algorithm>
#include <climits>
#include <iomanip>
#include <locale>
#include <sstream>

using namespace OFX::Private;

namespace OFX {

  namespace {

    OfxStatus collectMetadataKey(const char *key, void *userData)
    {
      // the host calls this through a C function pointer, so nothing may propagate out of it
      try {
        ((std::vector<std::string> *) userData)->push_back(key);
      }
      catch(...) {
        return kOfxStatErrMemory;
      }
      return kOfxStatOK;
    }

    MetadataTypeEnum mapDataType(OfxPropDataType type)
    {
      switch(type) {
      case kOfxPropDataTypeString  : return eMetadataTypeString;
      case kOfxPropDataTypeDouble  : return eMetadataTypeDouble;
      case kOfxPropDataTypeInteger : return eMetadataTypeInt;
      default : return eMetadataTypeNone;
      }
    }

    /** @brief writes value as the shortest text which reads back as the same double, in
    the classic locale so that the decimal point does not follow whatever locale the host
    happens to have installed */
    std::string doubleToString(double value)
    {
      for(int precision = 15; precision <= 17; ++precision) {
        std::ostringstream os;
        os.imbue(std::locale::classic());
        os << std::setprecision(precision) << value;

        std::istringstream is(os.str());
        is.imbue(std::locale::classic());

        double roundTripped = 0;
        is >> roundTripped;

        if(!is.fail() && roundTripped == value)
          return os.str();
      }

      std::ostringstream os;
      os.imbue(std::locale::classic());
      os << std::setprecision(17) << value;
      return os.str();
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

    bool readRawString(OfxPropertySetHandle metadata, const char *key, int index, std::string &value)
    {
      char *raw = 0;
      if(gPropSuite->propGetString(metadata, key, index, &raw) != kOfxStatOK || !raw)
        return false;
      value = raw;
      return true;
    }

    bool readRawDouble(OfxPropertySetHandle metadata, const char *key, int index, double &value)
    {
      return gPropSuite->propGetDouble(metadata, key, index, &value) == kOfxStatOK;
    }

    bool readRawInt(OfxPropertySetHandle metadata, const char *key, int index, int &value)
    {
      return gPropSuite->propGetInt(metadata, key, index, &value) == kOfxStatOK;
    }

    bool readAsString(OfxPropertySetHandle metadata, const char *key, MetadataTypeEnum type, int index, std::string &value)
    {
      double asDouble = 0;
      int asInt = 0;

      switch(type) {
      case eMetadataTypeString :
        return readRawString(metadata, key, index, value);

      case eMetadataTypeDouble :
        if(!readRawDouble(metadata, key, index, asDouble))
          return false;
        value = doubleToString(asDouble);
        return true;

      case eMetadataTypeInt :
        if(!readRawInt(metadata, key, index, asInt))
          return false;
        value = intToString(asInt);
        return true;

      case eMetadataTypeNone :
        break;
      }

      // the host cannot say what the key is, so try each type in turn
      if(readRawString(metadata, key, index, value))
        return true;
      if(readRawDouble(metadata, key, index, asDouble)) {
        value = doubleToString(asDouble);
        return true;
      }
      if(readRawInt(metadata, key, index, asInt)) {
        value = intToString(asInt);
        return true;
      }
      return false;
    }

    bool readAsDouble(OfxPropertySetHandle metadata, const char *key, MetadataTypeEnum type, int index, double &value)
    {
      std::string asString;
      int asInt = 0;

      switch(type) {
      case eMetadataTypeDouble :
        return readRawDouble(metadata, key, index, value);

      case eMetadataTypeInt :
        if(!readRawInt(metadata, key, index, asInt))
          return false;
        value = asInt;
        return true;

      case eMetadataTypeString :
        return readRawString(metadata, key, index, asString) && stringToDouble(asString, value);

      case eMetadataTypeNone :
        break;
      }

      if(readRawDouble(metadata, key, index, value))
        return true;
      if(readRawInt(metadata, key, index, asInt)) {
        value = asInt;
        return true;
      }
      return readRawString(metadata, key, index, asString) && stringToDouble(asString, value);
    }

    bool readAsInt(OfxPropertySetHandle metadata, const char *key, MetadataTypeEnum type, int index, int &value, int defaultValue)
    {
      double asDouble = 0;

      if(type == eMetadataTypeInt)
        return readRawInt(metadata, key, index, value);

      if(type == eMetadataTypeNone && readRawInt(metadata, key, index, value))
        return true;

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
  }

  MetadataSet::~MetadataSet()
  {
    reset();
  }

  MetadataSet::MetadataSet(MetadataSet &&other) noexcept
    : _metadataHandle(other._metadataHandle)
  {
    other._metadataHandle = 0;
  }

  MetadataSet &MetadataSet::operator=(MetadataSet &&other) noexcept
  {
    if(this != &other) {
      reset();
      _metadataHandle = other._metadataHandle;
      other._metadataHandle = 0;
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
  }

  bool MetadataSet::has(const std::string &key) const
  {
    if(!_metadataHandle)
      return false;

    int dimension = 0;
    return gPropSuite->propGetDimension(_metadataHandle, key.c_str(), &dimension) == kOfxStatOK;
  }

  MetadataTypeEnum MetadataSet::getType(const std::string &key) const
  {
    if(!_metadataHandle || !gPropSuiteV2)
      return eMetadataTypeNone;

    OfxPropDataType type = kOfxPropDataTypeNone;
    if(gPropSuiteV2->propGetType(_metadataHandle, key.c_str(), &type) != kOfxStatOK)
      return eMetadataTypeNone;

    return mapDataType(type);
  }

  int MetadataSet::getDimension(const std::string &key) const
  {
    if(!_metadataHandle)
      return 0;

    int dimension = 0;
    if(gPropSuite->propGetDimension(_metadataHandle, key.c_str(), &dimension) != kOfxStatOK)
      return 0;

    return dimension < 0 ? 0 : dimension;
  }

  std::string MetadataSet::getString(const std::string &key, int index, const std::string &defaultValue) const
  {
    std::string value;

    if(!_metadataHandle || index < 0)
      return defaultValue;

    if(!readAsString(_metadataHandle, key.c_str(), getType(key), index, value))
      return defaultValue;

    return value;
  }

  double MetadataSet::getDouble(const std::string &key, int index, double defaultValue) const
  {
    double value = 0;

    if(!_metadataHandle || index < 0)
      return defaultValue;

    if(!readAsDouble(_metadataHandle, key.c_str(), getType(key), index, value))
      return defaultValue;

    return value;
  }

  int MetadataSet::getInt(const std::string &key, int index, int defaultValue) const
  {
    int value = 0;

    if(!_metadataHandle || index < 0)
      return defaultValue;

    if(!readAsInt(_metadataHandle, key.c_str(), getType(key), index, value, defaultValue))
      return defaultValue;

    return value;
  }

  std::vector<std::string> MetadataSet::getStringN(const std::string &key) const
  {
    std::vector<std::string> values;
    const MetadataTypeEnum type = getType(key);
    const int dimension = getDimension(key);

    for(int i = 0; i < dimension; ++i) {
      std::string value;
      if(!readAsString(_metadataHandle, key.c_str(), type, i, value))
        return std::vector<std::string>();
      values.push_back(value);
    }

    return values;
  }

  std::vector<double> MetadataSet::getDoubleN(const std::string &key) const
  {
    std::vector<double> values;
    const MetadataTypeEnum type = getType(key);
    const int dimension = getDimension(key);

    for(int i = 0; i < dimension; ++i) {
      double value = 0;
      if(!readAsDouble(_metadataHandle, key.c_str(), type, i, value))
        return std::vector<double>();
      values.push_back(value);
    }

    return values;
  }

  std::vector<int> MetadataSet::getIntN(const std::string &key) const
  {
    std::vector<int> values;
    const MetadataTypeEnum type = getType(key);
    const int dimension = getDimension(key);

    for(int i = 0; i < dimension; ++i) {
      int value = 0;
      if(!readAsInt(_metadataHandle, key.c_str(), type, i, value, 0))
        return std::vector<int>();
      values.push_back(value);
    }

    return values;
  }

  std::vector<MetadataEntry> MetadataSet::entries(void) const
  {
    const std::vector<std::string> sorted = keys();
    std::vector<MetadataEntry> entries;

    entries.reserve(sorted.size());

    for(size_t i = 0; i < sorted.size(); ++i)
      entries.push_back(MetadataEntry(sorted[i], getType(sorted[i]), getDimension(sorted[i])));

    return entries;
  }

  std::vector<std::string> MetadataSet::keys(void) const
  {
    std::vector<std::string> keys;

    if(!_metadataHandle || !gMetadataSuite)
      return keys;

    OfxStatus stat = gMetadataSuite->metadataEnumerate(_metadataHandle, collectMetadataKey, &keys);

    if(stat != kOfxStatOK) {
      throwSuiteStatusException(stat);
      return std::vector<std::string>();
    }

    // the suite guarantees no order, so one is imposed here rather than passed on
    std::sort(keys.begin(), keys.end());

    return keys;
  }

};
