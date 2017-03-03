// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <inttypes.h>
#include <magenta/assert.h>
#include <magenta/device/audio2.h>
#include <magenta/process.h>
#include <magenta/syscalls.h>
#include <mx/channel.h>
#include <mx/handle.h>
#include <mx/vmo.h>
#include <mxtl/algorithm.h>
#include <mxtl/limits.h>
#include <mxio/io.h>
#include <stdio.h>
#include <string.h>

#include "audio-output.h"

template <typename ReqType, typename RespType>
mx_status_t DoCall(const mx::channel& channel,
                   const ReqType&     req,
                   RespType*          resp,
                   mx::handle*        resp_handle_out = nullptr) {
    constexpr mx_time_t CALL_TIMEOUT = 100000000u;
    mx_channel_call_args_t args;

    MX_DEBUG_ASSERT((resp_handle_out == nullptr) || !resp_handle_out->is_valid());

    args.wr_bytes       = const_cast<ReqType*>(&req);
    args.wr_num_bytes   = sizeof(ReqType);
    args.wr_handles     = nullptr;
    args.wr_num_handles = 0;
    args.rd_bytes       = resp;
    args.rd_num_bytes   = sizeof(RespType);
    args.rd_handles     = resp_handle_out ? resp_handle_out->get_address() : nullptr;
    args.rd_num_handles = resp_handle_out ? 1 : 0;

    uint32_t bytes, handles;
    mx_status_t read_status, write_status;

    write_status = channel.call(0, CALL_TIMEOUT, &args, &bytes, &handles, &read_status);

    if (write_status != NO_ERROR) {
        if (write_status == ERR_CALL_FAILED) {
            printf("Cmd read failure (cmd %04x, res %d)\n", req.hdr.cmd, read_status);
            return read_status;
        } else {
            printf("Cmd write failure (cmd %04x, res %d)\n", req.hdr.cmd, write_status);
            return write_status;
        }
    }

    if (bytes != sizeof(RespType)) {
        printf("Unexpected response size (got %u, expected %zu)\n", bytes, sizeof(RespType));
        return ERR_INTERNAL;
    }

    return resp->result;
}

mx_status_t AudioOutput::Open(const char* stream_name) {
    if (stream_ch_ != MX_HANDLE_INVALID)
        return ERR_BAD_STATE;

    int fd = ::open(stream_name, O_RDONLY);
    if (fd < 0) {
        printf("Failed to open \"%s\" (res %d)\n", stream_name, fd);
        return fd;
    }

    ssize_t res = ::mxio_ioctl(fd, AUDIO2_IOCTL_GET_CHANNEL,
                               nullptr, 0,
                               &stream_ch_, sizeof(stream_ch_));
    ::close(fd);

    if (res != NO_ERROR) {
        printf("Failed to obtain channel (res %zd)\n", res);
        return static_cast<mx_status_t>(res);
    }

    return NO_ERROR;
}

mx_status_t AudioOutput::SetFormat(uint32_t frames_per_second,
                                   uint16_t channels,
                                   audio2_sample_format_t sample_format) {
    if ((stream_ch_ == MX_HANDLE_INVALID) || (rb_ch_ != MX_HANDLE_INVALID))
        return ERR_BAD_STATE;

    switch (sample_format) {
    case AUDIO2_SAMPLE_FORMAT_8BIT:         sample_size_ = 1; break;
    case AUDIO2_SAMPLE_FORMAT_16BIT:        sample_size_ = 2; break;
    case AUDIO2_SAMPLE_FORMAT_24BIT_PACKED: sample_size_ = 3; break;
    case AUDIO2_SAMPLE_FORMAT_20BIT_IN32:
    case AUDIO2_SAMPLE_FORMAT_24BIT_IN32:
    case AUDIO2_SAMPLE_FORMAT_32BIT:
    case AUDIO2_SAMPLE_FORMAT_32BIT_FLOAT:  sample_size_ = 4; break;
    default: return ERR_NOT_SUPPORTED;
    }

    channel_cnt_ = channels;
    frame_sz_    = channels * sample_size_;
    frame_rate_  = frames_per_second;

    audio2_stream_cmd_set_format_req_t  req;
    audio2_stream_cmd_set_format_resp_t resp;
    req.hdr.cmd            = AUDIO2_STREAM_CMD_SET_FORMAT;
    req.hdr.transaction_id = 1;
    req.frames_per_second  = frames_per_second;
    req.channels           = channels;
    req.sample_format      = sample_format;

    mx::handle tmp;
    mx_status_t res = DoCall(stream_ch_, req, &resp, &tmp);
    if (res != NO_ERROR) {
        printf("Failed to set format %uHz %hu-Ch fmt 0x%x (res %d)\n",
                frames_per_second, channels, sample_format, res);
    }

    // TODO(johngro) : Verify the type of this handle before transfering it to
    // our ring buffer channel handle.
    rb_ch_.reset(tmp.release());

    return res;
}

