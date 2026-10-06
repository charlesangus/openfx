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

  /// the suite defines no keys, so these names are the fixture's own. The example
  /// plugins whose defaults read or write a key the fixture publishes default to the
  /// same name, so their contracts can run without being told it
  const char kFilePathKey[]    = "file_path";
  const char kFrameRateKey[]   = "frame_rate";
  const char kSampleTypeKey[]  = "sample_type";
  const char kBitDepthKey[]    = "bit_depth";
  const char kTimecodeKey[]    = "timecode";
  const char kSourceFrameKey[] = "source_frame";

  /// the int X N case
  const char kDataWindowKey[] = "data_window";

  /// its value is a realistic path that happens to carry a bare '%' (an opacity folder)
  /// and a '%s' (an unsubstituted shot-name token), so it catches a plugin that passes
  /// metadata text as a printf format rather than an arg
  const char kBurnInTemplateKey[] = "burn_in_template";

  /// the keys metadataPlugin retains from each of its inputs, which it lists itself;
  /// every other key the fixture publishes is one it drops
  const char *const kRetainedKeys[] = {
    kFilePathKey, kFrameRateKey, kSampleTypeKey, kBitDepthKey, kTimecodeKey, kSourceFrameKey
  };
  const int kRetainedKeyCount = sizeof(kRetainedKeys) / sizeof(kRetainedKeys[0]);

  const char kSourceMovie[] = "/shots/ab_010/plate/ab_010_plate.mov";
  const char kBurnInTemplate[] = "/shots/ab_010/burnin/50%/ab_010_%s.txt";

  /// Source is a movie, so it carries one path at every frame and a timecode that
  /// advances a frame at a time; Mask is a numbered sequence, so its path and its
  /// source frame number both advance instead.
  const Entry kEntries[] = {
    {"Source", kFilePathKey,       eString, kAnyTime, kSourceMovie,  0,    {0},                0},
    {"Source", kFrameRateKey,      eDouble, kAnyTime, 0,             24.0, {0},                0},
    {"Source", kSampleTypeKey,     eString, kAnyTime, "float",       0,    {0},                0},
    {"Source", kBitDepthKey,       eInt,    kAnyTime, 0,             0,    {16},               1},
    {"Source", kDataWindowKey,     eInt,    kAnyTime, 0,             0,    {0, 0, 1920, 1080}, 4},
    {"Source", kBurnInTemplateKey, eString, kAnyTime, kBurnInTemplate, 0,   {0},                0},
    {"Source", kTimecodeKey,       eString, 1,        "01:00:00:00", 0,    {0},                0},
    {"Source", kTimecodeKey,       eString, 2,        "01:00:00:01", 0,    {0},                0},
    {"Source", kTimecodeKey,       eString, 3,        "01:00:00:02", 0,    {0},                0},
    {"Source", kSourceFrameKey,    eInt,    1,        0,             0,    {100},              1},
    {"Source", kSourceFrameKey,    eInt,    2,        0,             0,    {101},              1},
    {"Source", kSourceFrameKey,    eInt,    3,        0,             0,    {102},              1},

    {"Mask",   kSampleTypeKey,     eString, kAnyTime, "uint",        0,    {0},                0},
    {"Mask",   kBitDepthKey,       eInt,    kAnyTime, 0,             0,    {8},                1},
    {"Mask",   kFilePathKey,       eString, 1,        "/shots/ab_010/mask/ab_010_mask.0087.exr", 0, {0}, 0},
    {"Mask",   kFilePathKey,       eString, 2,        "/shots/ab_010/mask/ab_010_mask.0088.exr", 0, {0}, 0},
    {"Mask",   kFilePathKey,       eString, 3,        "/shots/ab_010/mask/ab_010_mask.0089.exr", 0, {0}, 0},
    {"Mask",   kSourceFrameKey,    eInt,    1,        0,             0,    {87},               1},
    {"Mask",   kSourceFrameKey,    eInt,    2,        0,             0,    {88},               1},
    {"Mask",   kSourceFrameKey,    eInt,    3,        0,             0,    {89},               1}
  };

  const int kEntryCount = sizeof(kEntries) / sizeof(kEntries[0]);
}

#endif // METADATA_HOST_FIXTURE_H
