// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef METADATA_HOST_FIXTURE_H
#define METADATA_HOST_FIXTURE_H

#include "ofxCore.h"
#include "ofxImageEffect.h"
#include "ofxMetadata.h"

namespace MetadataFixture {

  enum ValueType {
    eString, ///< read with propGetString, stringValue holds the value
    eDouble, ///< read with propGetDouble, doubleValue holds the value
    eInt     ///< read with propGetIntN, intValues holds intCount values
  };

  /// an entry carrying this time applies at every frame of the fixture's range,
  /// rather than at one of them
  const OfxTime kAnyTime = -1;

  /// the largest number of ints an entry can carry
  const int kMaxInts = 4;

  struct Entry {
    const char *clip;
    const char *key;
    ValueType   type;
    OfxTime     time;
    const char *stringValue;
    double      doubleValue;
    int         intValues[kMaxInts];
    int         intCount;
  };

  const OfxTime kFirstFrame = 1;
  const OfxTime kLastFrame  = 3;

  /// the clips the harness wires up, the last of which is the effect's output and
  /// carries no metadata of its own
  const char *const kInputClips[] = {"Source", "Mask"};
  const int kInputClipCount = sizeof(kInputClips) / sizeof(kInputClips[0]);
  const char kOutputClip[] = kOfxImageEffectOutputClipName;

  /// the standard vocabulary has no int X N key and reserves its own prefix, so the
  /// array case is carried under a reverse DNS name, as the standard requires of every
  /// key it does not define itself
  const char kDataWindowKey[] = "org.openfx.examples.metadataHost.dataWindow";

  const char kSourceMovie[] = "/shots/ab_010/plate/ab_010_plate.mov";

  /// Source is a movie, so it carries one path at every frame and a timecode that
  /// advances a frame at a time; Mask is a numbered sequence, so its path and its
  /// source frame number both advance instead.
  const Entry kEntries[] = {
    {"Source", kOfxMetadataKeyFilePath,    eString, kAnyTime, kSourceMovie,  0,    {0},                0},
    {"Source", kOfxMetadataKeyFrameRate,   eDouble, kAnyTime, 0,             24.0, {0},                0},
    {"Source", kOfxMetadataKeySampleType,  eString, kAnyTime, "float",       0,    {0},                0},
    {"Source", kOfxMetadataKeyBitDepth,    eInt,    kAnyTime, 0,             0,    {16},               1},
    {"Source", kDataWindowKey,             eInt,    kAnyTime, 0,             0,    {0, 0, 1920, 1080}, 4},
    {"Source", kOfxMetadataKeyTimecode,    eString, 1,        "01:00:00:00", 0,    {0},                0},
    {"Source", kOfxMetadataKeyTimecode,    eString, 2,        "01:00:00:01", 0,    {0},                0},
    {"Source", kOfxMetadataKeyTimecode,    eString, 3,        "01:00:00:02", 0,    {0},                0},
    {"Source", kOfxMetadataKeySourceFrame, eInt,    1,        0,             0,    {100},              1},
    {"Source", kOfxMetadataKeySourceFrame, eInt,    2,        0,             0,    {101},              1},
    {"Source", kOfxMetadataKeySourceFrame, eInt,    3,        0,             0,    {102},              1},

    {"Mask",   kOfxMetadataKeySampleType,  eString, kAnyTime, "uint",        0,    {0},                0},
    {"Mask",   kOfxMetadataKeyBitDepth,    eInt,    kAnyTime, 0,             0,    {8},                1},
    {"Mask",   kOfxMetadataKeyFilePath,    eString, 1,        "/shots/ab_010/mask/ab_010_mask.0087.exr", 0, {0}, 0},
    {"Mask",   kOfxMetadataKeyFilePath,    eString, 2,        "/shots/ab_010/mask/ab_010_mask.0088.exr", 0, {0}, 0},
    {"Mask",   kOfxMetadataKeyFilePath,    eString, 3,        "/shots/ab_010/mask/ab_010_mask.0089.exr", 0, {0}, 0},
    {"Mask",   kOfxMetadataKeySourceFrame, eInt,    1,        0,             0,    {87},               1},
    {"Mask",   kOfxMetadataKeySourceFrame, eInt,    2,        0,             0,    {88},               1},
    {"Mask",   kOfxMetadataKeySourceFrame, eInt,    3,        0,             0,    {89},               1}
  };

  const int kEntryCount = sizeof(kEntries) / sizeof(kEntries[0]);
}

#endif // METADATA_HOST_FIXTURE_H
