// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

namespace media {

// Provides functions that map gain into a linear perceived level for UI.
class PerceivedLevel {
 public:
  // Converts a gain in db to an audio 'level' in the range 0.0 to 1.0
  // inclusive.
  static float GainToLevel(float gain);

  // Converts an audio 'level' in the range 0.0 to 1.0 inclusive to a gain
  // in db.
  static float LevelToGain(float level);

  // Converts a gain in db to an audio 'level' in the range 0 to |max_level|
  // inclusive.
  static int GainToLevel(float gain, int max_level);

  // Converts an audio 'level' in the range 0 to |max_level| inclusive to a gain
  // in db.
  static float LevelToGain(int level, int max_level);
};

}  // namespace media
