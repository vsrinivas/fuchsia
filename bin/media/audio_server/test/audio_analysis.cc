// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "audio_analysis.h"

#include "lib/fxl/logging.h"

namespace media {
namespace test {

//
// Subtest utility functions -- used by test functions; can ASSERT on their own.
//

//
// TODO(mpuryear): Consider using the googletest Array Matchers for this
//
// Numerically compare two buffers of integers. Emit values if mismatch found.
// For testability, last param bool represents whether we expect comp to fail
template <typename T>
bool CompareBuffers(const T* actual,
                    const T* expect,
                    uint32_t buf_size,
                    bool expect_to_pass) {
  for (uint32_t idx = 0; idx < buf_size; ++idx) {
    if (actual[idx] != expect[idx]) {
      if (expect_to_pass) {
        FXL_LOG(ERROR) << "[" << idx << "] was "
                       << static_cast<int32_t>(actual[idx]) << ", should be "
                       << static_cast<int32_t>(expect[idx]);
      }
      return false;
    }
  }
  if (!expect_to_pass) {
    FXL_LOG(ERROR) << "We expected two buffers (length " << buf_size
                   << ") to differ, but they did not!";
  }
  return true;
}

// Numerically compares a buffer of integers to a specific value.
// For testability, last param bool represents whether we expect comp to fail
template <typename T>
bool CompareBufferToVal(const T* buf,
                        T val,
                        uint32_t buf_size,
                        bool expect_to_pass) {
  for (uint32_t idx = 0; idx < buf_size; ++idx) {
    if (buf[idx] != val) {
      if (expect_to_pass) {
        FXL_LOG(ERROR) << "[" << idx << "] was "
                       << static_cast<int32_t>(buf[idx]) << ", should be "
                       << static_cast<int32_t>(val);
      }
      return false;
    }
  }
  if (!expect_to_pass) {
    FXL_LOG(ERROR) << "We expected buffer (length " << buf_size
                   << ") to differ from value " << static_cast<int32_t>(val)
                   << ", but it was equal!";
  }
  return true;
}

template bool CompareBuffers<int32_t>(const int32_t*,
                                      const int32_t*,
                                      uint32_t,
                                      bool);
template bool CompareBuffers<int16_t>(const int16_t*,
                                      const int16_t*,
                                      uint32_t,
                                      bool);
template bool CompareBuffers<uint8_t>(const uint8_t*,
                                      const uint8_t*,
                                      uint32_t,
                                      bool);
template bool CompareBufferToVal<int32_t>(const int32_t*,
                                          int32_t,
                                          uint32_t,
                                          bool);
template bool CompareBufferToVal<int16_t>(const int16_t*,
                                          int16_t,
                                          uint32_t,
                                          bool);
template bool CompareBufferToVal<uint8_t>(const uint8_t*,
                                          uint8_t,
                                          uint32_t,
                                          bool);

}  // namespace test
}  // namespace media
