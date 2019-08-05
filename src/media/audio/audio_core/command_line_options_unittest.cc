// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/command_line_options.h"

#include <gtest/gtest.h>

#include "src/lib/fxl/arraysize.h"

namespace media::audio {
namespace {

TEST(CommandLineOptionsTest, DefaultValues) {
  const char* argv[] = {"audio_core"};
  auto result = CommandLineOptions::ParseFromArgcArgv(arraysize(argv), argv);

  ASSERT_TRUE(result.is_ok());
  ASSERT_EQ(true, result.value().enable_device_settings_writeback);
}

TEST(CommandLineOptionsTest, ParseArgs) {
  const char* argv[] = {"audio_core", "--disable-device-settings-writeback"};
  auto result = CommandLineOptions::ParseFromArgcArgv(arraysize(argv), argv);

  ASSERT_TRUE(result.is_ok());
  ASSERT_EQ(false, result.value().enable_device_settings_writeback);
}

TEST(CommandLineOptionsTest, RejectFlagValues) {
  // We won't parse anything after the '='. Ensure we don't accept these strings as they could
  // cause surprising behavior.
  const char* argv[] = {"audio_core", "--disable-device-settings-writeback=false"};
  auto result = CommandLineOptions::ParseFromArgcArgv(arraysize(argv), argv);

  ASSERT_FALSE(result.is_ok());
}

TEST(CommandLineOptionsTest, RejectUnknownFlags) {
  const char* argv[] = {"audio_core", "--unknown"};
  auto result = CommandLineOptions::ParseFromArgcArgv(arraysize(argv), argv);

  ASSERT_FALSE(result.is_ok());
}

TEST(CommandLineOptionsTest, RejectPositionalArgs) {
  const char* argv[] = {"audio_core", "positional_arg"};
  auto result = CommandLineOptions::ParseFromArgcArgv(arraysize(argv), argv);

  ASSERT_FALSE(result.is_ok());
}

}  // namespace
}  // namespace media::audio
