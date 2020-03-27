// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/audio_device.h"

#include <cstring>

#include <gtest/gtest.h>

namespace media::audio {
namespace {

TEST(AudioDeviceTest, UniqueIdFromString) {
  const auto id_result_from_invalid_length = AudioDevice::UniqueIdFromString("efef");
  EXPECT_TRUE(id_result_from_invalid_length.is_error());

  const auto id_result_from_invalid_content =
      AudioDevice::UniqueIdFromString("eeeeeeeeeeeeeeeeeeeeeeeeeeeeee&8");
  EXPECT_TRUE(id_result_from_invalid_content.is_error());

  const audio_stream_unique_id_t unique_id = {.data = {0xff, 0xeb}};
  const auto valid_string = AudioDevice::UniqueIdToString(unique_id);
  const auto id_result_from_valid = AudioDevice::UniqueIdFromString(valid_string);
  EXPECT_TRUE(id_result_from_valid.is_ok());

  EXPECT_EQ(memcmp(id_result_from_valid.value().data, unique_id.data, 16), 0)
      << "Expected: " << valid_string
      << " got: " << AudioDevice::UniqueIdToString(id_result_from_valid.value());
}

TEST(AudioDeviceTest, UniqueIdFromStringMixedCase) {
  const audio_stream_unique_id_t unique_id = {
      .data = {0xff, 0xeb, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}};
  const auto valid_string = "FFeB0000000000000000000000000000";
  const auto id_result_from_valid = AudioDevice::UniqueIdFromString(valid_string);
  EXPECT_TRUE(id_result_from_valid.is_ok());

  EXPECT_EQ(memcmp(id_result_from_valid.value().data, unique_id.data, 16), 0)
      << "Expected: " << valid_string
      << " got: " << AudioDevice::UniqueIdToString(id_result_from_valid.value());
}

}  // namespace
}  // namespace media::audio
