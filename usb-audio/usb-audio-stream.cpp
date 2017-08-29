// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <audio-proto-utils/format-utils.h>
#include <ddk/device.h>
#include <magenta/device/audio.h>
#include <magenta/device/usb.h>
#include <magenta/hw/usb-audio.h>
#include <magenta/process.h>
#include <magenta/types.h>
#include <mx/vmar.h>
#include <mxtl/algorithm.h>
#include <mxtl/limits.h>
#include <string.h>

#include "debug-logging.h"
#include "usb-audio.h"
#include "usb-audio-stream.h"

namespace audio {
namespace usb {

static constexpr uintptr_t PRIVILEGED_CONNECTION_CTX = 0x1;
static constexpr uint32_t  MAX_OUTSTANDING_IOTXN = 8;

static constexpr uint32_t ExtractSampleRate(const usb_audio_ac_samp_freq& sr) {
    return static_cast<uint32_t>(sr.freq[0])
        | (static_cast<uint32_t>(sr.freq[1]) << 8)
        | (static_cast<uint32_t>(sr.freq[2]) << 16);
}

UsbAudioStream::~UsbAudioStream() {
    // We destructing.  All of our iotxns should be sitting in the free list.
    MX_DEBUG_ASSERT(allocated_iotxn_cnt_ == free_iotxn_cnt_);

    while (!list_is_empty(&free_iotxn_)) {
        iotxn_release(list_remove_head_type(&free_iotxn_, iotxn_t, node));
    }
}

// static
mx_status_t UsbAudioStream::Create(bool is_input,
                                   mx_device_t* parent,
                                   usb_protocol_t* usb,
                                   int index,
                                   usb_interface_descriptor_t* usb_interface,
                                   usb_endpoint_descriptor_t* usb_endpoint,
                                   usb_audio_ac_format_type_i_desc* format_desc) {
    auto stream = mxtl::AdoptRef(new UsbAudioStream(parent, usb, is_input, index));
    char name[64];
    snprintf(name, sizeof(name), "usb-audio-%s-%03d", is_input ? "input" : "output", index);

    mx_status_t res = stream->Bind(name, usb_interface, usb_endpoint, format_desc);
    if (res == MX_OK) {
        // If bind/setup has succeeded, then the devmgr now controls our
        // lifecycle and will release us when finished with us.  Let go of our
        // local unique_ptr.
        //
        // TODO(johngro) : outright leaking this reference feels wrong.  We
        // should bind this to the devmgr cookie somehow instead.
        static_cast<void>(stream.leak_ref());
    }

    return MX_OK;
}

void UsbAudioStream::PrintDebugPrefix() const {
    printf("usb-audio-%s-%03d: ", is_input() ? "input" : "output", usb_index_);
}

mx_status_t UsbAudioStream::Bind(const char* devname,
                                 usb_interface_descriptor_t* usb_interface,
                                 usb_endpoint_descriptor_t* usb_endpoint,
                                 usb_audio_ac_format_type_i_desc* format_desc) {
    // TODO(johngro) : parse all of the supported formats and widgets present in
    // this audio device.  Support things like aynch plug notification, format
    // selection, gain control, sidetone, etc...
    if (!usb_interface || !usb_endpoint || !format_desc)
        return MX_ERR_INVALID_ARGS;

    MX_DEBUG_ASSERT(!supported_formats_.size());
    mx_status_t res = AddFormats(*format_desc, &supported_formats_);
    if (res != MX_OK) {
        LOG("Failed to parse format descriptor (res %d)\n", res);
        return res;
    }

    // TODO(johngro): Do this differently when we have the ability to queue io
    // transactions to a USB isochronous endpoint and can have the bus driver
    // DMA directly from the ring buffer we have set up with our user.
    {
        mxtl::AutoLock txn_lock(&txn_lock_);

        list_initialize(&free_iotxn_);
        free_iotxn_cnt_ = 0;
        allocated_iotxn_cnt_ = 0;
        max_iotxn_size_ = usb_ep_max_packet(usb_endpoint);

        for (uint32_t i = 0; i < MAX_OUTSTANDING_IOTXN; ++i) {
            iotxn_t* txn = usb_alloc_iotxn(usb_endpoint->bEndpointAddress, max_iotxn_size_);

            if (!txn) {
                LOG("Failed to allocate iotxn %u/%u (size %u)\n",
                    i + 1, MAX_OUTSTANDING_IOTXN, max_iotxn_size_);
                return MX_ERR_NO_MEMORY;
            }

            txn->cookie = this;
            txn->complete_cb = [](iotxn_t* txn, void* cookie) -> void {
                MX_DEBUG_ASSERT(cookie != nullptr);
                reinterpret_cast<UsbAudioStream*>(cookie)->IotxnComplete(txn);
            };

            list_add_head(&free_iotxn_, &txn->node);
            ++free_iotxn_cnt_;
            ++allocated_iotxn_cnt_;
        }
    }

    iface_num_   = usb_interface->bInterfaceNumber;
    alt_setting_ = usb_interface->bAlternateSetting;
    usb_ep_addr_ = usb_endpoint->bEndpointAddress;

    return UsbAudioStreamBase::DdkAdd(devname);
}

void UsbAudioStream::ReleaseRingBufferLocked() {
    if (ring_buffer_virt_ != nullptr) {
        MX_DEBUG_ASSERT(ring_buffer_size_ != 0);
        mx::vmar::root_self().unmap(reinterpret_cast<uintptr_t>(ring_buffer_virt_),
                                    ring_buffer_size_);
        ring_buffer_virt_ = nullptr;
        ring_buffer_size_ = 0;
    }
    ring_buffer_vmo_.reset();
}

mx_status_t UsbAudioStream::AddFormats(
        const usb_audio_ac_format_type_i_desc& format_desc,
        mxtl::Vector<audio_stream_format_range_t>* supported_formats) {
    if (!supported_formats)
        return MX_ERR_INVALID_ARGS;

    // Record the min/max number of channels.
    audio_stream_format_range_t range;
    range.min_channels = format_desc.bNrChannels;
    range.max_channels = format_desc.bNrChannels;

    // Encode the bit resolution and subframe size from the audio descriptor as
    // an audio device driver audio_sample_format_t
    //
    // TODO(johngro) : figure out how format descriptors are used to indicate
    // 32-bit floating point, uLaw/aLaw compression, or 8 bit unsigned.  In
    // theory, there should be a wFormatTag field somewhere in the structure
    // which indicates this, but their does not appear to be one (currently).
    // If it follows the pattern of a Type II MPEG audio format, it may be that
    // bDescriptorSubtype is supposed to be USB_AUDIO_AS_FORMAT_SPECIFIC which
    // will then be followed by a 2 byte wFormatTag instead of a single byte
    // bFormatType.
    switch (format_desc.bBitResolution) {
    case 8:
    case 16:
    case 32: {
        if (format_desc.bSubFrameSize != (format_desc.bBitResolution >> 3)) {
            LOG("Unsupported format.  Subframe size (%u bytes) does not "
                "match Bit Res (%u bits)\n",
                format_desc.bSubFrameSize,
                format_desc.bBitResolution);
            return MX_ERR_NOT_SUPPORTED;
        }
        switch (format_desc.bBitResolution) {
        case 8:  range.sample_formats = AUDIO_SAMPLE_FORMAT_8BIT; break;
        case 16: range.sample_formats = AUDIO_SAMPLE_FORMAT_16BIT; break;
        case 32: range.sample_formats = AUDIO_SAMPLE_FORMAT_32BIT; break;
        }
    } break;

    case 20:
    case 24: {
        if ((format_desc.bSubFrameSize != 3) && (format_desc.bSubFrameSize != 4)) {
            LOG("Unsupported format.  %u-bit audio must be packed into a 3 "
                "or 4 byte subframe (Subframe size %u)\n",
                format_desc.bBitResolution,
                format_desc.bSubFrameSize);
            return MX_ERR_NOT_SUPPORTED;
        }
        switch (format_desc.bBitResolution) {
        case 20: range.sample_formats = (format_desc.bSubFrameSize == 3)
                                      ? AUDIO_SAMPLE_FORMAT_20BIT_PACKED
                                      : AUDIO_SAMPLE_FORMAT_20BIT_IN32;
        case 24: range.sample_formats = (format_desc.bSubFrameSize == 3)
                                      ? AUDIO_SAMPLE_FORMAT_24BIT_PACKED
                                      : AUDIO_SAMPLE_FORMAT_24BIT_IN32;
        }
    } break;

    default:
        LOG("Unsupported format.  Bad Bit Res (%u bits)\n", format_desc.bBitResolution);
        return MX_ERR_NOT_SUPPORTED;
    }

    // If bSamFreqType is 0, it means that we have a continuous range of
    // sampling frequencies available.  Otherwise, we have a discrete number and
    // bSamFreqType specifies how many.
    //
    // See Universal Serial Bus Device Class Definition for Audio Data Formats
    // Release 1.0 Tables 2-2 and 2-3.
    if (format_desc.bSamFreqType) {
        mxtl::AllocChecker ac;
        supported_formats->reserve(format_desc.bSamFreqType, &ac);
        if (!ac.check()) {
            LOG("Out of memory attempting to reserve %u format ranges\n",
                format_desc.bSamFreqType);
            return MX_ERR_NO_MEMORY;
        }

        // TODO(johngro) : This could be encoded more compactly if wanted to do
        // so by extracting all of the 48k and 44.1k rates into a bitmask, and
        // then putting together ranges which represented continuous runs of
        // frame rates in each of the families.
        for (uint32_t i = 0; i < format_desc.bSamFreqType; ++i) {
            uint32_t rate = ExtractSampleRate(format_desc.tSamFreq[i]);
            range.min_frames_per_second = rate;
            range.max_frames_per_second = rate;

            if (audio::utils::FrameRateIn48kFamily(rate)) {
                range.flags = ASF_RANGE_FLAG_FPS_48000_FAMILY;
            } else
            if (audio::utils::FrameRateIn441kFamily(rate)) {
                range.flags = ASF_RANGE_FLAG_FPS_44100_FAMILY;
            } else {
                range.flags = ASF_RANGE_FLAG_FPS_CONTINUOUS;
            }

            supported_formats->push_back(range);
        }
    } else {
        mxtl::AllocChecker ac;
        supported_formats->reserve(1, &ac);
        if (!ac.check()) {
            LOG("Out of memory attempting to reserve 1 format range\n");
            return MX_ERR_NO_MEMORY;
        }

        range.min_frames_per_second = ExtractSampleRate(format_desc.tSamFreq[0]);
        range.max_frames_per_second = ExtractSampleRate(format_desc.tSamFreq[1]);
        range.flags = ASF_RANGE_FLAG_FPS_CONTINUOUS;

        supported_formats->push_back(range);
    }

    return MX_OK;
}

void UsbAudioStream::DdkUnbind() {
    // Close all of our client channels if we have not already.
    ShutdownDispatcherChannels();

    // Unpublish our device node.
    DdkRemove();
}

void UsbAudioStream::DdkRelease() {
    // Reclaim our reference from the driver framework and let it go out of
    // scope.  If this is our last reference (it should be), we will destruct
    // immediately afterwards.
    auto thiz = mxtl::internal::MakeRefPtrNoAdopt(this);
}

mx_status_t UsbAudioStream::DdkIoctl(uint32_t op,
                                     const void* in_buf, size_t in_len,
                                     void* out_buf, size_t out_len, size_t* out_actual) {
    // The only IOCTL we support is get channel.
    if (op != AUDIO_IOCTL_GET_CHANNEL) {
        return MX_ERR_NOT_SUPPORTED;
    }

    if ((out_buf == nullptr) ||
        (out_actual == nullptr) ||
        (out_len != sizeof(mx_handle_t))) {
        return MX_ERR_INVALID_ARGS;
    }

    mxtl::AutoLock lock(&lock_);

    // Attempt to allocate a new driver channel and bind it to us.  If we don't
    // already have an stream_channel_, flag this channel is the privileged
    // connection (The connection which is allowed to do things like change
    // formats).
    uintptr_t ctx = (stream_channel_ == nullptr) ? PRIVILEGED_CONNECTION_CTX : 0;
    auto channel = DispatcherChannelAllocator::New(ctx);
    if (channel == nullptr)
        return MX_ERR_NO_MEMORY;

    mx::channel client_endpoint;
    mx_status_t res = channel->Activate(mxtl::WrapRefPtr(this), &client_endpoint);
    if (res == MX_OK) {
        if (ctx) {
            MX_DEBUG_ASSERT(stream_channel_ == nullptr);
            stream_channel_ = channel;
        }

        *(reinterpret_cast<mx_handle_t*>(out_buf)) = client_endpoint.release();
        *out_actual = sizeof(mx_handle_t);
    }

    return res;
}

mx_status_t UsbAudioStream::ProcessChannel(DispatcherChannel* channel) {
    MX_DEBUG_ASSERT(channel != nullptr);
    mxtl::AutoLock lock(&lock_);

    return (rb_channel_.get() != channel)
        ? ProcessStreamChannelLocked(channel)
        : ProcessRingBufChannelLocked(channel);
}

#define HANDLE_REQ(_ioctl, _payload, _handler, _allow_noack)    \
case _ioctl:                                                    \
    if (req_size != sizeof(req._payload)) {                     \
        DEBUG_LOG("Bad " #_ioctl                                \
                  " response length (%u != %zu)\n",             \
                  req_size, sizeof(req._payload));              \
        return MX_ERR_INVALID_ARGS;                                \
    }                                                           \
    if (!_allow_noack && (req.hdr.cmd & AUDIO_FLAG_NO_ACK)) {  \
        DEBUG_LOG("NO_ACK flag not allowed for " #_ioctl "\n"); \
        return MX_ERR_INVALID_ARGS;                                \
    }                                                           \
    return _handler(channel, req._payload);
mx_status_t UsbAudioStream::ProcessStreamChannelLocked(DispatcherChannel* channel) {
    MX_DEBUG_ASSERT(channel != nullptr);

    // TODO(johngro) : Factor all of this behavior around accepting channels and
    // dispatching audio driver requests into some form of utility class so it
    // can be shared with the IntelHDA codec implementations as well.
    union {
        audio_proto::CmdHdr           hdr;
        audio_proto::StreamGetFmtsReq get_formats;
        audio_proto::StreamSetFmtReq  set_format;
        audio_proto::GetGainReq       get_gain;
        audio_proto::SetGainReq       set_gain;
        audio_proto::PlugDetectReq    plug_detect;
        // TODO(johngro) : add more commands here
    } req;

    static_assert(sizeof(req) <= 256,
                  "Request buffer is getting to be too large to hold on the stack!");

    uint32_t req_size;
    mx_status_t res = channel->Read(&req, sizeof(req), &req_size);
    if (res != MX_OK)
        return res;

    if ((req_size < sizeof(req.hdr) ||
        (req.hdr.transaction_id == AUDIO_INVALID_TRANSACTION_ID)))
        return MX_ERR_INVALID_ARGS;

    // Strip the NO_ACK flag from the request before selecting the dispatch target.
    auto cmd = static_cast<audio_proto::Cmd>(req.hdr.cmd & ~AUDIO_FLAG_NO_ACK);
    switch (cmd) {
        HANDLE_REQ(AUDIO_STREAM_CMD_GET_FORMATS, get_formats, OnGetStreamFormatsLocked, false);
        HANDLE_REQ(AUDIO_STREAM_CMD_SET_FORMAT,  set_format,  OnSetStreamFormatLocked,  false);
        HANDLE_REQ(AUDIO_STREAM_CMD_GET_GAIN,    get_gain,    OnGetGainLocked,          false);
        HANDLE_REQ(AUDIO_STREAM_CMD_SET_GAIN,    set_gain,    OnSetGainLocked,          true);
        HANDLE_REQ(AUDIO_STREAM_CMD_PLUG_DETECT, plug_detect, OnPlugDetectLocked,       true);
        default:
            DEBUG_LOG("Unrecognized stream command 0x%04x\n", req.hdr.cmd);
            return MX_ERR_NOT_SUPPORTED;
    }
}

mx_status_t UsbAudioStream::ProcessRingBufChannelLocked(DispatcherChannel* channel) {
    MX_DEBUG_ASSERT(channel != nullptr);

    union {
        audio_proto::CmdHdr                 hdr;
        audio_proto::RingBufGetFifoDepthReq get_fifo_depth;
        audio_proto::RingBufGetBufferReq    get_buffer;
        audio_proto::RingBufStartReq        rb_start;
        audio_proto::RingBufStopReq         rb_stop;
        // TODO(johngro) : add more commands here
    } req;

    static_assert(sizeof(req) <= 256,
                  "Request buffer is getting to be too large to hold on the stack!");

    uint32_t req_size;
    mx_status_t res = channel->Read(&req, sizeof(req), &req_size);
    if (res != MX_OK)
        return res;

    if ((req_size < sizeof(req.hdr) ||
        (req.hdr.transaction_id == AUDIO_INVALID_TRANSACTION_ID)))
        return MX_ERR_INVALID_ARGS;

    // Strip the NO_ACK flag from the request before selecting the dispatch target.
    auto cmd = static_cast<audio_proto::Cmd>(req.hdr.cmd & ~AUDIO_FLAG_NO_ACK);
    switch (cmd) {
        HANDLE_REQ(AUDIO_RB_CMD_GET_FIFO_DEPTH, get_fifo_depth, OnGetFifoDepthLocked, false);
        HANDLE_REQ(AUDIO_RB_CMD_GET_BUFFER,     get_buffer,     OnGetBufferLocked,    false);
        HANDLE_REQ(AUDIO_RB_CMD_START,          rb_start,       OnStartLocked,        false);
        HANDLE_REQ(AUDIO_RB_CMD_STOP,           rb_stop,        OnStopLocked,         false);
        default:
            DEBUG_LOG("Unrecognized ring buffer command 0x%04x\n", req.hdr.cmd);
            return MX_ERR_NOT_SUPPORTED;
    }

    return MX_ERR_NOT_SUPPORTED;
}
#undef HANDLE_REQ

mx_status_t UsbAudioStream::OnGetStreamFormatsLocked(DispatcherChannel* channel,
                                                     const audio_proto::StreamGetFmtsReq& req) {
    MX_DEBUG_ASSERT(channel != nullptr);
    size_t formats_sent = 0;
    audio_proto::StreamGetFmtsResp resp;

    if (supported_formats_.size() > mxtl::numeric_limits<uint16_t>::max()) {
        LOG("Too many formats (%zu) to send during AUDIO_STREAM_CMD_GET_FORMATS request!\n",
            supported_formats_.size());
        return MX_ERR_INTERNAL;
    }

    resp.hdr = req.hdr;
    resp.format_range_count = static_cast<uint16_t>(supported_formats_.size());

    do {
        uint16_t todo, payload_sz, to_send;
        mx_status_t res;

        todo = mxtl::min<uint16_t>(supported_formats_.size() - formats_sent,
                                   AUDIO_STREAM_CMD_GET_FORMATS_MAX_RANGES_PER_RESPONSE);
        payload_sz = sizeof(resp.format_ranges[0]) * todo;
        to_send = offsetof(audio_proto::StreamGetFmtsResp, format_ranges) + payload_sz;

        resp.first_format_range_ndx = formats_sent;
        ::memcpy(resp.format_ranges, supported_formats_.get() + formats_sent, payload_sz);

        res = channel->Write(&resp, sizeof(resp));
        if (res != MX_OK) {
            DEBUG_LOG("Failed to send get stream formats response (res %d)\n", res);
            return res;
        }

        formats_sent += todo;
    } while (formats_sent < supported_formats_.size());

    return MX_OK;
}

mx_status_t UsbAudioStream::OnSetStreamFormatLocked(DispatcherChannel* channel,
                                                    const audio_proto::StreamSetFmtReq& req) {
    MX_DEBUG_ASSERT(channel != nullptr);

    mx::channel client_rb_channel;
    audio_proto::StreamSetFmtResp resp;
    bool found_one = false;

    resp.hdr = req.hdr;

    // Only the privileged stream channel is allowed to change the format.
    if (channel->owner_ctx() != PRIVILEGED_CONNECTION_CTX) {
        MX_DEBUG_ASSERT(channel == stream_channel_.get());
        resp.result = MX_ERR_ACCESS_DENIED;
        goto finished;
    }

    // Check the format for compatibility
    for (const auto& fmt : supported_formats_) {
        if (audio::utils::FormatIsCompatible(req.frames_per_second,
                                             req.channels,
                                             req.sample_format,
                                             fmt)) {
            found_one = true;
            break;
        }
    }

    if (!found_one) {
        resp.result = MX_ERR_INVALID_ARGS;
        goto finished;
    }

    {
        // TODO(johngro) : If the ring buffer is running, should we automatically
        // stop it instead of returning bad state?
        mxtl::AutoLock txn_lock(&txn_lock_);
        if (ring_buffer_state_ != RingBufferState::STOPPED) {
            resp.result = MX_ERR_BAD_STATE;
            goto finished;
        }
    }

    // Determine the frame size.
    frame_size_ = audio::utils::ComputeFrameSize(req.channels, req.sample_format);
    if (!frame_size_) {
        LOG("Failed to compute frame size (ch %hu fmt 0x%08x)\n", req.channels, req.sample_format);
        resp.result = MX_ERR_INTERNAL;
        goto finished;
    }

    // Compute the size of our short packets, and the constants used to generate
    // the short/long packet cadence.  For now, assume that we will be operating
    // at a 1mSec isochronous rate.
    //
    // Make sure that we can fit our longest payload length into one of our
    // iotxns.
    //
    // TODO(johngro) : Unless/until we can find some way to set the USB bus
    // driver to perform direct DMA to/from the Ring Buffer VMO without the need
    // for software intervention, we may want to expose ways to either increase
    // the isochronous interval (to minimize load) or to use USB 2.0 125uSec
    // sub-frame timing (to decrease latency) if possible.
    uint32_t long_payload_len;
    iso_packet_rate_    = 1000;
    bytes_per_packet_   = (req.frames_per_second / iso_packet_rate_) * frame_size_;
    fractional_bpp_inc_ = (req.frames_per_second % iso_packet_rate_);
    long_payload_len    = bytes_per_packet_ + (fractional_bpp_inc_ ? frame_size_ : 0);
    if (long_payload_len > max_iotxn_size_) {
        resp.result = MX_ERR_INVALID_ARGS;
        goto finished;
    }

    // Looks like we are going ahead with this format change.  Tear down any
    // exiting ring buffer interface before proceeding.
    if (rb_channel_ != nullptr) {
        rb_channel_->Deactivate(false);
        rb_channel_.reset();
    }

    // We always try to keep two isochronous packets in flight at any point in
    // time.  Based on our cadence generation parameters, determine if it is
    // possible to have 0, 1 or 2 long packets back to back at any point in time
    // during the sequence.
    //
    // TODO(johngro): This is not the proper way to report the FIFO depth.  How
    // far ahead the USB controller will read ahead into its FIFO is going to be
    // a property of the controller and the properties of the endpoint.  It is
    // possible that this is negotiable to some extent as well.  I need to work
    // with voydanof@ to determine what we can expose from the USB bus driver in
    // order to report this accurately.
    //
    // Right now, we assume that the controller will never get farther ahead
    // than two isochronous iotxns, so we report this the worst case fifo_depth.
    fifo_bytes_ = bytes_per_packet_ << 1;

    // If we have no fractional portion to accumulate, we always send
    // short packets.  If our fractional portion is <= 1/2 of our
    // isochronous rate, then we will never send two long packets back
    // to back.
    if (fractional_bpp_inc_) {
        fifo_bytes_ += frame_size_;
        if (fractional_bpp_inc_ > (iso_packet_rate_ >> 1)) {
            fifo_bytes_ += frame_size_;
        }
    }

    // Send the commands required to set up the new format.
    //
    // TODO(johngro): more work is needed if we are changing sample format or
    // channel count.  Right now, we only support the one format/count provided
    // to us by the outer layer, but eventually we need to support them all.
    MX_DEBUG_ASSERT(parent_ != nullptr);
    resp.result = usb_audio_set_sample_rate(&usb_, usb_ep_addr_, req.frames_per_second);
    if (resp.result != MX_OK) goto finished;

    // Create a new ring buffer channel which can be used to move bulk data and
    // bind it to us.
    rb_channel_ = DispatcherChannelAllocator::New();
    if (rb_channel_ == nullptr) {
        resp.result = MX_ERR_NO_MEMORY;
        goto finished;
    }

    resp.result = rb_channel_->Activate(mxtl::WrapRefPtr(this), &client_rb_channel);
    if (resp.result != MX_OK) rb_channel_.reset();

finished:
    if (resp.result == MX_OK) {
        return channel->Write(&resp, sizeof(resp), mxtl::move(client_rb_channel));
    } else {
        return channel->Write(&resp, sizeof(resp));
    }
}

mx_status_t UsbAudioStream::OnGetGainLocked(DispatcherChannel* channel,
                                            const audio_proto::GetGainReq& req) {
    MX_DEBUG_ASSERT(channel != nullptr);
    audio_proto::GetGainResp resp;

    resp.hdr       = req.hdr;
    resp.cur_mute  = false;
    resp.cur_gain  = 0.0;
    resp.can_mute  = false;
    resp.min_gain  = 0.0;
    resp.max_gain  = 0.0;
    resp.gain_step = 0.0;

    return channel->Write(&resp, sizeof(resp));
}

mx_status_t UsbAudioStream::OnSetGainLocked(DispatcherChannel* channel,
                                            const audio_proto::SetGainReq& req) {
    MX_DEBUG_ASSERT(channel != nullptr);
    if (req.hdr.cmd & AUDIO_FLAG_NO_ACK)
        return MX_OK;

    audio_proto::SetGainResp resp;
    resp.hdr = req.hdr;

    bool illegal_mute = (req.flags & AUDIO_SGF_MUTE_VALID) && (req.flags & AUDIO_SGF_MUTE);
    bool illegal_gain = (req.flags & AUDIO_SGF_GAIN_VALID) && (req.gain != 0.0f);

    resp.cur_mute = false;
    resp.cur_gain = 0.0;
    resp.result   = (illegal_mute || illegal_gain)
                  ? MX_ERR_INVALID_ARGS
                  : MX_OK;

    return channel->Write(&resp, sizeof(resp));
}

mx_status_t UsbAudioStream::OnPlugDetectLocked(DispatcherChannel* channel,
                                               const audio_proto::PlugDetectReq& req) {
    if (req.hdr.cmd & AUDIO_FLAG_NO_ACK)
        return MX_OK;

    audio_proto::PlugDetectResp resp;
    resp.hdr   = req.hdr;
    resp.flags = static_cast<audio_pd_notify_flags_t>(AUDIO_PDNF_HARDWIRED |
                                                       AUDIO_PDNF_PLUGGED);
    resp.plug_state_time = create_time_;

    return channel->Write(&resp, sizeof(resp));
}

mx_status_t UsbAudioStream::OnGetFifoDepthLocked(DispatcherChannel* channel,
                                                 const audio_proto::RingBufGetFifoDepthReq& req) {
    audio_proto::RingBufGetFifoDepthResp resp;

    resp.hdr = req.hdr;
    resp.result = MX_OK;
    resp.fifo_depth = fifo_bytes_;

    return channel->Write(&resp, sizeof(resp));
}

mx_status_t UsbAudioStream::OnGetBufferLocked(DispatcherChannel* channel,
                                              const audio_proto::RingBufGetBufferReq& req) {
    audio_proto::RingBufGetBufferResp resp;
    mx::vmo client_rb_handle;
    uint32_t map_flags, client_rights;

    resp.hdr    = req.hdr;
    resp.result = MX_ERR_INTERNAL;

    {
        // We cannot create a new ring buffer if we are not currently stopped.
        mxtl::AutoLock txn_lock(&txn_lock_);
        if (ring_buffer_state_ != RingBufferState::STOPPED) {
            resp.result = MX_ERR_BAD_STATE;
            goto finished;
        }
    }

    // Unmap and release any previous ring buffer.
    ReleaseRingBufferLocked();

    // Compute the ring buffer size.  It needs to be at least as big
    // as the virtual fifo depth.
    MX_DEBUG_ASSERT(frame_size_ && ((fifo_bytes_ % frame_size_) == 0));
    MX_DEBUG_ASSERT(fifo_bytes_ && ((fifo_bytes_ % fifo_bytes_) == 0));
    ring_buffer_size_  = req.min_ring_buffer_frames;
    ring_buffer_size_ *= frame_size_;
    if (ring_buffer_size_ < fifo_bytes_)
        ring_buffer_size_ = fifo_bytes_;

    // Set up our state for generating notifications.
    if (req.notifications_per_ring) {
        bytes_per_notification_ = ring_buffer_size_ / req.notifications_per_ring;
    } else {
        bytes_per_notification_ = 0;
    }

    // Create the ring buffer vmo we will use to share memory with the client.
    resp.result = mx::vmo::create(ring_buffer_size_, 0, &ring_buffer_vmo_);
    if (resp.result != MX_OK) {
        LOG("Failed to create ring buffer (size %u, res %d)\n", ring_buffer_size_, resp.result);
        goto finished;
    }

    // Map the VMO into our address space.
    //
    // TODO(johngro): skip this step when APIs in the USB bus driver exist to
    // DMA directly from the VMO.
    map_flags = MX_VM_FLAG_PERM_READ;
    if (is_input())
        map_flags |= MX_VM_FLAG_PERM_WRITE;

    resp.result = mx::vmar::root_self().map(0, ring_buffer_vmo_,
                                            0, ring_buffer_size_,
                                            map_flags,
                                            reinterpret_cast<uintptr_t*>(&ring_buffer_virt_));
    if (resp.result != MX_OK) {
        LOG("Failed to map ring buffer (size %u, res %d)\n", ring_buffer_size_, resp.result);
        goto finished;
    }

    // Create the client's handle to the ring buffer vmo and set it back to them.
    client_rights = MX_RIGHT_TRANSFER | MX_RIGHT_MAP | MX_RIGHT_READ;
    if (!is_input())
        client_rights |= MX_RIGHT_WRITE;

    resp.result = ring_buffer_vmo_.duplicate(client_rights, &client_rb_handle);
    if (resp.result != MX_OK) {
        LOG("Failed to duplicate ring buffer handle (res %d)\n", resp.result);
        goto finished;
    }

finished:
    mx_status_t res;
    if (resp.result == MX_OK) {
        MX_DEBUG_ASSERT(client_rb_handle.is_valid());
        res = channel->Write(&resp, sizeof(resp), mxtl::move(client_rb_handle));
    } else {
        res = channel->Write(&resp, sizeof(resp));
    }

    if (res != MX_OK)
        ReleaseRingBufferLocked();

    return res;
}

mx_status_t UsbAudioStream::OnStartLocked(DispatcherChannel* channel,
                                          const audio_proto::RingBufStartReq& req) {
    audio_proto::RingBufStartResp resp;
    resp.hdr = req.hdr;
    resp.start_ticks = 0;

    mxtl::AutoLock txn_lock(&txn_lock_);

    if (ring_buffer_state_ != RingBufferState::STOPPED) {
        // The ring buffer is running, do not linger in the lock while we send
        // the error code back to the user.
        txn_lock.release();
        resp.result = MX_ERR_BAD_STATE;
        return channel->Write(&resp, sizeof(resp));
    }

    // We are idle, all of our iotxns should be sitting in the free list.
    MX_DEBUG_ASSERT(allocated_iotxn_cnt_ == free_iotxn_cnt_);

    // switch to alternate interface if necessary
    if (alt_setting_ != 0) {
        usb_set_interface(&usb_, iface_num_, alt_setting_);
    }

    // Initialize the counters used to...
    // 1) generate the short/long packet cadence.
    // 2) generate notifications.
    // 3) track the position in the ring buffer.
    fractional_bpp_acc_ = 0;
    notification_acc_   = 0;
    ring_buffer_offset_ = 0;
    ring_buffer_pos_    = 0;

    // Schedule the frame number which the first transaction will go out on.
    //
    // TODO(johngro): This cannot be the current frame number, that train
    // has already left the station.  It probably should not be the next frame
    // number either as that train might be just about to leave the station.
    //
    // For now, set this to be the current frame number +2 and use the first
    // transaction complete callback to estimate the DMA start time.  Moving
    // forward, when the USB bus driver can tell us which frame a transaction
    // went out on, schedule the transaction using the special "on the next USB
    // isochronous frame" sentinel value and figure out which frame that was
    // during the callback.
    size_t read_amt;
    resp.result = device_ioctl(parent_, IOCTL_USB_GET_CURRENT_FRAME,
                               NULL, 0,
                               &usb_frame_num_, sizeof(usb_frame_num_),
                               &read_amt);
    if ((resp.result != MX_OK) || (read_amt != sizeof(usb_frame_num_))) {
        LOG("Failed to fetch USB frame number!  (res %d, amt %zu)\n", resp.result, read_amt);
        if (alt_setting_ != 0) {
            usb_set_interface(&usb_, iface_num_, 0);
        }
        return channel->Write(&resp, sizeof(resp));
    }

    usb_frame_num_ += 2;

    // Queue up all of our transactions.
    while (!list_is_empty(&free_iotxn_))
        QueueIotxnLocked();

    // Flag ourselves as being in the starting state, record the transaction ID
    // we will send back to our client when we have successfully started, then
    // get out.
    ring_buffer_state_ = RingBufferState::STARTING;
    pending_job_resp_.start = resp;
    return MX_OK;
}

mx_status_t UsbAudioStream::OnStopLocked(DispatcherChannel* channel,
                                         const audio_proto::RingBufStopReq& req) {
    mxtl::AutoLock txn_lock(&txn_lock_);

    // TODO(johngro): We currently cannot cancel USB transactions once queued.
    // When we can, we can come back and simply cancel the in-flight
    // transactions instead of having an intermediate STOPPING state we use to
    // wait for the transactions in flight to finish via IotxnComplete.
    if (ring_buffer_state_ != RingBufferState::STARTED) {
        audio_proto::RingBufStopResp resp;

        txn_lock.release();
        resp.hdr = req.hdr;
        resp.result = MX_ERR_BAD_STATE;

        return channel->Write(&resp, sizeof(resp));
    }

    ring_buffer_state_ = RingBufferState::STOPPING;
    pending_job_resp_.stop.hdr = req.hdr;

    return MX_OK;
}

void UsbAudioStream::IotxnComplete(iotxn_t* txn) {
    enum class Action {
        NONE,
        SIGNAL_STARTED,
        SIGNAL_STOPPED,
        NOTIFY_POSITION,
        HANDLE_UNPLUG,
    };

    union {
        audio_proto::RingBufStopResp  stop;
        audio_proto::RingBufStartResp start;
        audio_proto::RingBufPositionNotify notify_pos;
    } resp;

    uint64_t complete_time = mx_ticks_get();
    Action when_finished = Action::NONE;

    // TODO(johngro) : See MG-940.  Eliminate this as soon as we have a more
    // official way of meeting real-time latency requirements.  Also, the fact
    // that this boosting gets done after the first transaction completes
    // degrades the quality of the startup time estimate (if the system is under
    // high load when the system starts up).  As a general issue, there are
    // better ways of refining this estimate than bumping the thread prio before
    // the first transaction gets queued.  Therefor, we just have a poor
    // estimate for now and will need to live with the consequences.
    if (!iotxn_complete_prio_bumped_) {
        mx_thread_set_priority(24 /* HIGH_PRIORITY in LK */);
        iotxn_complete_prio_bumped_ = true;
    }

    {
        mxtl::AutoLock txn_lock(&txn_lock_);

        // Cache the status and lenght of this io transaction.
        mx_status_t txn_status = txn->status;
        uint32_t txn_length = txn->length;

        // Complete the iotxn.  This will return the transaction to the free
        // list and (in the case of an input stream) copy the payload to the
        // ring buffer, and update the ring buffer position.
        //
        // TODO(johngro): copying the payload out of the ring buffer is an
        // operation which goes away when we get to the zero copy world.
        CompleteIotxnLocked(txn);

        // Did the transaction fail because the device was unplugged?  If so,
        // enter the stopping state and close the connections to our clients.
        if (txn_status == MX_ERR_IO_NOT_PRESENT) {
            ring_buffer_state_ = RingBufferState::STOPPING_AFTER_UNPLUG;
        } else {
            // If we are supposed to be delivering notifications, check to see
            // if it is time to do so.
            if (bytes_per_notification_) {
                notification_acc_ += txn_length;

                if ((ring_buffer_state_ == RingBufferState::STARTED) &&
                    (notification_acc_ >= bytes_per_notification_)) {
                    when_finished = Action::NOTIFY_POSITION;
                    notification_acc_ = (notification_acc_ % bytes_per_notification_);
                    resp.notify_pos.ring_buffer_pos = ring_buffer_pos_;
                }
            }
        }

        switch (ring_buffer_state_) {
        case RingBufferState::STOPPING:
            if (free_iotxn_cnt_ == allocated_iotxn_cnt_) {
                resp.stop = pending_job_resp_.stop;
                when_finished = Action::SIGNAL_STOPPED;
            }
            break;

        case RingBufferState::STOPPING_AFTER_UNPLUG:
            if (free_iotxn_cnt_ == allocated_iotxn_cnt_) {
                resp.stop = pending_job_resp_.stop;
                when_finished = Action::HANDLE_UNPLUG;
            }
            break;

        case RingBufferState::STARTING:
            resp.start = pending_job_resp_.start;
            when_finished = Action::SIGNAL_STARTED;
            break;

        case RingBufferState::STARTED:
            QueueIotxnLocked();
            break;

        case RingBufferState::STOPPED:
        default:
            LOG("Invalid state (%u) in %s\n",
                static_cast<uint32_t>(ring_buffer_state_), __PRETTY_FUNCTION__);
            MX_DEBUG_ASSERT(false);
            break;
        }
    }

    if (when_finished != Action::NONE) {
        mxtl::AutoLock lock(&lock_);
        switch (when_finished) {
        case Action::SIGNAL_STARTED:
            if (rb_channel_ != nullptr) {
                // TODO(johngro) : this start time estimate is not as good as it
                // could be.  We really need to have the USB bus driver report
                // the relationship between the USB frame counter and the system
                // tick counter (and track the relationship in the case that the
                // USB oscillator is not derived from the system oscillator).
                // Then we can accurately report the start time as the time of
                // the tick on which we scheduled the first transaction.
                resp.start.result = MX_OK;
                resp.start.start_ticks = complete_time - ticks_per_msec_;
                rb_channel_->Write(&resp.start, sizeof(resp.start));
            }
            {
                mxtl::AutoLock txn_lock(&txn_lock_);
                ring_buffer_state_ = RingBufferState::STARTED;
            }
            break;

        case Action::HANDLE_UNPLUG:
            if (rb_channel_ != nullptr) {
                rb_channel_->Deactivate(false);
                rb_channel_.reset();
            }

            if (stream_channel_ != nullptr) {
                stream_channel_->Deactivate(false);
                stream_channel_.reset();
            }

            {
                mxtl::AutoLock txn_lock(&txn_lock_);
                ring_buffer_state_ = RingBufferState::STOPPED;
            }
            break;

        case Action::SIGNAL_STOPPED:
            if (rb_channel_ != nullptr) {
                resp.stop.result = MX_OK;
                rb_channel_->Write(&resp.stop, sizeof(resp.stop));
            }
            {
                mxtl::AutoLock txn_lock(&txn_lock_);
                ring_buffer_state_ = RingBufferState::STOPPED;
            }
            break;

        case Action::NOTIFY_POSITION:
            resp.notify_pos.hdr.cmd = AUDIO_RB_POSITION_NOTIFY;
            resp.notify_pos.hdr.transaction_id = AUDIO_INVALID_TRANSACTION_ID;
            rb_channel_->Write(&resp.notify_pos, sizeof(resp.notify_pos));
            break;

        default:
            MX_DEBUG_ASSERT(false);
            break;
        }
    }
}

void UsbAudioStream::QueueIotxnLocked() {
    MX_DEBUG_ASSERT((ring_buffer_state_ == RingBufferState::STARTING) ||
                    (ring_buffer_state_ == RingBufferState::STARTED));
    MX_DEBUG_ASSERT(!list_is_empty(&free_iotxn_));

    // Figure out how much we want to send or receive this time (short or long
    // packet)
    uint32_t todo = bytes_per_packet_;
    fractional_bpp_acc_ += fractional_bpp_inc_;
    if (fractional_bpp_acc_ >= iso_packet_rate_) {
        fractional_bpp_acc_ -= iso_packet_rate_;
        todo += frame_size_;
        MX_DEBUG_ASSERT(fractional_bpp_acc_ < iso_packet_rate_);
    }

    // Grab a free iotxn.
    auto txn = list_remove_head_type(&free_iotxn_, iotxn_t, node);
    MX_DEBUG_ASSERT(txn != nullptr);
    MX_DEBUG_ASSERT(free_iotxn_cnt_ > 0);
    --free_iotxn_cnt_;

    // If this is an output stream, copy our data into the iotxn.
    // TODO(johngro): eliminate this when we can get to a zero-copy world.
    if (!is_input()) {
        uint32_t avail = ring_buffer_size_ - ring_buffer_offset_;
        MX_DEBUG_ASSERT(ring_buffer_offset_ < ring_buffer_size_);
        MX_DEBUG_ASSERT((avail % frame_size_) == 0);
        uint32_t amt = mxtl::min(avail, todo);

        const uint8_t* src = reinterpret_cast<uint8_t*>(ring_buffer_virt_) + ring_buffer_offset_;
        iotxn_copyto(txn, src, amt, 0);
        if (amt == avail) {
            ring_buffer_offset_ = todo - amt;
            if (ring_buffer_offset_ > 0) {
                iotxn_copyto(txn, ring_buffer_virt_, ring_buffer_offset_, amt);
            }
        } else {
            ring_buffer_offset_ += amt;
        }
    }

    usb_iotxn_set_frame(txn, usb_frame_num_++);
    txn->length = todo;
    iotxn_queue(parent_, txn);
}

void UsbAudioStream::CompleteIotxnLocked(iotxn_t* txn) {
    MX_DEBUG_ASSERT(txn);

    // If we are an input stream, copy the payload into the ring buffer.
    if (is_input()) {
        uint32_t todo = txn->length;

        uint32_t avail = ring_buffer_size_ - ring_buffer_offset_;
        MX_DEBUG_ASSERT(ring_buffer_offset_ < ring_buffer_size_);
        MX_DEBUG_ASSERT((avail % frame_size_) == 0);

        uint32_t amt = mxtl::min(avail, todo);
        uint8_t* dst = reinterpret_cast<uint8_t*>(ring_buffer_virt_) + ring_buffer_offset_;

        if (txn->status == MX_OK) {
            iotxn_copyfrom(txn, dst, amt, 0);
            if (amt < todo) {
                iotxn_copyfrom(txn, ring_buffer_virt_, todo - amt, amt);
            }
        } else {
            // TODO(johngro): filling with zeros is only the proper thing to do
            // for signed formats.  USB does support unsigned 8-bit audio; if
            // that is our format, we should fill with 0x80 instead in order to
            // fill with silence.
            memset(dst, 0, amt);
            if (amt < todo) {
                memset(ring_buffer_virt_, 0, todo - amt);
            }
        }
    }

    // Update the ring buffer position.
    ring_buffer_pos_ += txn->length;
    if (ring_buffer_pos_ >= ring_buffer_size_) {
        ring_buffer_pos_ -= ring_buffer_size_;
        MX_DEBUG_ASSERT(ring_buffer_pos_ < ring_buffer_size_);
    }

    // If this is an input stream, the ring buffer offset should always be equal
    // to the stream position.
    if (is_input()) {
        ring_buffer_offset_ = ring_buffer_pos_;
    }

    // Return the transaction to the free list.
    list_add_head(&free_iotxn_, &txn->node);
    ++free_iotxn_cnt_;
    MX_DEBUG_ASSERT(free_iotxn_cnt_ <= allocated_iotxn_cnt_);
}

void UsbAudioStream::NotifyChannelDeactivated(const DispatcherChannel& channel) {
    mxtl::AutoLock lock(&lock_);

    if (channel.owner_ctx() == PRIVILEGED_CONNECTION_CTX) {
        MX_DEBUG_ASSERT(stream_channel_.get() == &channel);
        MX_DEBUG_ASSERT(rb_channel_.get() != &channel);
        stream_channel_.reset();
    } else
    if (&channel == rb_channel_.get()) {
        MX_DEBUG_ASSERT(stream_channel_.get() != &channel);

        {
            mxtl::AutoLock txn_lock(&txn_lock_);
            if (ring_buffer_state_ != RingBufferState::STOPPED) {
                ring_buffer_state_ = RingBufferState::STOPPING;
            }
        }

        rb_channel_.reset();
    }
}

}  // namespace usb
}  // namespace audio

extern "C"
mx_status_t usb_audio_sink_create(mx_device_t* device, usb_protocol_t* usb, int index,
                                  usb_interface_descriptor_t* intf,
                                  usb_endpoint_descriptor_t* ep,
                                  usb_audio_ac_format_type_i_desc* format_desc) {
    return audio::usb::UsbAudioStream::Create(false, device, usb, index, intf, ep, format_desc);
}

extern "C"
mx_status_t usb_audio_source_create(mx_device_t* device, usb_protocol_t* usb, int index,
                                    usb_interface_descriptor_t* intf,
                                    usb_endpoint_descriptor_t* ep,
                                    usb_audio_ac_format_type_i_desc* format_desc) {
    return audio::usb::UsbAudioStream::Create(true, device, usb, index, intf, ep, format_desc);
}
