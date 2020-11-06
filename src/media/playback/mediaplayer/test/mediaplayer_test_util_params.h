// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TEST_MEDIAPLAYER_TEST_UTIL_PARAMS_H_
#define SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TEST_MEDIAPLAYER_TEST_UTIL_PARAMS_H_

#include <string>
#include <vector>

#include "src/lib/fxl/command_line.h"

namespace media_player {
namespace test {

class MediaPlayerTestUtilParams {
 public:
  MediaPlayerTestUtilParams(const fxl::CommandLine& command_line);

  bool is_valid() const { return is_valid_; }

  bool play() const { return play_; }

  bool loop() const { return loop_; }

  bool test_seek() const { return test_seek_; }

  // --experiment is intended for ad-hoc use when a developer wants to drop in
  // e.g. a repro test. No implementation of it should be submitted, and it
  // should not appear in the usage message.
  bool experiment() const { return experiment_; }

  bool auto_play() const { return play_ || loop_ || test_seek_; }

  const std::vector<std::string>& paths() const { return paths_; }

  float rate() const { return rate_; }

 private:
  void Usage();

  bool is_valid_;

  std::vector<std::string> paths_;
  bool play_ = false;
  bool loop_ = false;
  bool test_seek_ = false;
  bool experiment_ = false;
  float rate_ = 1.0f;

  // Disallow copy, assign and move.
  MediaPlayerTestUtilParams(const MediaPlayerTestUtilParams&) = delete;
  MediaPlayerTestUtilParams(MediaPlayerTestUtilParams&&) = delete;
  MediaPlayerTestUtilParams& operator=(const MediaPlayerTestUtilParams&) = delete;
  MediaPlayerTestUtilParams& operator=(MediaPlayerTestUtilParams&&) = delete;
};

}  // namespace test
}  // namespace media_player

#endif  // SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TEST_MEDIAPLAYER_TEST_UTIL_PARAMS_H_