mx_status_t AudioOutput::GetBuffer(uint32_t frames, uint32_t irqs_per_ring) {
    if(!frames)
        return ERR_INVALID_ARGS;

    if (!rb_ch_.is_valid() || rb_vmo_.is_valid() || !frame_sz_)
        return ERR_BAD_STATE;

    // Get a VMO representing the ring buffer we will share with the audio driver.
    audio2_rb_cmd_get_buffer_req_t  req;
    audio2_rb_cmd_get_buffer_resp_t resp;

    req.hdr.cmd                = AUDIO2_RB_CMD_GET_BUFFER;
    req.hdr.transaction_id     = 1;
    req.min_ring_buffer_frames = frames;
    req.notifications_per_ring = irqs_per_ring;

    mx::handle tmp;
    mx_status_t res;
    res = DoCall(rb_ch_, req, &resp, &tmp);

    if ((res == NO_ERROR) && (resp.result != NO_ERROR))
        res = resp.result;

    if (res != NO_ERROR) {
        printf("Failed to get driver ring buffer VMO (res %d)\n", res);
        return res;
    }

    // TODO(johngro) : Verify the type of this handle before transfering it to our VMO handle.
    rb_vmo_.reset(tmp.release());

    // We have the buffer, fetch the size the driver finally decided on.
    uint64_t rb_sz;
    res = rb_vmo_.get_size(&rb_sz);
    if (res != NO_ERROR) {
        printf("Failed to fetch ring buffer VMO size (res %d)\n", res);
        return res;
    }

    // Sanity check the size and stash it if it checks out.
    if ((rb_sz > mxtl::numeric_limits<decltype(rb_sz_)>::max()) || ((rb_sz % frame_sz_) != 0)) {
        printf("Bad VMO size returned by audio driver! (size = %" PRIu64 " frame_sz = %u)\n",
                rb_sz, frame_sz_);
        return ERR_INVALID_ARGS;
    }

    rb_sz_ = static_cast<decltype(rb_sz_)>(rb_sz);

    // Map the VMO into our address space
    // TODO(johngro) : How do I specify the cache policy for this mapping?
    res = mx_vmar_map(mx_vmar_root_self(), 0u,
                      rb_vmo_.get(), 0u, rb_sz_,
                      MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE,
                      reinterpret_cast<uintptr_t*>(&rb_virt_));
    if (res != NO_ERROR) {
        printf("Failed to map ring buffer VMO (res %d)\n", res);
        return res;
    }

    // Success!  zero out the buffer and we are done.
    memset(rb_virt_, 0, rb_sz_);
    return NO_ERROR;
}

mx_status_t AudioOutput::StartRingBuffer() {
    if (rb_ch_ == MX_HANDLE_INVALID)
        return ERR_BAD_STATE;

    audio2_rb_cmd_start_req_t  req;
    audio2_rb_cmd_start_resp_t resp;

    req.hdr.cmd = AUDIO2_RB_CMD_START;
    req.hdr.transaction_id = 1;

    return DoCall(rb_ch_, req, &resp);
}

mx_status_t AudioOutput::StopRingBuffer() {
    if (rb_ch_ == MX_HANDLE_INVALID)
        return ERR_BAD_STATE;

    audio2_rb_cmd_stop_req_t  req;
    audio2_rb_cmd_stop_resp_t resp;

    req.hdr.cmd = AUDIO2_RB_CMD_STOP;
    req.hdr.transaction_id = 1;

    return DoCall(rb_ch_, req, &resp);
}

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
        mx_nanosleep(30000000);
    }

    mx_status_t stop_res = StopRingBuffer();
    if (res == NO_ERROR)
        res = stop_res;

    return res;
}
