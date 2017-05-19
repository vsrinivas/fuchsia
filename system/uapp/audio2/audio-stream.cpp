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
#include <mxalloc/new.h>
#include <mxtl/algorithm.h>
#include <mxtl/auto_call.h>
#include <mxtl/limits.h>
#include <mxio/io.h>
#include <stdio.h>
#include <string.h>

#include "audio-input.h"
#include "audio-output.h"
#include "audio-stream.h"

template <typename ReqType, typename RespType>
mx_status_t DoCallImpl(const mx::channel& channel,
                       const ReqType&     req,
                       RespType*          resp,
                       mx::handle*        resp_handle_out) {
    constexpr mx_duration_t CALL_TIMEOUT = MX_MSEC(100);
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

    write_status = channel.call(0, mx_deadline_after(CALL_TIMEOUT), &args, &bytes, &handles,
                                &read_status);

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

    return NO_ERROR;
}

template <typename ReqType, typename RespType>
mx_status_t DoCall(const mx::channel& channel,
                   const ReqType&     req,
                   RespType*          resp,
                   mx::handle*        resp_handle_out = nullptr) {
    mx_status_t res = DoCallImpl(channel, req, resp, resp_handle_out);
    return (res != NO_ERROR) ? res : resp->result;
}

template <typename ReqType, typename RespType>
mx_status_t DoNoFailCall(const mx::channel& channel,
                         const ReqType&     req,
                         RespType*          resp,
                         mx::handle*        resp_handle_out = nullptr) {
    return DoCallImpl(channel, req, resp, resp_handle_out);
}

mxtl::unique_ptr<AudioStream> AudioStream::Create(bool input, uint32_t dev_id) {
    AllocChecker ac;
    mxtl::unique_ptr<AudioStream> res(input
                                      ? static_cast<AudioStream*>(new (&ac) AudioInput(dev_id))
                                      : static_cast<AudioStream*>(new (&ac) AudioOutput(dev_id)));
    if (!ac.check())
       return nullptr;

    return res;
}

AudioStream::AudioStream(bool input, uint32_t dev_id)
    : input_(input),
      dev_id_(dev_id) {
    snprintf(name_, sizeof(name_), "/dev/class/audio2-%s/%03u",
             input_ ? "input" : "output",
             dev_id_);
}

mx_status_t AudioStream::Open() {
    if (stream_ch_ != MX_HANDLE_INVALID)
        return ERR_BAD_STATE;

    int fd = ::open(name(), O_RDONLY);
    if (fd < 0) {
        printf("Failed to open \"%s\" (res %d)\n", name(), fd);
        return fd;
    }

    ssize_t res = ::mxio_ioctl(fd, AUDIO2_IOCTL_GET_CHANNEL,
                               nullptr, 0,
                               &stream_ch_, sizeof(stream_ch_));
    ::close(fd);

    if (res != sizeof(stream_ch_)) {
        printf("Failed to obtain channel (res %zd)\n", res);
        return static_cast<mx_status_t>(res);
    }

    return NO_ERROR;
}

mx_status_t AudioStream::DumpInfo() {
    mx_status_t res;
    printf("Info for audio %s stream #%03u (%s)\n",
            input_ ? "input" : "output", dev_id_, name_);

    {   // Current gain settings and caps
        audio2_stream_cmd_get_gain_req  req;
        audio2_stream_cmd_get_gain_resp resp;

        req.hdr.cmd = AUDIO2_STREAM_CMD_GET_GAIN;
        req.hdr.transaction_id = 1;

        res = DoNoFailCall(stream_ch_, req, &resp);
        if (res != NO_ERROR) {
            printf("Failed to fetch gain information! (res %d)\n", res);
            return res;
        }

        printf("  Current Gain : %.2f dB (%smuted)\n", resp.cur_gain, resp.cur_mute ? "" : "un");
        printf("  Gain Caps    : ");
        if ((resp.min_gain == resp.max_gain) && (resp.min_gain == 0.0f)) {
            printf("fixed 0 dB gain");
        } else
        if (resp.gain_step == 0.0f) {
            printf("gain range [%.2f, %.2f] dB (continuous)", resp.min_gain, resp.max_gain);
        } else {
            printf("gain range [%.2f, %.2f] in %.2f dB steps",
                    resp.min_gain, resp.max_gain, resp.gain_step);
        }
        printf("; %s mute\n", resp.can_mute ? "can" : "cannot");
    }

    {   // Current gain settings and caps
        audio2_stream_cmd_plug_detect_resp resp;
        res = GetPlugState(&resp);
        if (res != NO_ERROR)
            return res;

        printf("  Plug State   : %splugged\n", resp.flags & AUDIO2_PDNF_PLUGGED ? "" : "un");
        printf("  PD Caps      : %s\n", (resp.flags & AUDIO2_PDNF_HARDWIRED)
                                        ? "hardwired"
                                        : ((resp.flags & AUDIO2_PDNF_CAN_NOTIFY)
                                            ? "dynamic (async)"
                                            : "dynamic (synchronous)"));
    }

    // TODO(johngro) : Add other info (supported formats, plug detect, etc...)
    // as we add commands to the protocol.

    return NO_ERROR;
}

