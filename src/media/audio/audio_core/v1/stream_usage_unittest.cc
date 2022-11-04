// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/v1/stream_usage.h"

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

TEST(StreamUsageMaskTest, Coherent) {
  for (const auto& set_usage : kStreamUsages) {
    StreamUsageMask usage_mask;
    usage_mask.insert(set_usage);
    for (const auto& check_usage : kStreamUsages) {
      EXPECT_EQ(usage_mask.contains(check_usage), set_usage == check_usage);
    }
  }
}

TEST(StreamUsageMaskTest, CopyAssign) {
  StreamUsageMask usage_mask({StreamUsage::WithRenderUsage(RenderUsage::MEDIA)});

  StreamUsageMask copy_constructed(usage_mask);
  EXPECT_EQ(usage_mask, copy_constructed);

  StreamUsageMask copy_assigned;
  copy_assigned = usage_mask;
  EXPECT_EQ(usage_mask, copy_assigned);
}

constexpr bool StreamUsageMaskConstexprTest() {
  StreamUsageMask usage_mask({StreamUsage::WithRenderUsage(RenderUsage::MEDIA)});
  StreamUsageMask copied = usage_mask;
  if (usage_mask != copied) {
    return false;
  }

  if (!usage_mask.contains(StreamUsage::WithRenderUsage(RenderUsage::MEDIA))) {
    return false;
  }
  if (usage_mask.is_empty()) {
    return false;
  }

  usage_mask.erase(StreamUsage::WithRenderUsage(RenderUsage::MEDIA));
  if (usage_mask.contains(StreamUsage::WithRenderUsage(RenderUsage::MEDIA))) {
    return false;
  }
  if (!usage_mask.is_empty()) {
    return false;
  }

  StreamUsageMask usage_mask2({StreamUsage::WithRenderUsage(RenderUsage::MEDIA),
                               StreamUsage::WithRenderUsage(RenderUsage::COMMUNICATION)});
  usage_mask.insert_all(usage_mask2);
  if (!usage_mask.contains(StreamUsage::WithRenderUsage(RenderUsage::MEDIA))) {
    return false;
  }
  if (!usage_mask.contains(StreamUsage::WithRenderUsage(RenderUsage::COMMUNICATION))) {
    return false;
  }
  if (usage_mask.is_empty()) {
    return false;
  }

  return true;
}

static_assert(StreamUsageMaskConstexprTest() == true);

}  // namespace
}  // namespace media::audio
