// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <string.h>
#include <zircon/device/audio.h>

#include <algorithm>
#include <memory>

#include <audio-utils/audio-output.h>
#include <audio-utils/audio-stream.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>

namespace audio {
namespace utils {

std::unique_ptr<AudioOutput> AudioOutput::Create(uint32_t dev_id) {
  fbl::AllocChecker ac;
  std::unique_ptr<AudioOutput> res(new (&ac) AudioOutput(dev_id));
  if (!ac.check())
    return nullptr;
  return res;
}

std::unique_ptr<AudioOutput> AudioOutput::Create(const char* dev_path) {
  fbl::AllocChecker ac;
  std::unique_ptr<AudioOutput> res(new (&ac) AudioOutput(dev_path));
  if (!ac.check())
    return nullptr;
  return res;
}

zx_status_t AudioOutput::Play(AudioSource& source) {
  auto res = PlayPrepare(source);
  if (res != ZX_OK)
    return res;
  res = StartRingBuffer();
  if (res != ZX_OK) {
    printf("Failed to start playback (res %d)\n", res);
    return res;
  }
  return PlayToCompletion(source);
}

zx_status_t AudioOutput::PlayPrepare(AudioSource& source) {
  zx_status_t res;

  if (source.finished())
    return ZX_OK;

  AudioSource::Format format = {};
  res = source.GetFormat(&format);
  if (res != ZX_OK) {
    printf("Failed to get source's format (res %d)\n", res);
    return res;
  }

  res = SetFormat(format.frame_rate, format.channels, format.channels_to_use_bitmask,
                  format.sample_format);
  if (res != ZX_OK) {
    printf("Failed to set source format [%u Hz, %hu Chan, %016lx Mask, %08x fmt] (res %d)\n",
           format.frame_rate, format.channels, format.channels_to_use_bitmask, format.sample_format,
           res);
    return res;
  }

  // TODO(112985): Restore QEMU support. ALSA under QEMU required huge buffers.
  // Add the ability to determine what type of read-ahead the
  // HW is going to require so we can adjust our buffer size to what the HW
  // requires, not what ALSA under QEMU requires.
  ZX_ASSERT(format.frame_rate > 1000);  // Only a reasonable rate can be used to get a reasonable
                                        // ring buffer.
  const uint32_t ring_buffer_frames = format.frame_rate / 10;  // 100msecs.
  constexpr uint32_t kInterruptsPerRingBuffer = 3;
  res = GetBuffer(ring_buffer_frames, kInterruptsPerRingBuffer);
  if (res != ZX_OK) {
    printf("Failed to set output format (res %d)\n", res);
    return res;
  }

  memset(rb_virt_, 0, rb_sz_);

  // Write up to half the ring buffer to allow playback start.
  auto buf = reinterpret_cast<uint8_t*>(rb_virt_);
  res = source.GetFrames(buf, rb_sz_ / 2, &bytes_written_);
  if (res != ZX_OK) {
    printf("Error packing frames (res %d)\n", res);
    return res;
  }
  zx_cache_flush(buf, bytes_written_, ZX_CACHE_FLUSH_DATA);
  return ZX_OK;
}

zx_status_t AudioOutput::PlayToCompletion(AudioSource& source) {
  auto buf = reinterpret_cast<uint8_t*>(rb_virt_);
  uint32_t rd, wr;
  uint32_t playout_rd, playout_amt;
  zx_status_t res = ZX_OK;
  rd = 0;
  wr = bytes_written_;
  playout_rd = playout_amt = 0;

  while (true) {
    // Top up the buffer.  In theory, we should only need to loop 2 times in
    // order to handle a ring discontinuity
    for (uint32_t i = 0; i < 2; ++i) {
      uint32_t space = (rb_sz_ + rd - wr - 1) % rb_sz_;
      uint32_t todo = std::min(space, rb_sz_ - wr);
      ZX_DEBUG_ASSERT(space < rb_sz_);

      if (!todo)
        break;

      if (source.finished()) {
        memset(buf + wr, 0, todo);
        zx_cache_flush(buf + wr, todo, ZX_CACHE_FLUSH_DATA);

        wr += todo;
      } else {
        uint32_t done;
        res = source.GetFrames(buf + wr, std::min(space, rb_sz_ - wr), &done);
        if (res != ZX_OK) {
          printf("Error packing frames (res %d)\n", res);
          break;
        }
        zx_cache_flush(buf + wr, done, ZX_CACHE_FLUSH_DATA);
        wr += done;

        if (source.finished()) {
          playout_rd = rd;
          playout_amt = (rb_sz_ + wr - rd) % rb_sz_;

          // We have just become finished.  Reset the loop counter and
          // start over, this time filling with as much silence as we
          // can.
          i = 0;
        }
      }

      if (wr < rb_sz_)
        break;

      ZX_DEBUG_ASSERT(wr == rb_sz_);
      wr = 0;
    }

    if (res != ZX_OK)
      break;

    auto position = fidl::WireCall(rb_ch_)->WatchClockRecoveryPositionInfo();

    rd = position.value().position_info.position;

    // rd has moved.  If the source has finished and rd has moved at least
    // the playout distance, we are finshed.
    if (source.finished()) {
      uint32_t dist = (rb_sz_ + rd - playout_rd) % rb_sz_;

      if (dist >= playout_amt)
        break;

      playout_amt -= dist;
      playout_rd = rd;
    }
  }

  if (res == ZX_OK) {
    // We have already let the DMA engine catch up, but we still need to
    // wait for the fifo to play out.  For now, just hard code this as
    // 30uSec.
    //
    // TODO: base this on the start time and the number of frames queued
    // instead of just making a number up.
    zx_nanosleep(zx_deadline_after(ZX_MSEC(30)));
  }

  zx_status_t stop_res = StopRingBuffer();
  if (res == ZX_OK)
    res = stop_res;

  return res;
}

}  // namespace utils

}  // namespace audio
