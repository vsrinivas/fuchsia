// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/mix/custom_stage.h"

#include <lib/fidl/cpp/wire/connect_service.h>
#include <lib/zx/vmo.h>
#include <zircon/syscalls/object.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>

#include <fbl/algorithm.h>

#include "src/media/audio/lib/format2/fixed.h"
#include "src/media/audio/lib/format2/format.h"
#include "src/media/audio/services/mixer/mix/mix_job_context.h"
#include "src/media/audio/services/mixer/mix/pipeline_stage.h"

namespace media_audio {

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

CustomStage::CustomStage(std::string_view name, ProcessorConfiguration config,
                         UnreadableClock reference_clock)
    : PipelineStage(name, Format::CreateOrDie(config.outputs()[0].format()), reference_clock),
      block_size_frames_(static_cast<int64_t>(config.block_size_frames())),
      latency_frames_(static_cast<int64_t>(config.outputs()[0].latency_frames())),
      max_frames_per_call_(static_cast<int64_t>(config.max_frames_per_call())),
      fidl_buffers_(config.inputs()[0].buffer(), config.outputs()[0].buffer()),
      fidl_processor_(fidl::WireSyncClient(std::move(config.processor()))),
      source_(Format::CreateOrDie(config.inputs()[0].format()), reference_clock,
              Fixed(latency_frames_ + static_cast<int64_t>(config.outputs()[0].ring_out_frames())),
              /*round_down_fractional_frames=*/false),
      source_buffer_(source_.format(), max_frames_per_call_) {
  // Validate processor config.
  FX_CHECK(block_size_frames_ > 0);
  FX_CHECK(max_frames_per_call_ >= block_size_frames_);
  FX_CHECK(max_frames_per_call_ % block_size_frames_ == 0);
  FX_CHECK(static_cast<uint64_t>(max_frames_per_call_) * source_.format().bytes_per_frame() <=
           config.inputs()[0].buffer().size);
}

void CustomStage::AdvanceSelfImpl(Fixed frame) {
  if (output_ && Fixed(frame.Floor()) >= next_frame_to_process_) {
    // Invalidate output beyond the valid frames.
    output_ = nullptr;
  }

  // Update `latency_frames_processed_` to compensate for the gap between the last processed frame
  // and the target `frame`. For example, given a sequence of calls with a latency of 3 frames and a
  // block size of 1:
  //
  // ```
  // Read(0, 10)
  // Advance(12)
  // Read(12, 10)
  // Advance(25)
  // Read(25, 1)
  // Advance(30)
  // ```
  //
  // The first `Read(0, 10)` call will process 13 frames in total, and return the range [3, 13) of
  // the output buffer to compensate for the 3 latency frames, setting `latency_frames_processed_`
  // to 3, and `next_frame_to_process_` to 10 respectively.
  //
  // The following `Advance(12)` call will set `latency_frames_processed_` back to 1 to indicate
  // that the previously processed lookahead frames at range [10, 12) are no longer valid. Then, the
  // following `Read(12, 10)` call will process 12 frames this time, returning its output frames at
  // range [2, 12), setting `latency_frames_processed_` back to 3, and `next_frame_to_process_` to
  // 22 respectively.
  //
  // After that, the `Advance(25)` call will reset `latency_frames_processed_` to 0 since the frames
  // at range [22, 25) are skipped. Then, the next `Read(25, 1)` call will process 4 frames to
  // compensate for the latency frames again, returning the output frames at range [3, 4), setting
  // `latency_frames_processed_` to 1, and `next_frame_to_process_` to 26. Finally, the last
  // `Advance(30)` call will reset `latency_frames_processed_` to 0, by advancing to frame 30, which
  // is beyond the previously processed lookahead frames at range [26, 27).
  if (const int64_t frames_to_skip = frame.Floor() - next_frame_to_process_; frames_to_skip > 0) {
    latency_frames_processed_ = std::max(latency_frames_processed_ - frames_to_skip, 0L);
    next_frame_to_process_ = frame.Floor();
  }
}

void CustomStage::AdvanceSourcesImpl(MixJobContext& ctx, Fixed frame) {
  source_.Advance(ctx, Fixed(frame.Floor()));
}

std::optional<PipelineStage::Packet> CustomStage::ReadImpl(MixJobContext& ctx, Fixed start_frame,
                                                           int64_t frame_count) {
  // `ReadImpl` should not be called until we've `Advance`'d past the last cached packet. Also see
  // comments in `PipelineStage::MakeCachedPacket` for more information.
  FX_CHECK(!output_);

  // `CustomStage` always produces data on integrally-aligned frames.
  start_frame = Fixed(start_frame.Floor());

  // Skip frames that were already processed. This is needed when the source stream contains gaps.
  // For example, given a sequence of calls:
  //
  // ```
  // Read(0, 20)
  // Read(20, 20)
  // ```
  //
  // If our block size is 30, then at the first call, we will attempt to produce 30 frames starting
  // at frame 0. If the source has data for that range, we'll cache all 30 processed frames and the
  // second `Read` call will be handled by our cache.
  //
  // However, if the source has no data for the range [0, 30), the first `Read` call will return
  // `std::nullopt`. At the second call, `next_frame_to_process_` will be at frame 30, so we
  // shouldn't read any frames before frame 30 since we already know that we have passed that range.
  if (const int64_t frames_to_skip = next_frame_to_process_ - start_frame.Floor();
      frames_to_skip > 0) {
    frame_count -= frames_to_skip;
    start_frame += frames_to_skip;
  }

  // Process next `frame_count` frames.
  while (frame_count > 0) {
    source_buffer_.Reset(start_frame + latency_frames_processed_);
    const int64_t frames_processed = Process(ctx, frame_count);
    next_frame_to_process_ += frames_processed;
    if (output_) {
      FX_CHECK(frames_processed > 0);
      return MakeCachedPacket(start_frame, frames_processed, output_);
    }
    // We tried to process an entire block, however there was no data to process. This implies
    // `frame_count > max_frames_per_call_`, so try the next block.
    start_frame += Fixed(frames_processed);
    frame_count -= frames_processed;
  }

  // No data left to process.
  return std::nullopt;
}

int64_t CustomStage::Process(MixJobContext& ctx, int64_t frame_count) {
  // Make sure to read enough frames to compensate for `latency_frames_`.
  int64_t latency_frames_to_process = latency_frames_ - latency_frames_processed_;
  frame_count += latency_frames_to_process;

  // Clamp `frame_count` with a multiple of `block_size_frames_`, at most `max_frames_per_call_`.
  frame_count = static_cast<int64_t>(
      fbl::round_up(static_cast<uint64_t>(frame_count), static_cast<uint64_t>(block_size_frames_)));
  frame_count = std::min(frame_count, max_frames_per_call_);

  // Read next `frame_count` frames from `source_`.
  bool has_data = false;
  while (source_buffer_.length() < frame_count) {
    const Fixed read_start_frame = source_buffer_.end();
    const int64_t read_frame_count = frame_count - source_buffer_.length();
    const auto packet = source_.Read(ctx, read_start_frame, read_frame_count);
    if (packet) {
      // SampleAndHold: source frame 1.X overlaps dest frame 2.0, so always round up.
      source_buffer_.AppendData(Fixed(packet->start().Ceiling()), packet->length(),
                                packet->payload());
      has_data = true;
    } else {
      source_buffer_.AppendSilence(read_start_frame, read_frame_count);
    }
  }

  if (!has_data) {
    // No data to process, mark this buffer processed and reset `latency_frames_processed_`.
    latency_frames_processed_ = std::max(latency_frames_processed_ - frame_count, 0L);
    return frame_count;
  }

  // Process this buffer via FIDL connection, the result will be filled into `fidl_buffers_.output`.
  FX_CHECK(source_buffer_.length() == frame_count);
  CallFidlProcess(ctx);

  if (latency_frames_to_process >= frame_count) {
    // The process buffer has not reached to contain any target output frames yet. This could happen
    // when `max_frames_per_call_ <= latency_frames_`.
    latency_frames_processed_ += frame_count;
    return 0;
  }

  // Set `output_` with the corresponding `latency_frames_to_process` offset.
  output_ = static_cast<char*>(fidl_buffers_.output) +
            static_cast<size_t>(latency_frames_to_process * format().bytes_per_frame());
  latency_frames_processed_ += latency_frames_to_process;
  return frame_count - latency_frames_to_process;
}

void CustomStage::CallFidlProcess(MixJobContext& ctx) {
  // TODO(fxbug.dev/87651): Do we need to populate the `options`?
  const int64_t frame_count = source_buffer_.length();

  // The source data needs to be copied into the pre-negotiated input buffer.
  std::memmove(fidl_buffers_.input, source_buffer_.payload(),
               frame_count * static_cast<int64_t>(source_.format().bytes_per_frame()));

  // Synchronous IPC.
  MixJobSubtask subtask("CustomStage::Process");

  const auto result =
      fidl_processor_.buffer(fidl_process_buffer_.view())->Process(frame_count, /*options=*/{});

  subtask.Done();
  ctx.AddSubtaskMetrics(subtask.FinalMetrics());

  auto status = result.status();
  if (result.ok() && result.value().is_error()) {
    status = result.value().error_value();
  }

  // Zero fill the output buffer on failure.
  if (status != ZX_OK) {
    std::memset(fidl_buffers_.output, 0, fidl_buffers_.output_size);
    return;
  }

  // On success, update our metrics.
  auto& server_metrics = result.value().value()->per_stage_metrics;
  for (size_t k = 0; k < server_metrics.count(); k++) {
    MixJobSubtask::Metrics metrics;
    if (server_metrics[k].has_name()) {
      metrics.name.Append(server_metrics[k].name().get());
    } else {
      metrics.name.AppendPrintf("CustomStage::task%lu", k);
    }
    if (server_metrics[k].has_wall_time()) {
      metrics.wall_time = zx::nsec(server_metrics[k].wall_time());
    }
    if (server_metrics[k].has_cpu_time()) {
      metrics.cpu_time = zx::nsec(server_metrics[k].cpu_time());
    }
    if (server_metrics[k].has_queue_time()) {
      metrics.queue_time = zx::nsec(server_metrics[k].queue_time());
    }
    if (server_metrics[k].has_page_fault_time()) {
      metrics.page_fault_time = zx::nsec(server_metrics[k].page_fault_time());
    }
    if (server_metrics[k].has_kernel_lock_contention_time()) {
      metrics.kernel_lock_contention_time =
          zx::nsec(server_metrics[k].kernel_lock_contention_time());
    }
    ctx.AddSubtaskMetrics(metrics);
  }
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

}  // namespace media_audio
