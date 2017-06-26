// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

namespace media {

// Provides functions that map gain into a linear perceived level for UI.
class PerceivedLevel {
 public:
  // Converts a gain in db to an audio 'level' in the range 0.0 to 1.0
  // inclusive. |gain| values of -60db or less yield level value 0.0. |gain|
  // values of 0db or more yield level value 1.0.
  static float GainToLevel(float gain);

  // Converts an audio 'level' in the range 0.0 to 1.0 inclusive to a gain
  // in db. |level| values of 0.0 or less yield gain value -160db. |level|
  // values slightly more than 0.0 yield gain values slightly more than
  // -60db. |level| values of 1.0 or more yield gain value 0.0db.
  static float LevelToGain(float level);

  // Converts a gain in db to an audio 'level' in the range 0 to |max_level|
  // inclusive. |gain| values of -60db or less yield level value 0. |gain|
  // values of 0db or more yield level value |max_level|. |max_level| must be
  // at least 1.
  static int GainToLevel(float gain, int max_level);

  // Converts an audio 'level' in the range 0 to |max_level| inclusive to a
  // gain in db. |level| values of 0 or less yield gain value -160db. |level|
  // value 1 yields gain value 60 / |max_level| - 60. |level| values of
  // |max_level| or more yield gain value 0.0. |max_level| must be at least 1.
  static float LevelToGain(int level, int max_level);
};

}  // namespace media
