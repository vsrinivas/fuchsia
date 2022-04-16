// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/mixer_service/mix/custom_stage.h"

#include <lib/fidl/llcpp/connect_service.h>
#include <lib/zx/time.h>
#include <lib/zx/vmo.h>
#include <zircon/syscalls/object.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <optional>
#include <utility>

#include <fbl/algorithm.h>

#include "src/media/audio/mixer_service/common/basic_types.h"
#include "src/media/audio/mixer_service/mix/pipeline_stage.h"

namespace media_audio_mixer_service {

namespace {

using ::fuchsia_audio_effects::wire::ProcessorConfiguration;
using ::fuchsia_mem::wire::Range;

zx_koid_t GetKoid(const zx::vmo& vmo) {
  // Must call with a valid VMO.
  zx_info_handle_basic_t info;
  const auto status = vmo.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  FX_CHECK(status == ZX_OK) << status;
  return info.koid;
}

}  // namespace

CustomStage::CustomStage(ProcessorConfiguration config)
    : PipelineStage("CustomStage", Format::CreateOrDie(config.inputs()[0].format())),
      block_size_frames_(config.block_size_frames()),
      max_frames_per_call_(config.max_frames_per_call()),
      fidl_buffers_(config.inputs()[0].buffer(), config.outputs()[0].buffer()),
      fidl_processor_(fidl::BindSyncClient(std::move(config.processor()))),
      source_buffer_(format(), static_cast<int64_t>(max_frames_per_call_)) {
  // Validate processor config.
  FX_CHECK(block_size_frames_ > 0);
  FX_CHECK(max_frames_per_call_ >= block_size_frames_);
  FX_CHECK(max_frames_per_call_ % block_size_frames_ == 0);
  FX_CHECK(max_frames_per_call_ * format().bytes_per_frame() <= config.inputs()[0].buffer().size);
}

void CustomStage::AdvanceImpl(Fixed frame) {
  // `CustomStage` always produces data on integrally-aligned frames.
  frame = Fixed(frame.Floor());
  if (has_valid_output_ && frame >= source_buffer_.end()) {
    // Invalidate output beyond the valid frames.
    has_valid_output_ = false;
  }
  source_->Advance(frame);
}

std::optional<PipelineStage::Packet> CustomStage::ReadImpl(Fixed start_frame, int64_t frame_count) {
  if (!source_) {
    // No source has been set.
    return std::nullopt;
  }

  // `ReadImpl` should not be called until we've `Advance`'d past the last cached packet. Also see
  // comments in `PipelineStage::MakeCachedPacket` for more information.
  FX_CHECK(!has_valid_output_);

  // `CustomStage` always produces data on integrally-aligned frames.
  start_frame = Fixed(start_frame.Floor());

  // Advance to our source's next available frame. This is needed when the source stream contains
  // gaps. For example, given a sequence of calls:
  //
  //   Read(0, 20)
  //   Read(20, 20)
  //
  // If our block size is 30, then at the first call, we will attempt to produce 30 frames starting
  // at frame 0. If the source has data for that range, we'll cache all 30 processed frames and the
  // second `Read` call will be handled by our cache.
  //
  // However, if the source has no data for the range [0, 30), the first `Read` call will return
  // `std::nullopt`. At the second call, we shouldn't ask the source for any frames before frame 30
  // because we already know that range is empty.
  if (const auto next_readable_frame = source_->next_readable_frame()) {
    // SampleAndHold: source frame 1.X overlaps dest frame 2.0, so always round up.
    const int64_t frames_to_advance = next_readable_frame->Ceiling() - start_frame.Floor();
    if (frames_to_advance > 0) {
      frame_count -= frames_to_advance;
      start_frame += Fixed(frames_to_advance);
    }
  }

  while (frame_count > 0) {
    const int64_t frames_read_from_source = ProcessBuffer(start_frame, frame_count);
    if (has_valid_output_) {
      FX_CHECK(source_buffer_.length() > 0);
      FX_CHECK(fidl_buffers_.output);
      return MakeCachedPacket(source_buffer_.start(), source_buffer_.length(),
                              fidl_buffers_.output);
    }
    // We tried to process an entire block, however the source had no data.
    // If `frame_count > max_frames_per_call_`, try the next block.
    start_frame += Fixed(frames_read_from_source);
    frame_count -= frames_read_from_source;
  }

  // The source has no data for the requested range.
  return std::nullopt;
}

void CustomStage::CallFidlProcess() {
  // TODO(fxbug.dev/87651): Add traces and stage metrics.

  // The source data needs to be copied into the pre-negotiated input buffer.
  std::memmove(fidl_buffers_.input, source_buffer_.payload(),
               source_buffer_.length() * static_cast<int64_t>(source_->format().bytes_per_frame()));

  const auto num_frames = source_buffer_.length();
  // TODO(fxbug.dev/87651): Do we need to populate the `options`?
  const auto result =
      fidl_processor_.buffer(fidl_process_buffer_.view())->Process(num_frames, /*options=*/{});

  auto status = result.status();
  if (result.ok() && result->result.is_err()) {
    status = result->result.err();
  }
  // Zero fill the output buffer on failure.
  if (status != ZX_OK) {
    std::memset(fidl_buffers_.output, 0, fidl_buffers_.output_size);
  }
}

int64_t CustomStage::ProcessBuffer(Fixed start_frame, int64_t frame_count) {
  has_valid_output_ = false;

  source_buffer_.Reset(start_frame);
  bool has_data = false;

  // Clamp `frame_count` with a multiple of `block_size_frames_`, at most `max_frames_per_call_`.
  frame_count =
      static_cast<int64_t>(fbl::round_up(static_cast<uint64_t>(frame_count), block_size_frames_));
  frame_count = std::min(frame_count, static_cast<int64_t>(max_frames_per_call_));

  // Read `frame_count` frames.
  while (source_buffer_.length() < frame_count) {
    const Fixed read_start_frame = source_buffer_.end();
    const int64_t read_frame_count = frame_count - source_buffer_.length();

    const auto packet = source_->Read(read_start_frame, read_frame_count);
    if (packet) {
      // SampleAndHold: source frame 1.X overlaps dest frame 2.0, so always round up.
      source_buffer_.AppendData(Fixed(packet->start().Ceiling()), packet->length(),
                                packet->payload());
      has_data = true;
    } else {
      source_buffer_.AppendSilence(read_start_frame, read_frame_count);
    }
  }

  FX_CHECK(source_buffer_.length() % block_size_frames_ == 0)
      << "Bad buffer size " << source_buffer_.length() << " must be divisible by "
      << block_size_frames_;

  // If the source had no frames, we don't need to process anything.
  if (!has_data) {
    return frame_count;
  }

  // Process this buffer via FIDL connection, the result will be filled into `fidl_buffers_.output`.
  CallFidlProcess();
  has_valid_output_ = true;

  return frame_count;
}

CustomStage::FidlBuffers::FidlBuffers(const Range& input_range, const Range& output_range) {
  const auto input_end = input_range.offset + input_range.size;
  const auto output_end = output_range.offset + output_range.size;
  if (GetKoid(input_range.vmo) == GetKoid(output_range.vmo)) {
    // Shared buffer: map the union of the input and output buffers.
    auto& mapper = mappers.emplace_back();
    const uint64_t shared_start = std::min(input_range.offset, output_range.offset);
    const uint64_t shared_end = std::max(input_end, output_end);
    const auto status = mapper.Map(input_range.vmo, shared_start, shared_end - shared_start,
                                   ZX_VM_PERM_READ | ZX_VM_PERM_WRITE);
    if (status != ZX_OK) {
      FX_PLOGS(FATAL, status) << "failed to map shared buffer with start=" << shared_start
                              << " end=" << shared_end;
    }
    input = static_cast<char*>(mapper.start()) + (input_range.offset - shared_start);
    output = static_cast<char*>(mapper.start()) + (output_range.offset - shared_start);
  } else {
    // Separate buffers: map separately.
    // We always write the input. We write the output if the IPC call fails. Hence we map both R+W.
    auto& input_mapper = mappers.emplace_back();
    if (const auto status = input_mapper.Map(input_range.vmo, input_range.offset, input_range.size,
                                             ZX_VM_PERM_READ | ZX_VM_PERM_WRITE);
        status != ZX_OK) {
      FX_PLOGS(FATAL, status) << "failed to map input buffer with offset=" << input_range.offset
                              << " size=" << input_range.size;
    }
    input = input_mapper.start();

    auto& output_mapper = mappers.emplace_back();
    if (const auto status =
            output_mapper.Map(output_range.vmo, output_range.offset, output_range.size,
                              ZX_VM_PERM_READ | ZX_VM_PERM_WRITE);
        status != ZX_OK) {
      FX_PLOGS(FATAL, status) << "failed to map output buffer with offset=" << output_range.offset
                              << " size=" << output_range.size;
    }
    output = output_mapper.start();
  }
  input_size = input_range.size;
  output_size = output_range.size;
}

}  // namespace media_audio_mixer_service
