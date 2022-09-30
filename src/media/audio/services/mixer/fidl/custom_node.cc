// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl/custom_node.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>
#include <lib/zx/vmo.h>
#include <zircon/types.h>

#include <memory>
#include <string_view>
#include <utility>

#include <fbl/algorithm.h>

#include "fidl/fuchsia.audio.effects/cpp/wire_types.h"
#include "fidl/fuchsia.mediastreams/cpp/wire_types.h"
#include "src/media/audio/lib/clock/unreadable_clock.h"
#include "src/media/audio/lib/format2/format.h"
#include "src/media/audio/services/common/logging.h"
#include "src/media/audio/services/mixer/fidl/ptr_decls.h"
#include "src/media/audio/services/mixer/mix/custom_stage.h"
#include "src/media/audio/services/mixer/mix/ptr_decls.h"

namespace media_audio {

namespace {

using ::fuchsia_audio_effects::wire::ProcessorConfiguration;
using ::fuchsia_mediastreams::wire::AudioSampleFormat;
using ::fuchsia_mem::wire::Range;

zx_rights_t GetRights(const zx::vmo& vmo) {
  // Must call with a valid VMO.
  zx_info_handle_basic_t info;
  auto status = vmo.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  FX_CHECK(status == ZX_OK) << status;
  return info.rights;
}

zx_koid_t GetKoid(const zx::vmo& vmo) {
  // Must call with a valid VMO.
  zx_info_handle_basic_t info;
  auto status = vmo.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  FX_CHECK(status == ZX_OK) << status;
  return info.koid;
}

bool ValidateMemRange(const Range& range, const Format& format, uint64_t max_frames_per_call,
                      uint64_t block_size_frames, std::string_view debug_prefix) {
  if (range.size == 0) {
    FX_LOGS(WARNING) << debug_prefix << "fuchsia.mem.Range is empty";
    return false;
  }

  uint64_t vmo_size;
  if (auto status = range.vmo.get_size(&vmo_size); status != ZX_OK) {
    FX_PLOGS(WARNING, status) << debug_prefix << "could not read VMO size";
    return false;
  }

  // The VMO must be RW mappable: we always write to input buffers, and in error cases, we also
  // write to output buffers.
  const zx_rights_t expected_rights = ZX_RIGHT_MAP | ZX_RIGHT_READ | ZX_RIGHT_WRITE;
  if (auto rights = GetRights(range.vmo); (rights & expected_rights) != expected_rights) {
    FX_LOGS(WARNING) << debug_prefix << "vmo has rights 0x" << std::hex << rights
                     << ", expect rights 0x" << expected_rights;
    return false;
  }

  // The buffer must lie within the VMO.
  uint64_t end_offset;
  if (add_overflow(range.offset, range.size, &end_offset) || end_offset > vmo_size) {
    FX_LOGS(WARNING) << debug_prefix << "fuchsia.mem.Range{offset=" << range.offset
                     << ", size=" << range.size << "} out-of-bounds: VMO size is " << vmo_size;
    return false;
  }

  // The buffer must be large enough to handle the largest possible input.
  const size_t min_size = max_frames_per_call * format.bytes_per_frame();
  if (range.size < min_size) {
    FX_LOGS(WARNING) << debug_prefix << "fuchsia.mem.Range{offset=" << range.offset
                     << ", size=" << range.size << "} too small: size must be at least " << min_size
                     << " to cover max_frames_per_call (" << max_frames_per_call << ")"
                     << " and block_size_frames (" << block_size_frames << ")";
    return false;
  }

  return true;
}

bool PartialOverlap(const Range& a, const Range& b) {
  if (GetKoid(a.vmo) != GetKoid(b.vmo)) {
    return false;
  }
  auto a_end = a.offset + a.size;
  auto b_end = b.offset + b.size;
  // Same VMOs but no intersection?
  if (a_end <= b.offset || b_end <= a.offset) {
    return false;
  }
  // They overlap: report true if the ranges don't match exactly.
  return a.offset != b.offset || a.size != b.size;
}

// Validates node `args` and parses them to be passed in `CustomStage`.
std::optional<CustomStage::Args> ValidateAndParseArgs(CustomNode::Args args) {
  // Validate processor config.
  auto& config = args.config;

  if (!config.has_processor() || !config.processor().is_valid()) {
    FX_LOGS(WARNING) << "ProcessorConfiguration missing field 'processor'";
    return std::nullopt;
  }
  if (!config.has_inputs() || config.inputs().count() != 1) {
    FX_LOGS(WARNING) << "ProcessorConfiguration must have exactly one input stream";
    return std::nullopt;
  }
  if (!config.has_outputs() || config.outputs().count() != 1) {
    FX_LOGS(WARNING) << "ProcessorConfiguration must have exactly one output stream";
    return std::nullopt;
  }

  auto& input = config.inputs()[0];
  auto& output = config.outputs()[0];

  // Validate input/output format.
  if (!input.has_format()) {
    FX_LOGS(WARNING) << "ProcessorConfiguration.inputs[0] missing field 'format'";
    return std::nullopt;
  }
  if (!output.has_format()) {
    FX_LOGS(WARNING) << "ProcessorConfiguration.outputs[0] missing field 'format'";
    return std::nullopt;
  }
  if (input.format().frames_per_second != output.format().frames_per_second) {
    FX_LOGS(WARNING) << "ProcessorConfiguration input and output have different frame rates: "
                     << input.format().frames_per_second
                     << " != " << output.format().frames_per_second;
    return std::nullopt;
  }

  if (!input.has_buffer()) {
    FX_LOGS(WARNING) << "ProcessorConfiguration.inputs[0] missing field 'buffer'";
    return std::nullopt;
  }
  if (!output.has_buffer()) {
    FX_LOGS(WARNING) << "ProcessorConfiguration.outputs[0] missing field 'buffer'";
    return std::nullopt;
  }

  // Validate formats.
  const auto source_format = Format::CreateLegacy(input.format());
  if (!source_format.is_ok()) {
    FX_LOGS(WARNING) << "ProcessorConfiguration invalid input format";
    return std::nullopt;
  }
  const auto dest_format = Format::CreateLegacy(output.format());
  if (!dest_format.is_ok()) {
    FX_LOGS(WARNING) << "ProcessorConfiguration invalid output format";
    return std::nullopt;
  }

  // Set defaults.
  const uint64_t default_max_frames_per_call =
      std::min(input.buffer().size / source_format.value().bytes_per_frame(),
               output.buffer().size / dest_format.value().bytes_per_frame());
  const uint64_t block_size_frames =
      config.has_block_size_frames() ? config.block_size_frames() : 1;
  uint64_t max_frames_per_call =
      config.has_max_frames_per_call() ? config.max_frames_per_call() : default_max_frames_per_call;

  const int64_t latency_frames =
      output.has_latency_frames() ? static_cast<int64_t>(output.latency_frames()) : 0;
  const int64_t ring_out_frames =
      output.has_ring_out_frames() ? static_cast<int64_t>(output.ring_out_frames()) : 0;

  // Ensure the block size is satisfiable.
  if (block_size_frames > max_frames_per_call) {
    FX_LOGS(WARNING) << "ProcessorConfiguration max_frames_per_call (" << max_frames_per_call
                     << ") < block_size_frames (" << block_size_frames << ")";
    return std::nullopt;
  }

  // Now round down `max_frames_per_call` so it satisfies the requested block size.
  max_frames_per_call = fbl::round_down(max_frames_per_call, block_size_frames);

  // Validate buffer sizes.
  if (max_frames_per_call > default_max_frames_per_call) {
    FX_LOGS(WARNING) << "ProcessorConfiguration max_frames_per_call (" << max_frames_per_call
                     << ") > input buffer size (" << default_max_frames_per_call << " frames)";
    return std::nullopt;
  }

  // Validate that we won't crash when trying to access the input and output buffers.
  if (!ValidateMemRange(input.buffer(), source_format.value(), max_frames_per_call,
                        block_size_frames, "ProcessorConfiguration: input buffer ")) {
    FX_LOGS(WARNING) << "ProcessorConfiguration: invalid input buffer";
    return std::nullopt;
  }
  if (!ValidateMemRange(output.buffer(), dest_format.value(), max_frames_per_call,
                        block_size_frames, "ProcessorConfiguration: output buffer ")) {
    FX_LOGS(WARNING) << "ProcessorConfiguration: invalid output buffer";
    return std::nullopt;
  }

  // Validate that the memory ranges do not overlap.
  if (PartialOverlap(input.buffer(), output.buffer())) {
    FX_LOGS(WARNING) << "ProcessorConfiguration: input and output buffers partially overlap";
    return std::nullopt;
  }

  return CustomStage::Args{
      .name = args.name,
      .reference_clock = std::move(args.reference_clock),
      .source_format = source_format.value(),
      .source_buffer = std::move(input.buffer()),
      .dest_format = dest_format.value(),
      .dest_buffer = std::move(output.buffer()),
      .block_size_frames = static_cast<int64_t>(block_size_frames),
      .latency_frames = latency_frames,
      .max_frames_per_call = static_cast<int64_t>(max_frames_per_call),
      .ring_out_frames = ring_out_frames,
      .processor = fidl::WireSyncClient(std::move(config.processor())),
  };
}

}  // namespace

std::shared_ptr<CustomNode> CustomNode::Create(Args args) {
  struct WithPublicCtor : public CustomNode {
   public:
    explicit WithPublicCtor(std::string_view name, UnreadableClock reference_clock,
                            PipelineDirection pipeline_direction)
        : CustomNode(name, std::move(reference_clock), pipeline_direction) {}
  };
  auto parent =
      std::make_shared<WithPublicCtor>(args.name, args.reference_clock, args.pipeline_direction);
  auto detached_thread = std::move(args.detached_thread);

  auto stage_args = ValidateAndParseArgs(std::move(args));
  if (!stage_args) {
    return nullptr;
  }

  const int64_t presentation_delay_frames =
      stage_args->latency_frames + stage_args->block_size_frames - 1;
  const zx::duration presentation_delay =
      zx::nsec(stage_args->dest_format.frames_per_ns().Inverse().Scale(
          presentation_delay_frames, TimelineRate::RoundingMode::Ceiling));
  const auto source_format = stage_args->source_format;

  auto pipeline_stage = std::make_shared<CustomStage>(std::move(*stage_args));
  pipeline_stage->set_thread(detached_thread->pipeline_thread());

  parent->InitializeChildNodes(std::move(pipeline_stage), parent, std::move(detached_thread),
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
    const Node* source) const {
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
    const Node* source) const {
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
