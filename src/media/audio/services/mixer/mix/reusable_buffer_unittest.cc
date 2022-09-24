// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/mix/reusable_buffer.h"

#include <ffl/string.h>
#include <gtest/gtest.h>

#include "src/lib/fxl/strings/string_printf.h"
#include "src/media/audio/lib/format2/fixed.h"
#include "src/media/audio/lib/format2/format.h"

namespace media_audio {
namespace {

using ::fuchsia_audio::SampleType;

const auto kFormatOneChan = Format::CreateOrDie({SampleType::kInt16, 1, 48000});
const auto kFormatTwoChan = Format::CreateOrDie({SampleType::kInt16, 2, 48000});

TEST(ReusableBufferTest, AppendDataOneChan) {
  ReusableBuffer buffer(kFormatOneChan, 20);
  std::vector<int16_t> payload1{1, 2, 3, 4, 5};
  std::vector<int16_t> payload2{6, 7, 8, 9, 10};
  std::vector<int16_t> payload3{11, 12, 13, 14, 15};

  // Starts empty.
  // Can call these before Reset().
  EXPECT_EQ(buffer.length(), 0);
  EXPECT_EQ(buffer.capacity(), 20);
  EXPECT_TRUE(buffer.empty());

  // Must call these after Reset().
  buffer.Reset(Fixed(0));
  EXPECT_EQ(buffer.start(), Fixed(0));
  EXPECT_EQ(buffer.end(), Fixed(0));
  EXPECT_EQ(buffer.length(), 0);
  EXPECT_TRUE(buffer.empty());

  buffer.AppendData(Fixed(0), static_cast<int64_t>(payload1.size()), payload1.data());
  EXPECT_EQ(buffer.start(), Fixed(0)) << "start = " << ffl::String(buffer.start()).c_str();
  EXPECT_EQ(buffer.end(), Fixed(5)) << "end = " << ffl::String(buffer.end()).c_str();
  EXPECT_EQ(buffer.length(), 5);
  EXPECT_FALSE(buffer.empty());
  EXPECT_EQ(reinterpret_cast<int16_t*>(buffer.payload())[0], 1);
  EXPECT_EQ(reinterpret_cast<int16_t*>(buffer.payload())[4], 5);

  // Append without a gap.
  buffer.AppendData(Fixed(5), static_cast<int64_t>(payload2.size()), payload2.data());
  EXPECT_EQ(buffer.start(), Fixed(0)) << "start = " << ffl::String(buffer.start()).c_str();
  EXPECT_EQ(buffer.end(), Fixed(10)) << "end = " << ffl::String(buffer.end()).c_str();
  EXPECT_EQ(buffer.length(), 10);
  EXPECT_FALSE(buffer.empty());
  EXPECT_EQ(reinterpret_cast<int16_t*>(buffer.payload())[0], 1);
  EXPECT_EQ(reinterpret_cast<int16_t*>(buffer.payload())[4], 5);
  EXPECT_EQ(reinterpret_cast<int16_t*>(buffer.payload())[5], 6);
  EXPECT_EQ(reinterpret_cast<int16_t*>(buffer.payload())[9], 10);

  // Append with a gap.
  buffer.AppendData(Fixed(15), static_cast<int64_t>(payload3.size()), payload3.data());
  EXPECT_EQ(buffer.start(), Fixed(0)) << "start = " << ffl::String(buffer.start()).c_str();
  EXPECT_EQ(buffer.end(), Fixed(20)) << "end = " << ffl::String(buffer.end()).c_str();
  EXPECT_EQ(buffer.length(), 20);
  EXPECT_FALSE(buffer.empty());
  EXPECT_EQ(reinterpret_cast<int16_t*>(buffer.payload())[0], 1);
  EXPECT_EQ(reinterpret_cast<int16_t*>(buffer.payload())[4], 5);
  EXPECT_EQ(reinterpret_cast<int16_t*>(buffer.payload())[5], 6);
  EXPECT_EQ(reinterpret_cast<int16_t*>(buffer.payload())[9], 10);
  EXPECT_EQ(reinterpret_cast<int16_t*>(buffer.payload())[10], 0);
  EXPECT_EQ(reinterpret_cast<int16_t*>(buffer.payload())[14], 0);
  EXPECT_EQ(reinterpret_cast<int16_t*>(buffer.payload())[15], 11);
  EXPECT_EQ(reinterpret_cast<int16_t*>(buffer.payload())[19], 15);
}

TEST(ReusableBufferTest, AppendSilenceOneChan) {
  ReusableBuffer buffer(kFormatOneChan, 25);
  std::vector<int16_t> payload{1, 2, 3, 4, 5};

  buffer.Reset(Fixed(0));
  EXPECT_EQ(buffer.start(), Fixed(0));
  EXPECT_EQ(buffer.end(), Fixed(0));
  EXPECT_EQ(buffer.length(), 0);
  EXPECT_TRUE(buffer.empty());

  buffer.AppendSilence(Fixed(0), 5);
  EXPECT_EQ(buffer.start(), Fixed(0)) << "start = " << ffl::String(buffer.start()).c_str();
  EXPECT_EQ(buffer.end(), Fixed(5)) << "end = " << ffl::String(buffer.end()).c_str();
  EXPECT_EQ(buffer.length(), 5);
  EXPECT_FALSE(buffer.empty());
  EXPECT_EQ(reinterpret_cast<int16_t*>(buffer.payload())[0], 0);
  EXPECT_EQ(reinterpret_cast<int16_t*>(buffer.payload())[4], 0);

  buffer.AppendData(Fixed(5), static_cast<int64_t>(payload.size()), payload.data());
  EXPECT_EQ(buffer.start(), Fixed(0)) << "start = " << ffl::String(buffer.start()).c_str();
  EXPECT_EQ(buffer.end(), Fixed(10)) << "end = " << ffl::String(buffer.end()).c_str();
  EXPECT_EQ(buffer.length(), 10);
  EXPECT_FALSE(buffer.empty());
  EXPECT_EQ(reinterpret_cast<int16_t*>(buffer.payload())[0], 0);
  EXPECT_EQ(reinterpret_cast<int16_t*>(buffer.payload())[4], 0);
  EXPECT_EQ(reinterpret_cast<int16_t*>(buffer.payload())[5], 1);
  EXPECT_EQ(reinterpret_cast<int16_t*>(buffer.payload())[9], 5);

  // Skip [10, 15).
  buffer.AppendSilence(Fixed(15), 5);
  buffer.AppendSilence(Fixed(20), 5);
  EXPECT_EQ(buffer.start(), Fixed(0)) << "start = " << ffl::String(buffer.start()).c_str();
  EXPECT_EQ(buffer.end(), Fixed(25)) << "end = " << ffl::String(buffer.end()).c_str();
  EXPECT_EQ(buffer.length(), 25);
  EXPECT_FALSE(buffer.empty());
  EXPECT_EQ(reinterpret_cast<int16_t*>(buffer.payload())[0], 0);
  EXPECT_EQ(reinterpret_cast<int16_t*>(buffer.payload())[4], 0);
  EXPECT_EQ(reinterpret_cast<int16_t*>(buffer.payload())[5], 1);
  EXPECT_EQ(reinterpret_cast<int16_t*>(buffer.payload())[9], 5);
  EXPECT_EQ(reinterpret_cast<int16_t*>(buffer.payload())[10], 0);
  EXPECT_EQ(reinterpret_cast<int16_t*>(buffer.payload())[14], 0);
  EXPECT_EQ(reinterpret_cast<int16_t*>(buffer.payload())[15], 0);
  EXPECT_EQ(reinterpret_cast<int16_t*>(buffer.payload())[19], 0);
  EXPECT_EQ(reinterpret_cast<int16_t*>(buffer.payload())[20], 0);
  EXPECT_EQ(reinterpret_cast<int16_t*>(buffer.payload())[24], 0);
}

TEST(ReusableBufferTest, AppendTwoChan) {
  ReusableBuffer buffer(kFormatTwoChan, 8);
  std::vector<int16_t> payload1{1, 2, 3, 4};

  // Starts empty.
  buffer.Reset(Fixed(0));
  EXPECT_EQ(buffer.start(), Fixed(0));
  EXPECT_EQ(buffer.end(), Fixed(0));
  EXPECT_EQ(buffer.length(), 0);
  EXPECT_TRUE(buffer.empty());

  // Append data.
  buffer.AppendData(Fixed(0), 2, payload1.data());
  EXPECT_EQ(buffer.start(), Fixed(0)) << "start = " << ffl::String(buffer.start()).c_str();
  EXPECT_EQ(buffer.end(), Fixed(2)) << "end = " << ffl::String(buffer.end()).c_str();
  EXPECT_EQ(buffer.length(), 2);
  EXPECT_FALSE(buffer.empty());
  EXPECT_EQ(reinterpret_cast<int16_t*>(buffer.payload())[0], 1);
  EXPECT_EQ(reinterpret_cast<int16_t*>(buffer.payload())[3], 4);

  // Append silence.
  buffer.AppendSilence(Fixed(2), 2);
  EXPECT_EQ(buffer.start(), Fixed(0)) << "start = " << ffl::String(buffer.start()).c_str();
  EXPECT_EQ(buffer.end(), Fixed(4)) << "end = " << ffl::String(buffer.end()).c_str();
  EXPECT_EQ(buffer.length(), 4);
  EXPECT_FALSE(buffer.empty());
  EXPECT_EQ(reinterpret_cast<int16_t*>(buffer.payload())[0], 1);
  EXPECT_EQ(reinterpret_cast<int16_t*>(buffer.payload())[3], 4);
  EXPECT_EQ(reinterpret_cast<int16_t*>(buffer.payload())[4], 0);
  EXPECT_EQ(reinterpret_cast<int16_t*>(buffer.payload())[7], 0);
}

TEST(ReusableBufferTest, AppendResetAppend) {
  ReusableBuffer buffer(kFormatOneChan, 5);
  std::vector<int16_t> payload{1, 2, 3, 4, 5};

  for (int k = 0; k < 2; k++) {
    SCOPED_TRACE(fxl::StringPrintf("reset#%d", k));

    buffer.Reset(Fixed(0));
    EXPECT_EQ(buffer.start(), Fixed(0));
    EXPECT_EQ(buffer.end(), Fixed(0));
    EXPECT_EQ(buffer.length(), 0);

    if (k == 0) {
      buffer.AppendData(Fixed(0), static_cast<int64_t>(payload.size()), payload.data());
    } else {
      buffer.AppendSilence(Fixed(0), 5);
    }

    EXPECT_EQ(buffer.start(), Fixed(0)) << "start = " << ffl::String(buffer.start()).c_str();
    EXPECT_EQ(buffer.end(), Fixed(5)) << "end = " << ffl::String(buffer.end()).c_str();
    EXPECT_EQ(buffer.length(), 5);
    EXPECT_FALSE(buffer.empty());
    EXPECT_EQ(reinterpret_cast<int16_t*>(buffer.payload())[0], (k == 0) ? 1 : 0);
    EXPECT_EQ(reinterpret_cast<int16_t*>(buffer.payload())[4], (k == 0) ? 5 : 0);
  }
}

}  // namespace
}  // namespace media_audio
