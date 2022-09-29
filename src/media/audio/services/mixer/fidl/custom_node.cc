// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl/custom_node.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>
#include <zircon/types.h>

#include <memory>
#include <string_view>
#include <utility>

#include "src/media/audio/lib/clock/unreadable_clock.h"
#include "src/media/audio/lib/format2/format.h"
#include "src/media/audio/services/common/logging.h"
#include "src/media/audio/services/mixer/fidl/ptr_decls.h"
#include "src/media/audio/services/mixer/mix/custom_stage.h"
#include "src/media/audio/services/mixer/mix/ptr_decls.h"

namespace media_audio {

std::shared_ptr<CustomNode> CustomNode::Create(Args args) {
  struct WithPublicCtor : public CustomNode {
   public:
    explicit WithPublicCtor(std::string_view name, UnreadableClock reference_clock,
                            PipelineDirection pipeline_direction)
        : CustomNode(name, std::move(reference_clock), pipeline_direction) {}
  };

  FX_CHECK(args.config.has_inputs() && args.config.inputs().count() == 1);
  const Format source_format = Format::CreateLegacyOrDie(args.config.inputs()[0].format());

  FX_CHECK(args.config.has_outputs() && args.config.outputs().count() == 1);
  FX_CHECK(args.config.block_size_frames() > 0);
  const int64_t presentation_delay_frames = static_cast<int64_t>(
      args.config.outputs()[0].latency_frames() + args.config.block_size_frames() - 1);

  // TODO(fxbug.dev/87651): Validate `ProcessorConfig` before parsing.
  auto pipeline_stage = std::make_shared<CustomStage>(CustomStage::Args{
      .name = args.name,
      .reference_clock = args.reference_clock,
      .source_format = source_format,
      .source_buffer = std::move(args.config.inputs()[0].buffer()),
      .dest_format = Format::CreateLegacyOrDie(args.config.outputs()[0].format()),
      .dest_buffer = std::move(args.config.outputs()[0].buffer()),
      .block_size_frames = static_cast<int64_t>(args.config.block_size_frames()),
      .latency_frames = static_cast<int64_t>(args.config.outputs()[0].latency_frames()),
      .max_frames_per_call = static_cast<int64_t>(args.config.max_frames_per_call()),
      .ring_out_frames = static_cast<int64_t>(args.config.outputs()[0].ring_out_frames()),
      .processor = fidl::WireSyncClient(std::move(args.config.processor())),
  });
  pipeline_stage->set_thread(args.detached_thread->pipeline_thread());

  const zx::duration presentation_delay =
      zx::nsec(pipeline_stage->format().frames_per_ns().Inverse().Scale(
          presentation_delay_frames, TimelineRate::RoundingMode::Ceiling));

  auto parent =
      std::make_shared<WithPublicCtor>(args.name, args.reference_clock, args.pipeline_direction);
  parent->InitializeChildNodes(std::move(pipeline_stage), parent, std::move(args.detached_thread),
                               source_format, presentation_delay);
  return parent;
}

CustomNode::ChildSourceNode::ChildSourceNode(std::string_view name,
                                             PipelineDirection pipeline_direction,
                                             PipelineStagePtr pipeline_stage, NodePtr parent,
                                             GraphDetachedThreadPtr detached_thread,
                                             const Format& format, zx::duration presentation_delay)
    : Node(name, /*is_meta=*/false, pipeline_stage->reference_clock(), pipeline_direction,
           std::move(pipeline_stage), std::move(parent)),
      format_(format),
      presentation_delay_(presentation_delay) {
  set_thread(std::move(detached_thread));
}

zx::duration CustomNode::ChildSourceNode::GetSelfPresentationDelayForSource(
    const NodePtr& source) const {
  // Report the underlying `CustomStage` delay.
  return presentation_delay_;
}

bool CustomNode::ChildSourceNode::CanAcceptSourceFormat(const Format& format) const {
  return format == format_;
}

std::optional<size_t> CustomNode::ChildSourceNode::MaxSources() const { return 1; }

bool CustomNode::ChildSourceNode::AllowsDest() const { return false; }

CustomNode::ChildDestNode::ChildDestNode(std::string_view name,
                                         PipelineDirection pipeline_direction,
                                         PipelineStagePtr pipeline_stage, NodePtr parent,
                                         GraphDetachedThreadPtr detached_thread)
    : Node(name, /*is_meta=*/false, pipeline_stage->reference_clock(), pipeline_direction,
           std::move(pipeline_stage), std::move(parent)) {
  set_thread(std::move(detached_thread));
}

zx::duration CustomNode::ChildDestNode::GetSelfPresentationDelayForSource(
    const NodePtr& source) const {
  // Child destination node does not contribute in any presentation delay, since the underlying
  // `CustomStage` delay is already incorparated by the corresponding `ChildSourceNode`.
  return zx::nsec(0);
}

bool CustomNode::ChildDestNode::CanAcceptSourceFormat(const Format& format) const { return false; }

std::optional<size_t> CustomNode::ChildDestNode::MaxSources() const { return 0; }

bool CustomNode::ChildDestNode::AllowsDest() const { return true; }

CustomNode::CustomNode(std::string_view name, UnreadableClock reference_clock,
                       PipelineDirection pipeline_direction)
    : Node(name, /*is_meta=*/true, std::move(reference_clock), pipeline_direction,
           /*pipeline_stage=*/nullptr, /*parent=*/nullptr) {}

void CustomNode::InitializeChildNodes(PipelineStagePtr pipeline_stage, NodePtr parent,
                                      GraphDetachedThreadPtr detached_thread,
                                      const Format& source_format,
                                      zx::duration presentation_delay) {
  // TODO(fxbug.dev/87651): This is currently hardcoded for the 1 -> 1 `CustomStage` implementation.
  // Refactor this to use `CustomNodeProperties` instead once M -> N edges are supported.
  SetBuiltInChildren(
      std::vector<NodePtr>{
          std::make_shared<ChildSourceNode>(std::string(parent->name()) + "ChildSource",
                                            parent->pipeline_direction(), pipeline_stage, parent,
                                            detached_thread, source_format, presentation_delay),
      },
      std::vector<NodePtr>{
          std::make_shared<ChildDestNode>(std::string(parent->name()) + "ChildDest",
                                          parent->pipeline_direction(), std::move(pipeline_stage),
                                          std::move(parent), std::move(detached_thread)),
      });
}

NodePtr CustomNode::CreateNewChildSource() {
  // It is not allowed to modify the child source nodes dynamically.
  return nullptr;
}

NodePtr CustomNode::CreateNewChildDest() {
  // It is not allowed to modify the child destination nodes dynamically.
  return nullptr;
}

}  // namespace media_audio
