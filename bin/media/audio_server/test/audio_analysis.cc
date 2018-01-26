// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "audio_analysis.h"
#include <array>
#include "garnet/bin/media/audio_server/gain.h"
#include "garnet/bin/media/audio_server/platform/generic/mixer.h"
#include "garnet/bin/media/audio_server/platform/generic/output_formatter.h"
#include "gtest/gtest.h"
#include "mixer_tests_shared.h"

namespace media {
namespace test {

//
// Subtest utility functions -- used by test functions; can ASSERT on their own.
//

//
// TODO(mpuryear): Consider using the googletest Array Matchers for this
//
// Numerically compare two buffers of integers. Emit values if mismatch found.
// TODO(mpuryear): Add a bool that represents whether we expect failure. Only
// "complain" (FXL_LOG) if unexpected error (or unexpected success!) occurs.
template <typename T>
bool CompareBuffers(const T* actual, const T* expect, uint32_t buf_size) {
  for (uint32_t idx = 0; idx < buf_size; ++idx) {
    if (actual[idx] != expect[idx]) {
      FXL_LOG(ERROR) << "[" << idx << "] was "
                     << static_cast<int32_t>(actual[idx]) << ", should be "
                     << static_cast<int32_t>(expect[idx]);
      return false;
    }
  }
  return true;
}

// Numerically compares a buffer of integers to a specific value.
// TODO(mpuryear): Add a bool that represents whether we expect failure. Only
// "complain" (FXL_LOG) if unexpected error (or unexpected success!) occurs.
template <typename T>
bool CompareBufferToVal(const T* buf, T val, uint32_t buf_size) {
  for (uint32_t idx = 0; idx < buf_size; ++idx) {
    if (buf[idx] != val) {
      FXL_LOG(ERROR) << "[" << idx << "] was " << static_cast<int32_t>(buf[idx])
                     << ", should be " << static_cast<int32_t>(val);
      return false;
    }
  }
  return true;
}

template bool CompareBuffers<int32_t>(const int32_t*, const int32_t*, uint32_t);
template bool CompareBuffers<int16_t>(const int16_t*, const int16_t*, uint32_t);
template bool CompareBuffers<uint8_t>(const uint8_t*, const uint8_t*, uint32_t);
template bool CompareBufferToVal<int32_t>(const int32_t*, int32_t, uint32_t);
template bool CompareBufferToVal<int16_t>(const int16_t*, int16_t, uint32_t);
template bool CompareBufferToVal<uint8_t>(const uint8_t*, uint8_t, uint32_t);

}  // namespace test
}  // namespace media