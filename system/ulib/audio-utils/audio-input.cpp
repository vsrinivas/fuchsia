// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <audio-utils/audio-input.h>
#include <audio-utils/audio-stream.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/limits.h>

namespace audio {
namespace utils {

static constexpr mx_time_t CHUNK_TIME = MX_MSEC(100);
static constexpr float MIN_DURATION = 0.100f;
static constexpr float MAX_DURATION = 86400.0f;

fbl::unique_ptr<AudioInput> AudioInput::Create(uint32_t dev_id) {
    fbl::AllocChecker ac;
    fbl::unique_ptr<AudioInput> res(new (&ac) AudioInput(dev_id));
    if (!ac.check())
        return nullptr;
    return res;
}

fbl::unique_ptr<AudioInput> AudioInput::Create(const char* dev_path) {
    fbl::AllocChecker ac;
    fbl::unique_ptr<AudioInput> res(new (&ac) AudioInput(dev_path));
    if (!ac.check())
        return nullptr;
    return res;
}

mx_status_t AudioInput::Record(AudioSink& sink, float duration_seconds) {
    AudioStream::Format fmt = {
        .frame_rate = frame_rate_,
        .channels = static_cast<uint16_t>(channel_cnt_),
        .sample_format = sample_format_,
    };

    duration_seconds = fbl::clamp(duration_seconds, MIN_DURATION, MAX_DURATION);

    mx_status_t res = sink.SetFormat(fmt);
    if (res != MX_OK) {
        printf("Failed to set sink format (rate %u, chan_count %u, fmt 0x%08x, res %d)\n",
                frame_rate_, channel_cnt_, sample_format_, res);
        return res;
    }

    uint64_t ring_bytes_64 = ((CHUNK_TIME * frame_rate_) / MX_SEC(1)) * frame_sz_;
    if (ring_bytes_64 > fbl::numeric_limits<uint32_t>::max()) {
        printf("Invalid frame rate %u\n", frame_rate_);
        return res;
    }

    uint32_t ring_bytes  = static_cast<uint32_t>(ring_bytes_64);
    uint32_t ring_frames = ring_bytes / frame_sz_;

    res = GetBuffer(ring_frames, 2u);
    if (res != MX_OK) {
        printf("Failed to establish ring buffer (%u frames, res %d)\n",
                ring_frames, res);
        return res;
    }

    mx_time_t duration_nsec = static_cast<mx_time_t>(MX_SEC(1)
                            * static_cast<double>(duration_seconds));
    mx_time_t stop_time = mx_time_get(MX_CLOCK_MONOTONIC) + duration_nsec;
    printf("Recording for %.1f seconds\n", duration_seconds);

    res = StartRingBuffer();
    if (res != MX_OK) {
        printf("Failed to start capture (res %d)\n", res);
        return res;
    }

    uint32_t  rd_ptr = 0;
    bool      peer_connected = true;
    while (true) {
        mx_signals_t sigs;

        res = rb_ch_.wait_one(MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED,
                              stop_time, &sigs);

        // If we get a timeout error, we have hit our stop time.
        if (res == MX_ERR_TIMED_OUT) break;

        if (res != MX_OK) {
            printf("Failed to wait for notificiation (res %d)\n", res);
            break;
        }

        if (sigs & MX_CHANNEL_PEER_CLOSED) {
            printf("Peer closed connection during record!\n");
            peer_connected = false;
            break;
        }

        audio_rb_position_notify_t pos_notif;

        uint32_t bytes_read, junk;
        res = rb_ch_.read(0,
                          &pos_notif, sizeof(pos_notif), &bytes_read,
                          nullptr, 0, &junk);
        if (res != MX_OK) {
            printf("Failed to read notification from ring buffer channel (res %d)\n", res);
            break;
        }

        if (bytes_read != sizeof(pos_notif)) {
            printf("Bad size when reading notification from ring buffer channel (%u != %zu)\n",
                   bytes_read, sizeof(pos_notif));
            res = MX_ERR_INTERNAL;
            break;
        }

        if (pos_notif.hdr.cmd != AUDIO_RB_POSITION_NOTIFY) {
            printf("Unexpected command type when reading notification from ring "
                   "buffer channel (cmd %04x)\n", pos_notif.hdr.cmd);
            res = MX_ERR_INTERNAL;
            break;
        }

        uint32_t todo = pos_notif.ring_buffer_pos + rb_sz_ - rd_ptr;
        if (todo >= rb_sz_)
            todo -= rb_sz_;

        MX_DEBUG_ASSERT(todo < rb_sz_);
        MX_DEBUG_ASSERT(rd_ptr < rb_sz_);

        uint32_t space = rb_sz_ - rd_ptr;
        uint32_t amt = fbl::min(space, todo);
        auto data = static_cast<const uint8_t*>(rb_virt_) + rd_ptr;

        res = sink.PutFrames(data, amt);
        if (res != MX_OK) {
            printf("Failed to record %u bytes (res %d)\n", amt, res);
            break;
        }

        if (amt < todo) {
            amt = todo - amt;
            MX_DEBUG_ASSERT(amt < ring_bytes);

            res = sink.PutFrames(rb_virt_, amt);
            if (res != MX_OK) {
                printf("Failed to record %u bytes (res %d)\n", amt, res);
                break;
            }

            rd_ptr = amt;
        } else {
            rd_ptr += amt;
            if (rd_ptr >= ring_bytes) {
                MX_DEBUG_ASSERT(rd_ptr == ring_bytes);
                rd_ptr = 0;
            }
        }
    }

    if (peer_connected) {
        StopRingBuffer();
    }

    mx_status_t finalize_res = sink.Finalize();
    return (res == MX_OK) ? finalize_res : res;
}

}  // namespace utils
}  // namespace audio
