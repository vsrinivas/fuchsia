// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl/mixer_node.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/media/audio/lib/format2/format.h"
#include "src/media/audio/services/mixer/fidl/testing/fake_graph.h"

namespace media_audio {
namespace {

TEST(MixerNodeTest, Create) {
  const Format source_format_1 =
      Format::CreateOrDie({fuchsia_audio::SampleType::kFloat32, 2, 48000});
  const Format source_format_2 = Format::CreateOrDie({fuchsia_audio::SampleType::kInt16, 1, 24000});
  const Format dest_format = Format::CreateOrDie({fuchsia_audio::SampleType::kFloat32, 1, 48000});

  FakeGraph graph({
      .unconnected_ordinary_nodes = {1, 2, 3},
      .formats = {{&source_format_1, {1}}, {&source_format_2, {2}}, {&dest_format, {3}}},
  });

  auto q = graph.global_task_queue();

  const auto mixer_node = MixerNode::Create({
      .format = dest_format,
      .reference_clock = DefaultClock(),
      .dest_buffer_frame_count = 10,
      .detached_thread = graph.detached_thread(),
  });
  ASSERT_NE(mixer_node, nullptr);
  EXPECT_EQ(mixer_node->reference_clock(), DefaultClock());
  EXPECT_EQ(mixer_node->pipeline_stage()->format(), dest_format);
  EXPECT_EQ(mixer_node->pipeline_stage()->thread(), graph.detached_thread()->pipeline_thread());
  EXPECT_EQ(mixer_node->thread(), graph.detached_thread());
  EXPECT_TRUE(mixer_node->sources().empty());
  EXPECT_EQ(mixer_node->dest(), nullptr);

  // TODO(fxbug.dev/87651): Test connecting the source nodes to the mixer node here - once
  // `PipelineStage::AddSourceOptions` can be passed in `GraphServer::CreateEdge`.
}

}  // namespace
}  // namespace media_audio
