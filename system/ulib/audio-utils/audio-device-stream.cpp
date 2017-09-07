// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <audio-utils/audio-device-stream.h>
#include <audio-utils/audio-input.h>
#include <audio-utils/audio-output.h>
#include <fcntl.h>
#include <inttypes.h>
#include <magenta/assert.h>
#include <magenta/device/audio.h>
#include <magenta/process.h>
#include <magenta/syscalls.h>
#include <mx/channel.h>
#include <mx/handle.h>
#include <mx/vmo.h>
#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <fbl/limits.h>
#include <mxio/io.h>
#include <stdio.h>
#include <string.h>

namespace audio {
namespace utils {

static constexpr mx_duration_t CALL_TIMEOUT = MX_MSEC(500);
template <typename ReqType, typename RespType>
mx_status_t DoCallImpl(const mx::channel& channel,
                       const ReqType&     req,
                       RespType*          resp,
                       mx::handle*        resp_handle_out,
                       uint32_t*          resp_len_out = nullptr) {
    mx_channel_call_args_t args;

    MX_DEBUG_ASSERT((resp_handle_out == nullptr) || !resp_handle_out->is_valid());

    args.wr_bytes       = const_cast<ReqType*>(&req);
    args.wr_num_bytes   = sizeof(ReqType);
    args.wr_handles     = nullptr;
    args.wr_num_handles = 0;
    args.rd_bytes       = resp;
    args.rd_num_bytes   = sizeof(RespType);
    args.rd_handles     = resp_handle_out ? resp_handle_out->reset_and_get_address() : nullptr;
    args.rd_num_handles = resp_handle_out ? 1 : 0;

    uint32_t bytes, handles;
    mx_status_t read_status, write_status;

    write_status = channel.call(0, mx_deadline_after(CALL_TIMEOUT), &args, &bytes, &handles,
                                &read_status);

    if (write_status != MX_OK) {
        if (write_status == MX_ERR_CALL_FAILED) {
            printf("Cmd read failure (cmd %04x, res %d)\n", req.hdr.cmd, read_status);
            return read_status;
        } else {
            printf("Cmd write failure (cmd %04x, res %d)\n", req.hdr.cmd, write_status);
            return write_status;
        }
    }

    // If the caller wants to know the size of the response length, let them
    // check to make sure it is consistent with what they expect.  Otherwise,
    // make sure that the number of bytes we got back matches the size of the
    // response structure.
    if (resp_len_out != nullptr) {
        *resp_len_out = bytes;
    } else
    if (bytes != sizeof(RespType)) {
        printf("Unexpected response size (got %u, expected %zu)\n", bytes, sizeof(RespType));
        return MX_ERR_INTERNAL;
    }

    return MX_OK;
}

template <typename ReqType, typename RespType>
mx_status_t DoCall(const mx::channel& channel,
                   const ReqType&     req,
                   RespType*          resp,
                   mx::handle*        resp_handle_out = nullptr) {
    mx_status_t res = DoCallImpl(channel, req, resp, resp_handle_out);
    return (res != MX_OK) ? res : resp->result;
}

template <typename ReqType, typename RespType>
mx_status_t DoNoFailCall(const mx::channel& channel,
                         const ReqType&     req,
                         RespType*          resp,
                         mx::handle*        resp_handle_out = nullptr) {
    return DoCallImpl(channel, req, resp, resp_handle_out);
}

AudioDeviceStream::AudioDeviceStream(bool input, uint32_t dev_id)
    : input_(input) {
    snprintf(name_, sizeof(name_), "/dev/class/audio-%s/%03u",
             input_ ? "input" : "output",
             dev_id);
}

AudioDeviceStream::AudioDeviceStream(bool input, const char* dev_path)
    : input_(input) {
    strncpy(name_, dev_path, sizeof(name_));
    name_[sizeof(name_) - 1] = 0;
}

mx_status_t AudioDeviceStream::Open() {
    if (stream_ch_ != MX_HANDLE_INVALID)
        return MX_ERR_BAD_STATE;

    int fd = ::open(name(), O_RDONLY);
    if (fd < 0) {
        printf("Failed to open \"%s\" (res %d)\n", name(), fd);
        return fd;
    }

    ssize_t res = ::mxio_ioctl(fd, AUDIO_IOCTL_GET_CHANNEL,
                               nullptr, 0,
                               &stream_ch_, sizeof(stream_ch_));
    ::close(fd);

    if (res != sizeof(stream_ch_)) {
        printf("Failed to obtain channel (res %zd)\n", res);
        return static_cast<mx_status_t>(res);
    }

    return MX_OK;
}

mx_status_t AudioDeviceStream::GetSupportedFormats(
        fbl::Vector<audio_stream_format_range_t>* out_formats) const {
    constexpr uint32_t MIN_RESP_SIZE = offsetof(audio_stream_cmd_get_formats_resp_t, format_ranges);
    audio_stream_cmd_get_formats_req req;
    audio_stream_cmd_get_formats_resp resp;
    uint32_t rxed;
    mx_status_t res;

    if (out_formats == nullptr) {
        return MX_ERR_INVALID_ARGS;
    }

    req.hdr.cmd = AUDIO_STREAM_CMD_GET_FORMATS;
    req.hdr.transaction_id = 1;
    res = DoCallImpl(stream_ch_, req, &resp, nullptr, &rxed);
    if ((res != MX_OK) || (rxed < MIN_RESP_SIZE)) {
        printf("Failed to fetch initial suppored format list chunk (res %d, rxed %u)\n",
                res, rxed);
        return res;
    }

    uint32_t expected_formats = resp.format_range_count;
    if (!expected_formats)
        return MX_OK;

    out_formats->reset();
    fbl::AllocChecker ac;
    out_formats->reserve(expected_formats, &ac);
    if (!ac.check()) {
        printf("Failed to allocated %u entries for format ranges\n", expected_formats);
        return MX_ERR_NO_MEMORY;
    }

    mx_txid_t txid = resp.hdr.transaction_id;
    uint32_t processed_formats = 0;
    while (true) {
        if (resp.hdr.cmd != AUDIO_STREAM_CMD_GET_FORMATS) {
            printf("Unexpected response command while fetching formats "
                   "(expected 0x%08x, got 0x%08x)\n",
                    AUDIO_STREAM_CMD_GET_FORMATS, resp.hdr.cmd);
            return MX_ERR_INTERNAL;
        }

        if (resp.hdr.transaction_id != txid) {
            printf("Unexpected response transaction id while fetching formats "
                   "(expected 0x%08x, got 0x%08x)\n",
                    txid, resp.hdr.transaction_id);
            return MX_ERR_INTERNAL;
        }

        if (resp.first_format_range_ndx != processed_formats) {
            printf("Bad format index while fetching formats (expected %u, got %hu)\n",
                    processed_formats, resp.first_format_range_ndx);
            return MX_ERR_INTERNAL;
        }

        uint32_t todo = fbl::min(static_cast<uint32_t>(expected_formats - processed_formats),
                                  AUDIO_STREAM_CMD_GET_FORMATS_MAX_RANGES_PER_RESPONSE);
        size_t min_size = MIN_RESP_SIZE + (todo * sizeof(audio_stream_format_range_t));
        if (rxed < min_size) {
            printf("Short response while fetching formats (%u < %zu)\n", rxed, min_size);
            return MX_ERR_INTERNAL;
        }

        for (uint16_t i = 0; i < todo; ++i) {
            out_formats->push_back(resp.format_ranges[i]);
        }

        processed_formats += todo;
        if (processed_formats == expected_formats)
            break;

        mx_signals_t pending_sig;
        res = stream_ch_.wait_one(MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED,
                                  mx_deadline_after(CALL_TIMEOUT),
                                  &pending_sig);
        if (res != MX_OK) {
            printf("Failed to wait for next response after processing %u/%u formats (res %d)\n",
                    processed_formats, expected_formats, res);
            return res;
        }

        res = stream_ch_.read(0u, &resp, sizeof(resp), &rxed, nullptr, 0, nullptr);
        if (res != MX_OK) {
            printf("Failed to read next response after processing %u/%u formats (res %d)\n",
                    processed_formats, expected_formats, res);
            return res;
        }
    }

    return MX_OK;
}

mx_status_t AudioDeviceStream::GetPlugState(audio_stream_cmd_plug_detect_resp_t* out_state,
                                            bool enable_notify) const {
    MX_DEBUG_ASSERT(out_state != nullptr);
    audio_stream_cmd_plug_detect_req req;

    req.hdr.cmd = AUDIO_STREAM_CMD_PLUG_DETECT;
    req.hdr.transaction_id = 1;
    req.flags = enable_notify ? AUDIO_PDF_ENABLE_NOTIFICATIONS : AUDIO_PDF_NONE;

    mx_status_t res = DoNoFailCall(stream_ch_, req, out_state);
    if (res != MX_OK)
        printf("Failed to fetch plug detect information! (res %d)\n", res);

    return res;
}

void AudioDeviceStream::DisablePlugNotifications() {
    audio_stream_cmd_plug_detect_req req;

    req.hdr.cmd = static_cast<audio_cmd_t>(AUDIO_STREAM_CMD_PLUG_DETECT | AUDIO_FLAG_NO_ACK);
    req.hdr.transaction_id = 1;
    req.flags = AUDIO_PDF_DISABLE_NOTIFICATIONS;

    stream_ch_.write(0, &req, sizeof(req), nullptr, 0);
}

mx_status_t AudioDeviceStream::SetMute(bool mute) {
    audio_stream_cmd_set_gain_req  req;
    audio_stream_cmd_set_gain_resp resp;

    req.hdr.cmd = AUDIO_STREAM_CMD_SET_GAIN;
    req.hdr.transaction_id = 1;
    req.flags = mute
              ? static_cast<audio_set_gain_flags_t>(AUDIO_SGF_MUTE_VALID | AUDIO_SGF_MUTE)
              : AUDIO_SGF_MUTE_VALID;

    mx_status_t res = DoCall(stream_ch_, req, &resp);
    if (res != MX_OK)
        printf("Failed to %smute stream! (res %d)\n", mute ? "" : "un", res);
    else
        printf("Stream is now %smuted\n", mute ? "" : "un");

    return res;
}

mx_status_t AudioDeviceStream::SetGain(float gain) {
    audio_stream_cmd_set_gain_req  req;
    audio_stream_cmd_set_gain_resp resp;

    req.hdr.cmd = AUDIO_STREAM_CMD_SET_GAIN;
    req.hdr.transaction_id = 1;
    req.flags = AUDIO_SGF_GAIN_VALID;
    req.gain  = gain;

    mx_status_t res = DoCall(stream_ch_, req, &resp);
    if (res != MX_OK) {
        printf("Failed to set gain to %.2f dB! (res %d)\n", gain, res);
    } else {
        printf("Gain is now %.2f dB.  Stream is %smuted.\n",
                resp.cur_gain, resp.cur_mute ? "" : "un");
    }

    return res;
}

mx_status_t AudioDeviceStream::GetGain(audio_stream_cmd_get_gain_resp_t* out_gain) const {
    if (out_gain == nullptr)
        return MX_ERR_INVALID_ARGS;

    audio_stream_cmd_get_gain_req req;
    req.hdr.cmd = AUDIO_STREAM_CMD_GET_GAIN;
    req.hdr.transaction_id = 1;

    return DoNoFailCall(stream_ch_, req, out_gain);
}

mx_status_t AudioDeviceStream::PlugMonitor(float duration) {
    mx_time_t deadline = mx_deadline_after(MX_SEC(static_cast<double>(duration)));
    audio_stream_cmd_plug_detect_resp resp;
    mx_status_t res = GetPlugState(&resp, true);
    if (res != MX_OK)
        return res;

    mx_time_t last_plug_time = resp.plug_state_time;
    bool last_plug_state = (resp.flags & AUDIO_PDNF_PLUGGED);
    printf("Initial plug state is : %s.\n", last_plug_state ? "plugged" : "unplugged");

    if (resp.flags & AUDIO_PDNF_HARDWIRED) {
        printf("Stream reports that it is hardwired, Monitoring is not possible.\n");
        return MX_OK;

    }

    auto ReportPlugState = [&last_plug_time, &last_plug_state](bool plug_state,
                                                               mx_time_t plug_time) {
        printf("Plug State now : %s (%.3lf sec since last change).\n",
               plug_state ? "plugged" : "unplugged",
               static_cast<double>(plug_time - last_plug_time) / MX_SEC(1));

        last_plug_state = plug_state;
        last_plug_time  = plug_time;
    };

    if (resp.flags & AUDIO_PDNF_CAN_NOTIFY) {
        printf("Stream is capable of async notification.  Monitoring for %.2f seconds\n",
                duration);

        auto cleanup = fbl::MakeAutoCall([this]() { DisablePlugNotifications(); });
        while (true) {
            mx_signals_t pending;
            res = stream_ch_.wait_one(MX_CHANNEL_PEER_CLOSED | MX_CHANNEL_READABLE,
                                      deadline, &pending);

            if ((res != MX_OK) || (pending & MX_CHANNEL_PEER_CLOSED)) {
                if (res != MX_ERR_TIMED_OUT)
                    printf("Error while waiting for plug notification (res %d)\n", res);

                if (pending & MX_CHANNEL_PEER_CLOSED)
                    printf("Peer closed while waiting for plug notification\n");

                break;
            }

            MX_DEBUG_ASSERT(pending & MX_CHANNEL_READABLE);

            audio_stream_plug_detect_notify_t state;
            uint32_t bytes_read;
            res = stream_ch_.read(0, &state, sizeof(state), &bytes_read, nullptr, 0, nullptr);
            if (res != MX_OK) {
                printf("Read failure while waiting for plug notification (res %d)\n", res);
                break;
            }

            if ((bytes_read != sizeof(state)) ||
                (state.hdr.cmd != AUDIO_STREAM_PLUG_DETECT_NOTIFY)) {
                printf("Size/type mismatch while waiting for plug notification.  "
                       "Got (%u/%u) Expected (%zu/%u)\n",
                       bytes_read, state.hdr.cmd,
                       sizeof(state), AUDIO_STREAM_PLUG_DETECT_NOTIFY);
                break;
            }

            bool plug_state = (state.flags & AUDIO_PDNF_PLUGGED);
            ReportPlugState(plug_state, state.plug_state_time);
        }
    } else {
        printf("Stream is not capable of async notification.  Polling for %.2f seconds\n",
                duration);

        while (true) {
            mx_time_t now = mx_time_get(MX_CLOCK_MONOTONIC);
            if (now >= deadline)
                break;

            mx_time_t next_wake = fbl::min(deadline, now + MX_MSEC(100u));

            mx_signals_t sigs;
            mx_status_t res = stream_ch_.wait_one(MX_CHANNEL_PEER_CLOSED, next_wake, &sigs);

            if ((res != MX_OK) && (res != MX_ERR_TIMED_OUT)) {
                printf("Error waiting on stream channel (res %d)\n", res);
                break;
            }

            if (sigs & MX_CHANNEL_PEER_CLOSED) {
                printf("Peer closed connection while polling plug state\n");
                break;
            }

            res = GetPlugState(&resp, true);
            if (res != MX_OK) {
                printf("Failed to poll plug state (res %d)\n", res);
                break;
            }

            bool plug_state = (resp.flags & AUDIO_PDNF_PLUGGED);
            if (plug_state != last_plug_state)
                ReportPlugState(resp.flags, resp.plug_state_time);
        }
    }

    printf("Monitoring finished.\n");

    return MX_OK;
}

mx_status_t AudioDeviceStream::SetFormat(uint32_t frames_per_second,
                                         uint16_t channels,
                                         audio_sample_format_t sample_format) {
    if ((stream_ch_ == MX_HANDLE_INVALID) || (rb_ch_ != MX_HANDLE_INVALID))
        return MX_ERR_BAD_STATE;

    auto noflag_format = static_cast<audio_sample_format_t>(
            (sample_format & ~AUDIO_SAMPLE_FORMAT_FLAG_MASK));

    switch (noflag_format) {
    case AUDIO_SAMPLE_FORMAT_8BIT:         sample_size_ = 1; break;
    case AUDIO_SAMPLE_FORMAT_16BIT:        sample_size_ = 2; break;
    case AUDIO_SAMPLE_FORMAT_24BIT_PACKED: sample_size_ = 3; break;
    case AUDIO_SAMPLE_FORMAT_20BIT_IN32:
    case AUDIO_SAMPLE_FORMAT_24BIT_IN32:
    case AUDIO_SAMPLE_FORMAT_32BIT:
    case AUDIO_SAMPLE_FORMAT_32BIT_FLOAT:  sample_size_ = 4; break;
    default: return MX_ERR_NOT_SUPPORTED;
    }

    channel_cnt_   = channels;
    frame_sz_      = channels * sample_size_;
    frame_rate_    = frames_per_second;
    sample_format_ = sample_format;

    audio_stream_cmd_set_format_req_t  req;
    audio_stream_cmd_set_format_resp_t resp;
    req.hdr.cmd            = AUDIO_STREAM_CMD_SET_FORMAT;
    req.hdr.transaction_id = 1;
    req.frames_per_second  = frames_per_second;
    req.channels           = channels;
    req.sample_format      = sample_format;

    mx::handle tmp;
    mx_status_t res = DoCall(stream_ch_, req, &resp, &tmp);
    if (res != MX_OK) {
        printf("Failed to set format %uHz %hu-Ch fmt 0x%x (res %d)\n",
                frames_per_second, channels, sample_format, res);
    }

    // TODO(johngro) : Verify the type of this handle before transferring it to
    // our ring buffer channel handle.
    rb_ch_.reset(tmp.release());

    return res;
}

mx_status_t AudioDeviceStream::GetBuffer(uint32_t frames, uint32_t irqs_per_ring) {
    mx_status_t res;

    if(!frames)
        return MX_ERR_INVALID_ARGS;

    if (!rb_ch_.is_valid() || rb_vmo_.is_valid() || !frame_sz_)
        return MX_ERR_BAD_STATE;

    // Stash the FIFO depth, in case users need to know it.
    {
        audio_rb_cmd_get_fifo_depth_req_t  req;
        audio_rb_cmd_get_fifo_depth_resp_t resp;

        req.hdr.cmd = AUDIO_RB_CMD_GET_FIFO_DEPTH;
        req.hdr.transaction_id = 1;
        res = DoCall(rb_ch_, req, &resp);
        if (res != MX_OK) {
            printf("Failed to fetch fifo depth (res %d)\n", res);
            return res;
        }

        fifo_depth_ = resp.fifo_depth;
    }

    {
        // Get a VMO representing the ring buffer we will share with the audio driver.
        audio_rb_cmd_get_buffer_req_t  req;
        audio_rb_cmd_get_buffer_resp_t resp;

        req.hdr.cmd                = AUDIO_RB_CMD_GET_BUFFER;
        req.hdr.transaction_id     = 1;
        req.min_ring_buffer_frames = frames;
        req.notifications_per_ring = irqs_per_ring;

        mx::handle tmp;
        res = DoCall(rb_ch_, req, &resp, &tmp);

        if ((res == MX_OK) && (resp.result != MX_OK))
            res = resp.result;

        if (res != MX_OK) {
            printf("Failed to get driver ring buffer VMO (res %d)\n", res);
            return res;
        }

        // TODO(johngro) : Verify the type of this handle before transferring it to our VMO handle.
        rb_vmo_.reset(tmp.release());
    }

    // We have the buffer, fetch the size the driver finally decided on.
    uint64_t rb_sz;
    res = rb_vmo_.get_size(&rb_sz);
    if (res != MX_OK) {
        printf("Failed to fetch ring buffer VMO size (res %d)\n", res);
        return res;
    }

    // Sanity check the size and stash it if it checks out.
    if ((rb_sz > fbl::numeric_limits<decltype(rb_sz_)>::max()) || ((rb_sz % frame_sz_) != 0)) {
        printf("Bad VMO size returned by audio driver! (size = %" PRIu64 " frame_sz = %u)\n",
                rb_sz, frame_sz_);
        return MX_ERR_INVALID_ARGS;
    }

    rb_sz_ = static_cast<decltype(rb_sz_)>(rb_sz);

    // Map the VMO into our address space
    // TODO(johngro) : How do I specify the cache policy for this mapping?
    uint32_t flags = input_
                   ? MX_VM_FLAG_PERM_READ
                   : MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE;
    res = mx_vmar_map(mx_vmar_root_self(), 0u,
                      rb_vmo_.get(), 0u, rb_sz_,
                      flags, reinterpret_cast<uintptr_t*>(&rb_virt_));
    if (res != MX_OK) {
        printf("Failed to map ring buffer VMO (res %d)\n", res);
        return res;
    }

    // Success!  If this is an output device, zero out the buffer and we are done.
    if (!input_) {
        memset(rb_virt_, 0, rb_sz_);
    }

    return MX_OK;
}

mx_status_t AudioDeviceStream::StartRingBuffer() {
    if (rb_ch_ == MX_HANDLE_INVALID)
        return MX_ERR_BAD_STATE;

    audio_rb_cmd_start_req_t  req;
    audio_rb_cmd_start_resp_t resp;

    req.hdr.cmd = AUDIO_RB_CMD_START;
    req.hdr.transaction_id = 1;

    mx_status_t res = DoCall(rb_ch_, req, &resp);

    if (res == MX_OK) {
        start_ticks_ = resp.start_ticks;
    }

    return res;
}

mx_status_t AudioDeviceStream::StopRingBuffer() {
    if (rb_ch_ == MX_HANDLE_INVALID)
        return MX_ERR_BAD_STATE;

    start_ticks_ = 0;

    audio_rb_cmd_stop_req_t  req;
    audio_rb_cmd_stop_resp_t resp;

    req.hdr.cmd = AUDIO_RB_CMD_STOP;
    req.hdr.transaction_id = 1;

    return DoCall(rb_ch_, req, &resp);
}

void AudioDeviceStream::ResetRingBuffer() {
    rb_ch_.reset();
    rb_vmo_.reset();
    rb_sz_ = 0;
    rb_virt_ = nullptr;
}


bool AudioDeviceStream::IsChannelConnected(const mx::channel& ch) {
    if (!ch.is_valid())
        return false;

    mx_signals_t junk;
    return ch.wait_one(MX_CHANNEL_PEER_CLOSED, 0u, &junk) != MX_ERR_TIMED_OUT;
}


}  // namespace utils
}  // namespace audio
