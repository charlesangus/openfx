#ifndef _ofxsMetadata_H_
#define _ofxsMetadata_H_
// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

/** @file This file contains the class that wraps the metadata attached to a clip at a
time, or to an image, and reads values out of it.
*/

#include "ofxMetadata.h"

#include "ofxsCore.h"

#include <string>
#include <vector>

namespace OFX {

  /** @brief Enumerates the types a metadata value can have */
  enum MetadataTypeEnum {
    eMetadataTypeNone,   /**< @brief the key is absent, or the host cannot report types */
    eMetadataTypeInt,
    eMetadataTypeDouble,
    eMetadataTypeString
  };

  /** @brief One key of a metadata set, as reported by MetadataSet::entries */
  struct MetadataEntry {
    std::string      key;
    MetadataTypeEnum type;
    int              dimension;

    MetadataEntry(void) : type(eMetadataTypeNone), dimension(0) {}
    MetadataEntry(const std::string &k, MetadataTypeEnum t, int d) : key(k), type(t), dimension(d) {}
  };

  /** @brief Wraps the metadata of a clip at a time, or of an image, and releases it when it goes out of scope

  A value is read back as the type you ask for rather than as the type the host holds it
  as, so a number reads back as a string and a string reads back as a number wherever the
  text is numeric, and a key which is absent or which will not convert reads back as the
  default you passed.

  A set which carries no metadata is empty rather than an error: it has no keys, and every
  getter returns its default. That is what you get from a clip or an image the host has no
  metadata for, and from any clip or image if the host has no metadata suite at all.

  The methods which read a value never throw. The ones which describe the shape of the set,
  fetchFromClip, fetchFromImage, entries and keys, throw an OFX::Exception::Suite if the
  host fails the underlying call.
  */
  class MetadataSet {
  protected :
    /** @brief The raw metadata property set handle, owned by this object */
    OfxPropertySetHandle _metadataHandle;

  public :
    /** @brief construct an empty set, carrying no metadata */
    MetadataSet(void);

    /** @brief take ownership of a handle returned by the metadata suite */
    explicit MetadataSet(OfxPropertySetHandle handle);

    ~MetadataSet();

    MetadataSet(MetadataSet &&other) noexcept;
    MetadataSet &operator=(MetadataSet &&other) noexcept;

    MetadataSet(const MetadataSet &) = delete;
    MetadataSet &operator=(const MetadataSet &) = delete;

    /** @brief fetch the metadata a clip carries at the given time */
    static MetadataSet fetchFromClip(OfxImageClipHandle clip, OfxTime time);

    /** @brief fetch the metadata an image carries, the image handle already naming a time */
    static MetadataSet fetchFromImage(OfxPropertySetHandle image);

    /** @brief does this set carry metadata at all */
    bool isValid(void) const {return _metadataHandle != 0;}

    /** @brief the raw handle, for a plugin which needs to call the suites directly */
    OfxPropertySetHandle getHandle(void) const {return _metadataHandle;}

    /** @brief release the metadata now rather than at the end of the scope, leaving the set empty */
    void reset(void);

    /** @brief is the key present in this set */
    bool has(const std::string &key) const;

    /** @brief the type the host holds the key as, eMetadataTypeNone if it is absent or the host cannot say */
    MetadataTypeEnum getType(const std::string &key) const;

    /** @brief how many values the key has, 0 if it is absent */
    int getDimension(const std::string &key) const;

    /** @brief the key's value at index as a string, defaultValue if it is absent or will not convert */
    std::string getString(const std::string &key, int index = 0, const std::string &defaultValue = std::string()) const;

    /** @brief the key's value at index as a double, defaultValue if it is absent or will not convert */
    double getDouble(const std::string &key, int index = 0, double defaultValue = 0) const;

    /** @brief the key's value at index as an int, defaultValue if it is absent or will not convert */
    int getInt(const std::string &key, int index = 0, int defaultValue = 0) const;

    /** @brief all of the key's values as strings, empty if it is absent or will not convert */
    std::vector<std::string> getStringN(const std::string &key) const;

    /** @brief all of the key's values as doubles, empty if it is absent or will not convert */
    std::vector<double> getDoubleN(const std::string &key) const;

    /** @brief all of the key's values as ints, empty if it is absent or will not convert */
    std::vector<int> getIntN(const std::string &key) const;

    /** @brief every key in the set with its type and dimension, in ascending key order */
    std::vector<MetadataEntry> entries(void) const;

    /** @brief every key in the set, in ascending key order */
    std::vector<std::string> keys(void) const;
  };

};

#endif
