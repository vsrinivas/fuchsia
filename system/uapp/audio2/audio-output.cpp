// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/device/audio2.h>
#include <mxtl/algorithm.h>
#include <mxtl/limits.h>
#include <stdio.h>
#include <string.h>

#include "audio-output.h"
#include "audio-source.h"

mx_status_t AudioOutput::Play(AudioSource& source) {
    mx_status_t res;

    if (source.finished())
        return NO_ERROR;

    AudioSource::Format format;
    res = source.GetFormat(&format);
    if (res != NO_ERROR) {
        printf("Failed to get source's format (res %d)\n", res);
        return res;
    }

    res = SetFormat(format.frame_rate, format.channels, format.sample_format);
    if (res != NO_ERROR) {
        printf("Failed to set source format [%u Hz, %hu Chan, %08x fmt] (res %d)\n",
                format.frame_rate, format.channels, format.sample_format, res);
        return res;
    }

    // ALSA under QEMU required huge buffers.
    //
    // TODO(johngro) : Add the ability to determine what type of read-ahead the
    // HW is going to require so we can adjust our buffer size to what the HW
    // requires, not what ALSA under QEMU requires.
    res = GetBuffer(480 * 20 * 3, 3);
    if (res != NO_ERROR) {
        printf("Failed to set output format (res %d)\n", res);
        return res;
    }

    memset(rb_virt_, 0, rb_sz_);

    auto buf = reinterpret_cast<uint8_t*>(rb_virt_);
    uint32_t rd, wr;
    uint32_t playout_rd, playout_amt;
    bool started = false;
    rd = wr = 0;
    playout_rd = playout_amt = 0;

    while (true) {
        uint32_t bytes_read, junk;
        audio2_rb_position_notify_t pos_notif;
        mx_signals_t sigs;

        // Top up the buffer.  In theory, we should only need to loop 2 times in
        // order to handle a ring discontinuity
        for (uint32_t i = 0; i < 2; ++i) {
            uint32_t space = (rb_sz_ + rd - wr - 1) % rb_sz_;
            uint32_t todo  = mxtl::min(space, rb_sz_ - wr);
            MX_DEBUG_ASSERT(space < rb_sz_);

            if (!todo)
                break;

            if (source.finished()) {
                memset(buf + wr, 0, todo);
                wr += todo;
            } else {
                uint32_t done;
                res = source.PackFrames(buf + wr, mxtl::min(space, rb_sz_ - wr), &done);
                if (res != NO_ERROR) {
                    printf("Error packing frames (res %d)\n", res);
                    break;
                }
                wr += done;

                if (source.finished()) {
                    playout_rd  = rd;
                    playout_amt = (rb_sz_ + wr - rd) % rb_sz_;

                    // We have just become finished.  Reset the loop counter and
                    // start over, this time filling with as much silence as we
                    // can.
                    i = 0;
                }
            }

            if (wr < rb_sz_)
                break;

            MX_DEBUG_ASSERT(wr == rb_sz_);
            wr = 0;
        }

        if (res != NO_ERROR)
            break;

        // If we have not started yet, do so.
        if (!started) {
            res = StartRingBuffer();
            if (res != NO_ERROR) {
                printf("Failed to start ring buffer!\n");
                break;
            }
            started = true;
        }

        res = rb_ch_.wait_one(MX_CHANNEL_READABLE, MX_TIME_INFINITE, &sigs);

        if (res != NO_ERROR) {
            printf("Failed to wait for notificiation (res %d)\n", res);
            break;
        }

        res = rb_ch_.read(0,
                          &pos_notif, sizeof(pos_notif), &bytes_read,
                          nullptr, 0, &junk);
        if (res != NO_ERROR) {
            printf("Failed to read notification from ring buffer channel (res %d)\n", res);
            break;
        }

        if (bytes_read != sizeof(pos_notif)) {
            printf("Bad size when reading notification from ring buffer channel (%u != %zu)\n",
                   bytes_read, sizeof(pos_notif));
            res = ERR_INTERNAL;
            break;
        }

        if (pos_notif.hdr.cmd != AUDIO2_RB_POSITION_NOTIFY) {
            printf("Unexpected command type when reading notification from ring "
                   "buffer channel (cmd %04x)\n", pos_notif.hdr.cmd);
            res = ERR_INTERNAL;
            break;
        }

        rd = pos_notif.ring_buffer_pos;

        // rd has moved.  If the source has finished and rd has moved at least
        // the playout distance, we are finsihed.
        if (source.finished()) {
            uint32_t dist = (rb_sz_ + rd - playout_rd) % rb_sz_;

            if (dist >= playout_amt)
                break;

            playout_amt -= dist;
            playout_rd   = rd;
        }
    }

    if (res == NO_ERROR) {
        // We have already let the DMA engine catch up, but we still need to
        // wait for the fifo to play out.  For now, just hard code this as
        // 30uSec.
        //
        // TODO: base this on the start time and the number of frames queued
        // instead of just making a number up.
        mx_nanosleep(mx_deadline_after(MX_MSEC(30)));
    }

    mx_status_t stop_res = StopRingBuffer();
    if (res == NO_ERROR)
        res = stop_res;

    return res;
}
