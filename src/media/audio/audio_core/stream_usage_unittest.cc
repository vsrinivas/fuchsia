// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/stream_usage.h"

#include <gtest/gtest.h>

namespace media::audio {
namespace {

TEST(StreamUsageTest, ToString) {
  EXPECT_STREQ("RenderUsage::BACKGROUND",
               StreamUsage::WithRenderUsage(RenderUsage::BACKGROUND).ToString());
  EXPECT_STREQ("RenderUsage::MEDIA", StreamUsage::WithRenderUsage(RenderUsage::MEDIA).ToString());
  EXPECT_STREQ("RenderUsage::INTERRUPTION",
               StreamUsage::WithRenderUsage(RenderUsage::INTERRUPTION).ToString());
  EXPECT_STREQ("RenderUsage::SYSTEM_AGENT",
               StreamUsage::WithRenderUsage(RenderUsage::SYSTEM_AGENT).ToString());
  EXPECT_STREQ("RenderUsage::COMMUNICATION",
               StreamUsage::WithRenderUsage(RenderUsage::COMMUNICATION).ToString());
  EXPECT_STREQ("RenderUsage::ULTRASOUND",
               StreamUsage::WithRenderUsage(RenderUsage::ULTRASOUND).ToString());
  EXPECT_STREQ("CaptureUsage::BACKGROUND",
               StreamUsage::WithCaptureUsage(CaptureUsage::BACKGROUND).ToString());
  EXPECT_STREQ("CaptureUsage::FOREGROUND",
               StreamUsage::WithCaptureUsage(CaptureUsage::FOREGROUND).ToString());
  EXPECT_STREQ("CaptureUsage::SYSTEM_AGENT",
               StreamUsage::WithCaptureUsage(CaptureUsage::SYSTEM_AGENT).ToString());
  EXPECT_STREQ("CaptureUsage::COMMUNICATION",
               StreamUsage::WithCaptureUsage(CaptureUsage::COMMUNICATION).ToString());
  EXPECT_STREQ("CaptureUsage::LOOPBACK",
               StreamUsage::WithCaptureUsage(CaptureUsage::LOOPBACK).ToString());
  EXPECT_STREQ("CaptureUsage::ULTRASOUND",
               StreamUsage::WithCaptureUsage(CaptureUsage::ULTRASOUND).ToString());
}

}  // namespace
}  // namespace media::audio
