// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/v1/effects_stage_v2.h"

#include <fuchsia/media/cpp/fidl.h>

#include <atomic>

#include <fbl/algorithm.h>

#include "src/media/audio/audio_core/v1/logging_flags.h"
#include "src/media/audio/audio_core/v1/mixer/intersect.h"
#include "src/media/audio/audio_core/v1/silence_padding_stream.h"
#include "src/media/audio/audio_core/v1/threading_model.h"

namespace media::audio {
namespace {

// We expect to copy the fuchsia_audio_effects::ProcessMetrics name into the StageMetrics name.
static_assert(fuchsia_audio_effects::wire::kMaxProcessStageNameLength <=
              StageMetrics::kMaxNameLength);

// We expect StreamUsageMask to map r to (1<<r) for each RenderUsage r.
// See ProcessOptions.usage_mask_per_input in sdk/fidl/fuchsia.audio.effects/processor.fidl.
static_assert(StreamUsageMask({StreamUsage::WithRenderUsage(RenderUsage::BACKGROUND)}).mask() ==
              (1 << static_cast<int>(fuchsia::media::AudioRenderUsage::BACKGROUND)));
static_assert(StreamUsageMask({StreamUsage::WithRenderUsage(RenderUsage::MEDIA)}).mask() ==
              (1 << static_cast<int>(fuchsia::media::AudioRenderUsage::MEDIA)));
static_assert(StreamUsageMask({StreamUsage::WithRenderUsage(RenderUsage::INTERRUPTION)}).mask() ==
              (1 << static_cast<int>(fuchsia::media::AudioRenderUsage::INTERRUPTION)));
static_assert(StreamUsageMask({StreamUsage::WithRenderUsage(RenderUsage::SYSTEM_AGENT)}).mask() ==
              (1 << static_cast<int>(fuchsia::media::AudioRenderUsage::SYSTEM_AGENT)));
static_assert(StreamUsageMask({StreamUsage::WithRenderUsage(RenderUsage::COMMUNICATION)}).mask() ==
              (1 << static_cast<int>(fuchsia::media::AudioRenderUsage::COMMUNICATION)));

// Ignore internal usages, such as ULTRASOUND.
static constexpr uint32_t kSupportedUsageMask =
    StreamUsageMask({StreamUsage::WithRenderUsage(RenderUsage::BACKGROUND),
                     StreamUsage::WithRenderUsage(RenderUsage::MEDIA),
                     StreamUsage::WithRenderUsage(RenderUsage::INTERRUPTION),
                     StreamUsage::WithRenderUsage(RenderUsage::SYSTEM_AGENT),
                     StreamUsage::WithRenderUsage(RenderUsage::COMMUNICATION)})
        .mask();

// Used to throttle log messages.
std::atomic<int64_t> fidl_error_count_{0};

Format ToOldFormat(const fuchsia_mediastreams::wire::AudioFormat& new_format) {
  FX_CHECK(new_format.sample_format == fuchsia_mediastreams::wire::AudioSampleFormat::kFloat);
  return Format::Create(fuchsia::media::AudioStreamType{
                            .sample_format = fuchsia::media::AudioSampleFormat::FLOAT,
                            .channels = new_format.channel_count,
                            .frames_per_second = new_format.frames_per_second,
                        })
      .take_value();
}

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

zx_status_t ValidateMemRange(bool is_input, const fuchsia_mem::wire::Range& range,
                             const fuchsia_audio_effects::wire::ProcessorConfiguration& config) {
  const char* const debug_prefix =
      is_input ? "ProcessorConfiguration input buffer: " : "ProcessorConfiguration output buffer: ";

  if (range.size == 0) {
    FX_LOGS(ERROR) << debug_prefix << "fuchsia.mem.Range is empty";
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  uint64_t vmo_size;
  if (auto status = range.vmo.get_size(&vmo_size); status != ZX_OK) {
    FX_PLOGS(ERROR, status) << debug_prefix << "could not read VMO size";
    return status;
  }

  // The VMO must be RW mappable: we always write to input buffers, and in error cases,
  // we also write to output buffers (see EffectsStageV2::CallProcess).
  const zx_rights_t expected_rights = ZX_RIGHT_MAP | ZX_RIGHT_READ | ZX_RIGHT_WRITE;
  if (auto rights = GetRights(range.vmo); (rights & expected_rights) != expected_rights) {
    FX_LOGS(ERROR) << debug_prefix << "vmo has rights 0x" << std::hex << rights
                   << ", expect rights 0x" << expected_rights;
    return ZX_ERR_INVALID_ARGS;
  }

  // The buffer must lie within the VMO.
  uint64_t end_offset;
  if (add_overflow(range.offset, range.size, &end_offset) || end_offset > vmo_size) {
    FX_LOGS(ERROR) << debug_prefix << "fuchsia.mem.Range{offset=" << range.offset
                   << ", size=" << range.size << "} out-of-bounds: VMO size is " << vmo_size;
    return ZX_ERR_OUT_OF_RANGE;
  }

  // The buffer must be large enough to handle the largest possible input.
  const size_t input_channels = config.inputs()[0].format().channel_count;
  const size_t output_channels = config.outputs()[0].format().channel_count;
  const size_t bytes_per_frame = (is_input ? input_channels : output_channels) * sizeof(float);
  const size_t min_size = config.max_frames_per_call() * bytes_per_frame;

  if (range.size < min_size) {
    FX_LOGS(ERROR) << debug_prefix << "fuchsia.mem.Range{offset=" << range.offset
                   << ", size=" << range.size << "} too small: size must be at least " << min_size
                   << " to cover max_frames_per_call (" << config.max_frames_per_call() << ")"
                   << " and block_size_frames (" << config.block_size_frames() << ")";
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  return ZX_OK;
}

zx_status_t PartialOverlap(const fuchsia_mem::wire::Range& a, const fuchsia_mem::wire::Range& b) {
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

// Make a copy of 'src' so the config can be edited. The resulting config takes ownership of
// all handles from 'src'. fxbug.dev/86255 explains why this is necessary.
fuchsia_audio_effects::wire::ProcessorConfiguration CloneConfigAndTakeHandles(
    fidl::AnyArena& arena, fuchsia_audio_effects::wire::ProcessorConfiguration src) {
  fuchsia_audio_effects::wire::ProcessorConfiguration dst(arena);

  if (src.has_processor()) {
    dst.set_processor(std::move(src.processor()));
  }

  if (src.has_inputs()) {
    dst.set_inputs(arena, arena, src.inputs().count());
    for (size_t k = 0; k < src.inputs().count(); k++) {
      auto& src_input = src.inputs()[k];
      auto& dst_input = dst.inputs()[k];
      dst_input.Allocate(arena);
      if (src_input.has_format()) {
        dst_input.set_format(arena, std::move(src_input.format()));
      }
      if (src_input.has_buffer()) {
        dst_input.set_buffer(arena, std::move(src_input.buffer()));
      }
    }
  }

  if (src.has_outputs()) {
    dst.set_outputs(arena, arena, src.outputs().count());
    for (size_t k = 0; k < src.outputs().count(); k++) {
      auto& src_output = src.outputs()[k];
      auto& dst_output = dst.outputs()[k];
      dst_output.Allocate(arena);
      if (src_output.has_format()) {
        dst_output.set_format(arena, std::move(src_output.format()));
      }
      if (src_output.has_buffer()) {
        dst_output.set_buffer(arena, std::move(src_output.buffer()));
      }
      if (src_output.has_latency_frames()) {
        dst_output.set_latency_frames(arena, src_output.latency_frames());
      }
      if (src_output.has_ring_out_frames()) {
        dst_output.set_ring_out_frames(arena, src_output.ring_out_frames());
      }
    }
  }

  if (src.has_max_frames_per_call()) {
    dst.set_max_frames_per_call(arena, src.max_frames_per_call());
  }

  if (src.has_block_size_frames()) {
    dst.set_block_size_frames(arena, src.block_size_frames());
  }

  return dst;
}

}  // namespace

// static
EffectsStageV2::FidlBuffers EffectsStageV2::FidlBuffers::Create(
    const fuchsia_mem::wire::Range& input_range, const fuchsia_mem::wire::Range& output_range) {
  const auto input_end = input_range.offset + input_range.size;
  const auto output_end = output_range.offset + output_range.size;

  // Shared buffer: map the intersection of the input and output buffers.
  if (GetKoid(input_range.vmo) == GetKoid(output_range.vmo)) {
    auto mapper = fbl::MakeRefCounted<RefCountedVmoMapper>();
    auto shared_start = std::min(input_range.offset, output_range.offset);
    auto shared_end = std::max(input_end, output_end);
    auto status = mapper->Map(input_range.vmo, shared_start, shared_end - shared_start,
                              ZX_VM_PERM_READ | ZX_VM_PERM_WRITE);
    if (status != ZX_OK) {
      FX_PLOGS(FATAL, status) << "failed to map shared buffer with start=" << shared_start
                              << " end=" << shared_end;
    }

    return {
        .input = reinterpret_cast<char*>(mapper->start()) + (input_range.offset - shared_start),
        .output = reinterpret_cast<char*>(mapper->start()) + (output_range.offset - shared_start),
        .input_size = input_range.size,
        .output_size = output_range.size,
        .mappers = {std::move(mapper)},
    };
  }

  // Separate buffers: map separately.
  // We always write the input. We write the output if the IPC call fails.
  // Hence we map both R+W.
  auto input_mapper = fbl::MakeRefCounted<RefCountedVmoMapper>();
  if (auto status = input_mapper->Map(input_range.vmo, input_range.offset, input_range.size,
                                      ZX_VM_PERM_READ | ZX_VM_PERM_WRITE);
      status != ZX_OK) {
    FX_PLOGS(FATAL, status) << "failed to map input buffer with offset=" << input_range.offset
                            << " size=" << input_range.size;
  }

  auto output_mapper = fbl::MakeRefCounted<RefCountedVmoMapper>();
  if (auto status = output_mapper->Map(output_range.vmo, output_range.offset, output_range.size,
                                       ZX_VM_PERM_READ | ZX_VM_PERM_WRITE);
      status != ZX_OK) {
    FX_PLOGS(FATAL, status) << "failed to map output buffer with offset=" << output_range.offset
                            << " size=" << output_range.size;
  }

  return FidlBuffers{
      .input = input_mapper->start(),
      .output = output_mapper->start(),
      .input_size = input_range.size,
      .output_size = output_range.size,
      .mappers = {std::move(input_mapper), std::move(output_mapper)},
  };
}

// static
fpromise::result<std::shared_ptr<EffectsStageV2>, zx_status_t> EffectsStageV2::Create(
    fuchsia_audio_effects::wire::ProcessorConfiguration config,
    std::shared_ptr<ReadableStream> source) {
  TRACE_DURATION("audio", "EffectsStageV2::Create");

  // The arena's initial size doesn't matter: this is not on the critical path
  // so it's OK to allocate.
  fidl::Arena<128> arena;
  config = CloneConfigAndTakeHandles(arena, config);

  // Validate the ProcessorConfiguration.
  // NOTE: This implementation supports exactly one FLOAT input and one FLOAT output.
  if (!config.has_processor() || !config.processor().is_valid()) {
    FX_LOGS(ERROR) << "ProcessorConfiguration missing field 'processor'";
    return fpromise::error(ZX_ERR_INVALID_ARGS);
  }
  if (!config.has_inputs() || config.inputs().count() != 1) {
    FX_LOGS(ERROR) << "ProcessorConfiguration must have exactly one input stream";
    return fpromise::error(ZX_ERR_INVALID_ARGS);
  }
  if (!config.has_outputs() || config.outputs().count() != 1) {
    FX_LOGS(ERROR) << "ProcessorConfiguration must have exactly one input stream";
    return fpromise::error(ZX_ERR_INVALID_ARGS);
  }

  auto& input = config.inputs()[0];
  auto& output = config.outputs()[0];

  using ASF = fuchsia_mediastreams::wire::AudioSampleFormat;

  // Validate input/output format.
  if (!input.has_format() || input.format().sample_format != ASF::kFloat) {
    FX_LOGS(ERROR) << "ProcessorConfiguration.inputs[0].format must use FLOAT";
    return fpromise::error(ZX_ERR_INVALID_ARGS);
  }
  if (!output.has_format() || output.format().sample_format != ASF::kFloat) {
    FX_LOGS(ERROR) << "ProcessorConfiguration.outputs[0].format must use FLOAT";
    return fpromise::error(ZX_ERR_INVALID_ARGS);
  }
  if (input.format().frames_per_second != output.format().frames_per_second) {
    FX_LOGS(ERROR) << "ProcessorConfiguration input and output have different frame rates: "
                   << input.format().frames_per_second
                   << " != " << output.format().frames_per_second;
    return fpromise::error(ZX_ERR_INVALID_ARGS);
  }

  if (!input.has_buffer()) {
    FX_LOGS(ERROR) << "ProcessorConfiguration.inputs[0] missing field 'buffer'";
    return fpromise::error(ZX_ERR_INVALID_ARGS);
  }
  if (!output.has_buffer()) {
    FX_LOGS(ERROR) << "ProcessorConfiguration.outputs[0] missing field 'buffer'";
    return fpromise::error(ZX_ERR_INVALID_ARGS);
  }

  // Set defaults.
  const uint64_t bytes_per_frame = config.inputs()[0].format().channel_count * sizeof(float);
  const uint64_t default_max_frames_per_call = input.buffer().size / bytes_per_frame;

  if (!config.has_block_size_frames()) {
    config.set_block_size_frames(arena, 1);
  }
  if (!config.has_max_frames_per_call()) {
    config.set_max_frames_per_call(arena, default_max_frames_per_call);
  }
  if (!output.has_latency_frames()) {
    output.set_latency_frames(arena, 0);
  }
  if (!output.has_ring_out_frames()) {
    output.set_ring_out_frames(arena, 0);
  }

  // Ensure the block size is satisfiable.
  if (config.block_size_frames() > config.max_frames_per_call()) {
    FX_LOGS(ERROR) << "ProcessorConfiguration max_frames_per_call (" << config.max_frames_per_call()
                   << ") < block_size_frames (" << config.block_size_frames() << ")";
    return fpromise::error(ZX_ERR_INVALID_ARGS);
  }

  // Now round down max_frames_per_call so it satisfies the requested block size.
  config.set_max_frames_per_call(
      arena, fbl::round_down(config.max_frames_per_call(), config.block_size_frames()));

  // Validate buffer sizes.
  if (config.max_frames_per_call() > default_max_frames_per_call) {
    FX_LOGS(ERROR) << "ProcessorConfiguration max_frames_per_call (" << config.max_frames_per_call()
                   << ") > input buffer size (" << default_max_frames_per_call << " frames)";
    return fpromise::error(ZX_ERR_INVALID_ARGS);
  }

  // Validate that we won't crash when trying to access the input and output buffers.
  if (auto status = ValidateMemRange(true, input.buffer(), config); status != ZX_OK) {
    return fpromise::error(status);
  }
  if (auto status = ValidateMemRange(false, output.buffer(), config); status != ZX_OK) {
    return fpromise::error(status);
  }

  // Validate that the memory ranges do not overlap.
  if (PartialOverlap(input.buffer(), output.buffer())) {
    FX_LOGS(ERROR) << "ProcessorConfiguration: input and output buffers partially overlap";
    return fpromise::error(ZX_ERR_INVALID_ARGS);
  }

  // Validate that the configured format matches the source stream's format.
  if (source->format().sample_format() != fuchsia::media::AudioSampleFormat::FLOAT ||
      source->format().channels() != static_cast<int32_t>(input.format().channel_count) ||
      source->format().frames_per_second() !=
          static_cast<int32_t>(input.format().frames_per_second)) {
    FX_LOGS(ERROR) << "EffectsStageV2 source is {sample_format="
                   << static_cast<int>(source->format().sample_format())
                   << ", channels=" << source->format().channels()
                   << ", fps=" << source->format().frames_per_second()
                   << "}, expected {sample_format=FLOAT"
                   << ", channels=" << input.format().channel_count
                   << ", fps=" << input.format().frames_per_second << "}";
    return fpromise::error(ZX_ERR_INVALID_ARGS);
  }

  class MakeSharedEnabler : public EffectsStageV2 {
   public:
    MakeSharedEnabler(fuchsia_audio_effects::wire::ProcessorConfiguration config,
                      std::shared_ptr<ReadableStream> source)
        : EffectsStageV2(std::move(config), std::move(source)) {}
  };

  return fpromise::ok(std::make_shared<MakeSharedEnabler>(std::move(config), std::move(source)));
}

EffectsStageV2::EffectsStageV2(fuchsia_audio_effects::wire::ProcessorConfiguration config,
                               std::shared_ptr<ReadableStream> source)
    : ReadableStream("EffectsStageV2", ToOldFormat(config.outputs()[0].format())),
      source_(SilencePaddingStream::WrapIfNeeded(std::move(source),
                                                 Fixed(config.outputs()[0].ring_out_frames()),
                                                 /*fractional_gaps_round_down=*/false)),
      processor_(fidl::WireSyncClient(std::move(config.processor()))),
      fidl_buffers_(FidlBuffers::Create(config.inputs()[0].buffer(), config.outputs()[0].buffer())),
      max_frames_per_call_(config.max_frames_per_call()),
      block_size_frames_(config.block_size_frames()),
      output_shift_frames_(config.outputs()[0].latency_frames()),
      source_buffer_(source_->format(), max_frames_per_call_) {
  // Initialize our lead time. Passing 0 here will resolve to our effect's lead time
  // not counting the impact of any downstream processors.
  SetPresentationDelay(zx::duration(0));
}

std::optional<ReadableStream::Buffer> EffectsStageV2::ReadLockImpl(ReadLockContext& ctx,
                                                                   Fixed dest_frame,
                                                                   int64_t frame_count) {
  // ReadLockImpl should not be called until we've Trim'd past the last cached buffer.
  // See comments for ReadableStream::MakeCachedBuffer.
  FX_CHECK(!cache_);

  // EffectsStageV2 always produces data on integrally-aligned frames.
  dest_frame = Fixed(dest_frame.Floor());

  // Advance to our source's next available frame. This is needed when the source stream
  // contains gaps. For example, given a sequence of calls:
  //
  //   ReadLock(ctx, 0, 20)
  //   ReadLock(ctx, 20, 20)
  //
  // If our block size is 30, then at the first call, we will attempt to produce 30 frames
  // starting at frame 0. If the source has data for that range, we'll cache all 30 processed
  // frames and the second ReadLock call will be handled by our cache.
  //
  // However, if the source has no data for the range [0, 30), the first ReadLock call will
  // return std::nullopt. At the second call, we shouldn't ask the source for any frames
  // before frame 30 because we already know that range is empty.
  if (auto next_available = source_->NextAvailableFrame(); next_available) {
    // SampleAndHold: source frame 1.X overlaps dest frame 2.0, so always round up.
    int64_t frames_to_trim = next_available->Ceiling() - dest_frame.Floor();
    if (frames_to_trim > 0) {
      frame_count -= frames_to_trim;
      dest_frame += Fixed(frames_to_trim);
    }
  }

  while (frame_count > 0) {
    int64_t frames_read_from_source = FillCache(ctx, dest_frame, frame_count);
    if (cache_) {
      FX_CHECK(source_buffer_.length() > 0);
      FX_CHECK(cache_->dest_buffer);
      return MakeCachedBuffer(source_buffer_.start(), source_buffer_.length(), cache_->dest_buffer,
                              cache_->source_usage_mask, cache_->source_total_applied_gain_db);
    }

    // We tried to process an entire block, however the source had no data.
    // If frame_count > max_frames_per_call_, try the next block.
    dest_frame += Fixed(frames_read_from_source);
    frame_count -= frames_read_from_source;
  }

  // The source has no data for the requested range.
  return std::nullopt;
}

void EffectsStageV2::TrimImpl(Fixed dest_frame) {
  // EffectsStageV2 always produces data on integrally-aligned frames.
  dest_frame = Fixed(dest_frame.Floor());

  if (cache_ && dest_frame >= source_buffer_.end()) {
    cache_ = std::nullopt;
  }
  source_->Trim(dest_frame);
}

int64_t EffectsStageV2::FillCache(ReadLockContext& ctx, Fixed dest_frame, int64_t frame_count) {
  cache_ = std::nullopt;

  source_buffer_.Reset(dest_frame);
  auto source_usage_mask = StreamUsageMask();
  float source_total_applied_gain_db = 0;
  bool has_data = false;

  // The buffer must have a multiple of block_size_frames_ and at most max_frames_per_call_.
  // The buffer must have at most frame_count frames (ideally it has exactly that many).
  frame_count = static_cast<int64_t>(
      fbl::round_up(static_cast<uint64_t>(frame_count), static_cast<uint64_t>(block_size_frames_)));
  frame_count = std::min(frame_count, max_frames_per_call_);

  // Read frame_count frames.
  while (source_buffer_.length() < frame_count) {
    Fixed start = source_buffer_.end();
    int64_t frames_remaining = frame_count - source_buffer_.length();

    auto buf = source_->ReadLock(ctx, start, frames_remaining);
    if (buf) {
      // SampleAndHold: source frame 1.X overlaps dest frame 2.0, so always round up.
      source_buffer_.AppendData(Fixed(buf->start().Ceiling()), buf->length(), buf->payload());
      source_usage_mask.insert_all(buf->usage_mask());
      source_total_applied_gain_db = buf->total_applied_gain_db();
      has_data = true;
    } else {
      source_buffer_.AppendSilence(start, frames_remaining);
    }
  }

  if (block_size_frames_ > 0) {
    FX_CHECK(source_buffer_.length() % block_size_frames_ == 0)
        << "Bad buffer size " << source_buffer_.length() << " must be divisible by "
        << block_size_frames_;
  }

  // If the source had no frames, we don't need to process anything.
  if (!has_data) {
    return frame_count;
  }

  // Process this buffer.
  // The result will be in fidl_buffers_.output.
  CallProcess(ctx, source_usage_mask, source_total_applied_gain_db);

  // Cache the result.
  cache_ = Cache{
      .source_usage_mask = source_usage_mask,
      .source_total_applied_gain_db = source_total_applied_gain_db,
      .dest_buffer = fidl_buffers_.output,
  };

  return frame_count;
}

void EffectsStageV2::CallProcess(ReadLockContext& ctx, StreamUsageMask source_usage_mask,
                                 float source_total_applied_gain_db) {
  TRACE_DURATION("audio", "EffectsStageV2::CallProcess");

  std::array<float, 1> total_applied_gain_db_array = {source_total_applied_gain_db};
  std::array<uint32_t, 1> usage_mask_array = {source_usage_mask.mask() & kSupportedUsageMask};

  auto total_applied_gain_db_vector =
      fidl::VectorView<float>::FromExternal(total_applied_gain_db_array);
  auto usage_mask_vector = fidl::VectorView<uint32_t>::FromExternal(usage_mask_array);

  // This arena is just used to store one pointer per field of ProcessOptions.
  // The actual data is stored in the above arrays.
  fidl::Arena<64> arena;
  fuchsia_audio_effects::wire::ProcessOptions options(arena);
  options.set_total_applied_gain_db_per_input(
      fidl::ObjectView<fidl::VectorView<float>>::FromExternal(&total_applied_gain_db_vector));
  options.set_usage_mask_per_input(
      fidl::ObjectView<fidl::VectorView<uint32_t>>::FromExternal(&usage_mask_vector));

  // The source data needs to be copied into the pre-negotiated input buffer.
  memmove(fidl_buffers_.input, source_buffer_.payload(),
          source_buffer_.length() * static_cast<int64_t>(source_->format().bytes_per_frame()));

  // Synchronous IPC.
  StageMetricsTimer timer("EffectsStageV2::Process");
  timer.Start();

  auto num_frames = source_buffer_.length();
  auto result = processor_.buffer(process_buffer_.view())->Process(num_frames, options);
  auto status = result.status();
  if (result.ok() && result->is_error()) {
    status = result->error_value();
  }

  timer.Stop();
  ctx.AddStageMetrics(timer.Metrics());

  // On failure, zero the output buffer.
  if (status != ZX_OK) {
    memset(fidl_buffers_.output, 0, fidl_buffers_.output_size);
    // Log 1 error per 10s, assuming one call per 10ms.
    if ((fidl_error_count_++) % 1000 == 0) {
      FX_PLOGS(WARNING, status) << "Process call failed";
    } else {
      FX_PLOGS(DEBUG, status) << "Process call failed";
    }
    return;
  }

  // On success, update our metrics.
  auto& server_metrics = result->value()->per_stage_metrics;
  for (size_t k = 0; k < server_metrics.count(); k++) {
    StageMetrics metrics;
    if (server_metrics[k].has_name()) {
      metrics.name.Append(server_metrics[k].name().get());
    } else {
      metrics.name.AppendPrintf("EffectsStageV2::stage%lu", k);
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
    ctx.AddStageMetrics(metrics);
  }
}

BaseStream::TimelineFunctionSnapshot EffectsStageV2::ref_time_to_frac_presentation_frame() const {
  auto snapshot = source_->ref_time_to_frac_presentation_frame();

  // Update our timeline function to include the latency introduced by these effects.
  //
  // Our effects shift incoming audio into the future by `output_shift_frames_`.
  // So input frame[N] corresponds to output frame[N + output_shift_frames_].
  auto delay_frac_frames = Fixed(output_shift_frames_);

  auto source_frac_frame_to_dest_frac_frame =
      TimelineFunction(delay_frac_frames.raw_value(), 0, TimelineRate(1, 1));
  snapshot.timeline_function = source_frac_frame_to_dest_frac_frame * snapshot.timeline_function;

  return snapshot;
}

void EffectsStageV2::SetPresentationDelay(zx::duration external_delay) {
  // Add in any additional lead time required by our effects.
  zx::duration intrinsic_lead_time = ComputeIntrinsicMinLeadTime();
  zx::duration total_delay = external_delay + intrinsic_lead_time;

  if constexpr (kLogPresentationDelay) {
    FX_LOGS(WARNING) << "(" << this << ") " << __FUNCTION__ << " given external_delay "
                     << external_delay.to_nsecs() << "ns";
    FX_LOGS(WARNING) << "Adding it to our intrinsic_lead_time " << intrinsic_lead_time.to_nsecs()
                     << "ns; setting our total_delay " << total_delay.to_nsecs() << "ns";
  }

  // Apply the total lead time to us and propagate that value to our source.
  ReadableStream::SetPresentationDelay(total_delay);
  source_->SetPresentationDelay(total_delay);
}

zx::duration EffectsStageV2::ComputeIntrinsicMinLeadTime() const {
  TimelineRate ticks_per_frame = format().frames_per_ns().Inverse();
  int64_t lead_frames = output_shift_frames_;
  if (block_size_frames_ > 0) {
    // If we have a block size, include that in the lead time.
    lead_frames += block_size_frames_ - 1;
  }
  return zx::duration(ticks_per_frame.Scale(lead_frames));
}

}  // namespace media::audio
