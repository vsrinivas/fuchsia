// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/affine/transform.h>
#include <lib/zx/clock.h>
#include <zircon/time.h>
#include <zircon/types.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

#include <audio-utils/audio-input.h>
#include <audio-utils/audio-stream.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>

namespace audio {
namespace utils {

static constexpr zx::duration kDesiredWakeupPeriod = zx::msec(50);
static constexpr float MIN_DURATION = 0.100f;
static constexpr float MAX_DURATION = 86400.0f;

std::unique_ptr<AudioInput> AudioInput::Create(uint32_t dev_id) {
  fbl::AllocChecker ac;
  std::unique_ptr<AudioInput> res(new (&ac) AudioInput(dev_id));
  if (!ac.check())
    return nullptr;
  return res;
}

std::unique_ptr<AudioInput> AudioInput::Create(const char* dev_path) {
  fbl::AllocChecker ac;
  std::unique_ptr<AudioInput> res(new (&ac) AudioInput(dev_path));
  if (!ac.check())
    return nullptr;
  return res;
}

zx_status_t AudioInput::Record(AudioSink& sink, Duration duration) {
  auto res = RecordPrepare(sink);
  if (res != ZX_OK)
    return res;
  res = StartRingBuffer();
  if (res != ZX_OK) {
    printf("Failed to start capture (res %d)\n", res);
    return res;
  }
  return RecordToCompletion(sink, duration);
}

zx_status_t AudioInput::RecordPrepare(AudioSink& sink) {
  AudioStream::Format fmt = {
      .frame_rate = frame_rate_,
      .channels = static_cast<uint16_t>(channel_cnt_),
      .sample_format = sample_format_,
  };

  zx_status_t res = sink.SetFormat(fmt);
  if (res != ZX_OK) {
    printf("Failed to set sink format (rate %u, chan_count %u, fmt 0x%08x, res %d)\n", frame_rate_,
           channel_cnt_, sample_format_, res);
    return res;
  }

  // Make sure we have a ring buffer size at least a FIFO depth + 2 x desired wake up period,
  // rounded up to a page size.
  uint64_t ring_bytes_64 =
      fifo_depth_ +
      (zx_duration_mul_int64(2 * kDesiredWakeupPeriod.get(), frame_rate_) / ZX_SEC(1)) * frame_sz_;
  if (ring_bytes_64 > std::numeric_limits<uint32_t>::max()) {
    printf("Invalid frame rate %u\n", frame_rate_);
    return res;
  }
  ring_bytes_64 = fbl::round_up(ring_bytes_64, static_cast<uint64_t>(PAGE_SIZE));

  uint32_t ring_bytes = static_cast<uint32_t>(ring_bytes_64);
  uint32_t ring_frames = ring_bytes / frame_sz_;

  res = GetBuffer(ring_frames, 8u);
  if (res != ZX_OK) {
    printf("Failed to establish ring buffer (%u frames, res %d)\n", ring_frames, res);
    return res;
  }

  return res;
}

zx_status_t AudioInput::RecordToCompletion(AudioSink& sink, Duration duration) {
  zx_status_t res = ZX_OK;
  long frames_expected = 0;
  int64_t bytes_expected = 0;
  const bool loop = std::holds_alternative<LoopingDoneCallback>(duration);
  if (!loop) {
    std::get<float>(duration) = std::clamp(std::get<float>(duration), MIN_DURATION, MAX_DURATION);
    printf("Recording for %.1f seconds\n", std::get<float>(duration));
    frames_expected = std::lround(frame_rate_ * std::get<float>(duration));
    bytes_expected = frame_sz_ * frames_expected;
  }

  uint32_t rd_ptr = 0;    // Our read ptr for the ring buffer.
  uint32_t wr_ptr = 0;    // Estimated write ptr in the ring buffer.
  uint32_t consumed = 0;  // Total bytes consumed.
  uint32_t produced = 0;  // Estimated total bytes produced.

  // A transformation from time to bytes captured safe to read.
  // We initialize next_wake_time to a FIFO from stat_time_ to make sure we are behind the HW.
  auto mono_to_safe_read_bytes = affine::Transform{static_cast<int64_t>(start_time_),
                                                   -static_cast<int64_t>(fifo_depth_),
                                                   {frame_rate_ * frame_sz_, zx::sec(1).get()}};
  auto next_wake_time = zx::time(mono_to_safe_read_bytes.ApplyInverse(0));

  // Repeat until looping is done or until consumed >= bytes_expected.
  while ((loop && std::get<LoopingDoneCallback>(duration)()) ||
         (!loop && consumed < bytes_expected)) {
    // Set next wake to either (the larger):
    // - At least a FIFO depth away.
    // - The desired wakeup period.
    auto a_fifo_away = mono_to_safe_read_bytes.ApplyInverse(
        mono_to_safe_read_bytes.Apply(zx::clock::get_monotonic().get()) + fifo_depth_);
    next_wake_time = std::max(zx::time(a_fifo_away), next_wake_time + kDesiredWakeupPeriod);
    if (next_wake_time > zx::clock::get_monotonic()) {
      zx::nanosleep(next_wake_time);
    }
    auto safe_read = mono_to_safe_read_bytes.Apply(zx::clock::get_monotonic().get());

    if (loop) {
      consumed = static_cast<uint32_t>(safe_read) - (static_cast<uint32_t>(safe_read) % frame_sz_);
    } else {
      consumed = std::min(safe_read, bytes_expected);
    }
    uint32_t increment = fbl::round_down(consumed - produced, frame_sz_);

    wr_ptr += increment;
    produced += increment;
    wr_ptr %= rb_sz_;

    uint32_t todo = wr_ptr + rb_sz_ - rd_ptr;
    todo %= rb_sz_;

    ZX_DEBUG_ASSERT(todo < rb_sz_);
    ZX_DEBUG_ASSERT(rd_ptr < rb_sz_);

    uint32_t space = rb_sz_ - rd_ptr;
    uint32_t amt = std::min(space, todo);
    auto data = static_cast<const uint8_t*>(rb_virt_) + rd_ptr;

    res = zx_cache_flush(data, amt, ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE);
    if (res != ZX_OK) {
      printf("Failed to cache invalidate(res %d).\n", res);
      break;
    }

    res = sink.PutFrames(data, amt);
    if (res != ZX_OK) {
      printf("Failed to record %u bytes (res %d)\n", amt, res);
      break;
    }

    if (amt < todo) {
      amt = todo - amt;
      ZX_DEBUG_ASSERT(amt < rb_sz_);

      res = zx_cache_flush(rb_virt_, amt, ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE);
      if (res != ZX_OK) {
        printf("Failed to cache invalidate(res %d) %d\n", res, __LINE__);
        break;
      }

      res = sink.PutFrames(rb_virt_, amt);
      if (res != ZX_OK) {
        printf("Failed to record %u bytes (res %d)\n", amt, res);
        break;
      }

      rd_ptr = amt;
    } else {
      rd_ptr += amt;
      if (rd_ptr >= rb_sz_) {
        ZX_DEBUG_ASSERT(rd_ptr == rb_sz_);
        rd_ptr = 0;
      }
    }
  }

  StopRingBuffer();

  zx_status_t finalize_res = sink.Finalize();
  return (res == ZX_OK) ? finalize_res : res;
}

}  // namespace utils
}  // namespace audio
