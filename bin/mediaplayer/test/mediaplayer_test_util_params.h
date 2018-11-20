// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIAPLAYER_TEST_MEDIAPLAYER_TEST_UTIL_PARAMS_H_
#define GARNET_BIN_MEDIAPLAYER_TEST_MEDIAPLAYER_TEST_UTIL_PARAMS_H_

#include <string>
#include <vector>
#include "lib/fxl/command_line.h"
#include "lib/fxl/macros.h"

namespace media_player {
namespace test {

class MediaPlayerTestUtilParams {
 public:
  MediaPlayerTestUtilParams(const fxl::CommandLine& command_line);

  bool is_valid() const { return is_valid_; }

  bool play() const { return play_; }

  bool loop() const { return loop_; }

  bool test_seek() const { return test_seek_; }

  bool auto_play() const { return play_ || loop_ || test_seek_; }

  const std::vector<std::string>& urls() const { return urls_; }

 private:
  void Usage();

  bool is_valid_;

  std::vector<std::string> urls_;
  bool play_ = false;
  bool loop_ = false;
  bool test_seek_ = false;

  FXL_DISALLOW_COPY_AND_ASSIGN(MediaPlayerTestUtilParams);
};

}  // namespace test
}  // namespace media_player

#endif  // GARNET_BIN_MEDIAPLAYER_TEST_MEDIAPLAYER_TEST_UTIL_PARAMS_H_