mx_status_t AudioStream::GetPlugState(audio2_stream_cmd_plug_detect_resp_t* out_state,
                                      bool enable_notify) {
    MX_DEBUG_ASSERT(out_state != nullptr);
    audio2_stream_cmd_plug_detect_req req;

    req.hdr.cmd = AUDIO2_STREAM_CMD_PLUG_DETECT;
    req.hdr.transaction_id = 1;
    req.flags = enable_notify ? AUDIO2_PDF_ENABLE_NOTIFICATIONS : AUDIO2_PDF_NONE;

    mx_status_t res = DoNoFailCall(stream_ch_, req, out_state);
    if (res != NO_ERROR)
        printf("Failed to fetch plug detect information! (res %d)\n", res);

    return res;
}

void AudioStream::DisablePlugNotifications() {
    audio2_stream_cmd_plug_detect_req req;

    req.hdr.cmd = static_cast<audio2_cmd_t>(AUDIO2_STREAM_CMD_PLUG_DETECT | AUDIO2_FLAG_NO_ACK);
    req.hdr.transaction_id = 1;
    req.flags = AUDIO2_PDF_DISABLE_NOTIFICATIONS;

    stream_ch_.write(0, &req, sizeof(req), nullptr, 0);
}

mx_status_t AudioStream::SetMute(bool mute) {
    audio2_stream_cmd_set_gain_req  req;
    audio2_stream_cmd_set_gain_resp resp;

    req.hdr.cmd = AUDIO2_STREAM_CMD_SET_GAIN;
    req.hdr.transaction_id = 1;
    req.flags = mute
              ? static_cast<audio2_set_gain_flags_t>(AUDIO2_SGF_MUTE_VALID | AUDIO2_SGF_MUTE)
              : AUDIO2_SGF_MUTE_VALID;

    mx_status_t res = DoCall(stream_ch_, req, &resp);
    if (res != NO_ERROR)
        printf("Failed to %smute stream! (res %d)\n", mute ? "" : "un", res);
    else
        printf("Stream is now %smuted\n", mute ? "" : "un");

    return res;
}

mx_status_t AudioStream::SetGain(float gain) {
    audio2_stream_cmd_set_gain_req  req;
    audio2_stream_cmd_set_gain_resp resp;

    req.hdr.cmd = AUDIO2_STREAM_CMD_SET_GAIN;
    req.hdr.transaction_id = 1;
    req.flags = AUDIO2_SGF_GAIN_VALID;
    req.gain  = gain;

    mx_status_t res = DoCall(stream_ch_, req, &resp);
    if (res != NO_ERROR) {
        printf("Failed to set gain to %.2f dB! (res %d)\n", gain, res);
    } else {
        printf("Gain is now %.2f dB.  Stream is %smuted.\n",
                resp.cur_gain, resp.cur_mute ? "" : "un");
    }

    return res;
}

mx_status_t AudioStream::PlugMonitor(float duration) {
    mx_time_t deadline = mx_deadline_after(MX_SEC(static_cast<double>(duration)));
    audio2_stream_cmd_plug_detect_resp resp;
    mx_status_t res = GetPlugState(&resp, true);
    if (res != NO_ERROR)
        return res;

    mx_time_t last_plug_time = resp.plug_state_time;
    bool last_plug_state = (resp.flags & AUDIO2_PDNF_PLUGGED);
    printf("Initial plug state is : %s.\n", last_plug_state ? "plugged" : "unplugged");

    if (resp.flags & AUDIO2_PDNF_HARDWIRED) {
        printf("Stream reports that it is hardwired, Monitoring is not possible.\n");
        return NO_ERROR;

    }

    auto ReportPlugState = [&last_plug_time, &last_plug_state](bool plug_state,
                                                               mx_time_t plug_time) {
        printf("Plug State now : %s (%.3lf sec since last change).\n",
               plug_state ? "plugged" : "unplugged",
               static_cast<double>(plug_time - last_plug_time) / 1000000000.0);

        last_plug_state = plug_state;
        last_plug_time  = plug_time;
    };

    if (resp.flags & AUDIO2_PDNF_CAN_NOTIFY) {
        printf("Stream is capable of async notification.  Monitoring for %.2f seconds\n",
                duration);

        auto cleanup = mxtl::MakeAutoCall([this]() { DisablePlugNotifications(); });
        while (true) {
            mx_signals_t pending;
            res = stream_ch_.wait_one(MX_CHANNEL_PEER_CLOSED | MX_CHANNEL_READABLE,
                                      deadline, &pending);

            if ((res != NO_ERROR) || (pending & MX_CHANNEL_PEER_CLOSED)) {
                if (res != ERR_TIMED_OUT)
                    printf("Error while waiting for plug notification (res %d)\n", res);

                if (pending & MX_CHANNEL_PEER_CLOSED)
                    printf("Peer closed while waiting for plug notification\n");

                break;
            }

            MX_DEBUG_ASSERT(pending & MX_CHANNEL_READABLE);

            audio2_stream_plug_detect_notify_t state;
            uint32_t bytes_read;
            res = stream_ch_.read(0, &state, sizeof(state), &bytes_read, nullptr, 0, nullptr);
            if (res != NO_ERROR) {
                printf("Read failure while waiting for plug notification (res %d)\n", res);
                break;
            }

            if ((bytes_read != sizeof(state)) ||
                (state.hdr.cmd != AUDIO2_STREAM_PLUG_DETECT_NOTIFY)) {
                printf("Size/type mismatch while waiting for plug notification.  "
                       "Got (%u/%u) Expected (%zu/%u)\n",
                       bytes_read, state.hdr.cmd,
                       sizeof(state), AUDIO2_STREAM_PLUG_DETECT_NOTIFY);
                break;
            }

            bool plug_state = (state.flags & AUDIO2_PDNF_PLUGGED);
            ReportPlugState(plug_state, state.plug_state_time);
        }
    } else {
        printf("Stream is not capable of async notification.  Polling for %.2f seconds\n",
                duration);

        while (true) {
            mx_time_t now = mx_time_get(MX_CLOCK_MONOTONIC);
            if (now >= deadline)
                break;

            mx_time_t next_wake = mxtl::min(deadline, now + MX_MSEC(100u));
            mx_nanosleep(next_wake);

            mx_status_t res = GetPlugState(&resp, true);
            if (res != NO_ERROR) {
                printf("Failed to poll plug state (res %d)\n", res);
                break;
            }

            bool plug_state = (resp.flags & AUDIO2_PDNF_PLUGGED);
            if (plug_state != last_plug_state)
                ReportPlugState(resp.flags, resp.plug_state_time);
        }
    }

    printf("Monitoring finished.\n");

    return NO_ERROR;
}

mx_status_t AudioStream::SetFormat(uint32_t frames_per_second,
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

mx_status_t AudioStream::GetBuffer(uint32_t frames, uint32_t irqs_per_ring) {
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

mx_status_t AudioStream::StartRingBuffer() {
    if (rb_ch_ == MX_HANDLE_INVALID)
        return ERR_BAD_STATE;

    audio2_rb_cmd_start_req_t  req;
    audio2_rb_cmd_start_resp_t resp;

    req.hdr.cmd = AUDIO2_RB_CMD_START;
    req.hdr.transaction_id = 1;

    return DoCall(rb_ch_, req, &resp);
}

mx_status_t AudioStream::StopRingBuffer() {
    if (rb_ch_ == MX_HANDLE_INVALID)
        return ERR_BAD_STATE;

    audio2_rb_cmd_stop_req_t  req;
    audio2_rb_cmd_stop_resp_t resp;

    req.hdr.cmd = AUDIO2_RB_CMD_STOP;
    req.hdr.transaction_id = 1;

    return DoCall(rb_ch_, req, &resp);
}
