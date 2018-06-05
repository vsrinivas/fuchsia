// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <audio-proto-utils/format-utils.h>
#include <ddk/device.h>
#include <digest/digest.h>
#include <lib/zx/vmar.h>
#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <fbl/limits.h>
#include <string.h>
#include <zircon/device/usb.h>
#include <zircon/hw/usb-audio.h>
#include <zircon/process.h>
#include <zircon/types.h>

#include <dispatcher-pool/dispatcher-thread-pool.h>

#include "usb-audio.h"
#include "usb-audio-device.h"
#include "usb-audio-stream.h"
#include "usb-audio-stream-interface.h"

namespace audio {
namespace usb {

static constexpr uint32_t MAX_OUTSTANDING_REQ = 8;

static constexpr uint32_t ExtractSampleRate(const usb_audio_as_samp_freq& sr) {
    return static_cast<uint32_t>(sr.freq[0])
        | (static_cast<uint32_t>(sr.freq[1]) << 8)
        | (static_cast<uint32_t>(sr.freq[2]) << 16);
}

UsbAudioStream::UsbAudioStream(UsbAudioDevice* parent,
                               fbl::unique_ptr<UsbAudioStreamInterface> ifc,
                               fbl::RefPtr<dispatcher::ExecutionDomain> default_domain)
    : UsbAudioStreamBase(parent->zxdev()),
      AudioStreamProtocol(ifc->direction() == Direction::Input),
      parent_(*parent),
      ifc_(fbl::move(ifc)),
      default_domain_(fbl::move(default_domain)),
      create_time_(zx_clock_get(ZX_CLOCK_MONOTONIC)) {
    snprintf(log_prefix_, sizeof(log_prefix_),
             "UsbAud %04hx:%04hx %s-%03d",
             parent_.vid(),
             parent_.pid(),
             is_input() ? "input" : "output",
             ifc_->term_link());
}

UsbAudioStream::~UsbAudioStream() {
    // We destructing.  All of our requests should be sitting in the free list.
    ZX_DEBUG_ASSERT(allocated_req_cnt_ == free_req_cnt_);

    while (!list_is_empty(&free_req_)) {
        usb_request_release(list_remove_head_type(&free_req_, usb_request_t, node));
    }
}

fbl::RefPtr<UsbAudioStream> UsbAudioStream::Create(UsbAudioDevice* parent,
                                                   fbl::unique_ptr<UsbAudioStreamInterface> ifc) {
    ZX_DEBUG_ASSERT(parent != nullptr);
    ZX_DEBUG_ASSERT(ifc != nullptr);

    auto domain = dispatcher::ExecutionDomain::Create();
    if (domain == nullptr) {
        LOG_EX(ERROR, *parent,
               "Failed to create execution domain while trying to create UsbAudioStream\n");
        return nullptr;
    }

    fbl::AllocChecker ac;
    auto stream = fbl::AdoptRef(
            new (&ac) UsbAudioStream(parent, fbl::move(ifc), fbl::move(domain)));
    if (!ac.check()) {
        LOG_EX(ERROR, *parent,
               "Out of memory while attempting to allocate UsbAudioStream\n");
        return nullptr;
    }

    stream->ComputePersistentUniqueId();

    return stream;
}

zx_status_t UsbAudioStream::Bind() {
    // TODO(johngro): Do this differently when we have the ability to queue io
    // transactions to a USB isochronous endpoint and can have the bus driver
    // DMA directly from the ring buffer we have set up with our user.
    {
        fbl::AutoLock req_lock(&req_lock_);

        list_initialize(&free_req_);
        free_req_cnt_ = 0;
        allocated_req_cnt_ = 0;

        for (uint32_t i = 0; i < MAX_OUTSTANDING_REQ; ++i) {
            usb_request_t* req;
            zx_status_t status = usb_req_alloc(&parent_.usb_proto(),
                                               &req,
                                               ifc_->max_req_size(),
                                               ifc_->ep_addr());
            if (status != ZX_OK) {
                LOG(ERROR, "Failed to allocate usb request %u/%u (size %u): %d\n",
                    i + 1, MAX_OUTSTANDING_REQ, ifc_->max_req_size(), status);
                return status;
            }

            req->cookie = this;
            req->complete_cb = [](usb_request_t* req, void* cookie) -> void {
                ZX_DEBUG_ASSERT(cookie != nullptr);
                reinterpret_cast<UsbAudioStream*>(cookie)->RequestComplete(req);
            };

            list_add_head(&free_req_, &req->node);
            ++free_req_cnt_;
            ++allocated_req_cnt_;
        }
    }

    char name[64];
    snprintf(name, sizeof(name), "usb-audio-%s-%03d",
             is_input() ? "input" : "output", ifc_->term_link());

    zx_status_t status = UsbAudioStreamBase::DdkAdd(name);
    if (status == ZX_OK) {
        // If bind/setup has succeeded, then the devmgr now holds a reference to us.
        // Manually increase our reference count to account for this.
        this->AddRef();
    } else {
        LOG(ERROR, "Failed to publish UsbAudioStream device node (name \"%s\", status %d)\n",
            name, status);
    }

    return status;
}

void UsbAudioStream::ComputePersistentUniqueId() {
    // Do the best that we can to generate a persistent ID unique to this audio
    // stream by blending information from a number of sources.  In particular,
    // consume...
    //
    // 1) This USB device's top level device descriptor (this contains the
    //    VID/PID of the device, among other things)
    // 2) The contents of the descriptor list used to describe the control and
    //    streaming interfaces present in the device.
    // 3) The manufacturer, product, and serial number string descriptors (if
    //    present)
    // 4) The stream interface ID.
    //
    // The goal here is to produce something like a UUID which is as unique to a
    // specific instance of a specific device as we can make it, but which
    // should persist across boots even in the presence of driver updates an
    // such.  Even so, upper levels of code will still need to deal with the sad
    // reality that some types of devices may end up looking the same between
    // two different instances.  If/when this becomes an issue, we may need to
    // pursue other options.  One choice might be to change the way devices are
    // enumerated in the USB section of the device tree so that their path has
    // only to do with physical topology, and has no runtime enumeration order
    // dependencies.  At that point in time, adding the topology into the hash
    // should do the job, but would imply that the same device plugged into two
    // different ports will have a different unique ID for the purposes of
    // saving and restoring driver settings (as it does in some operating
    // systems today).
    //
    uint16_t vid = parent_.desc().idVendor;
    uint16_t pid = parent_.desc().idProduct;
    audio_stream_unique_id_t fallback_id { .data = {
        'U', 'S', 'B', ' ',
        static_cast<uint8_t>(vid >> 8),
        static_cast<uint8_t>(vid),
        static_cast<uint8_t>(pid >> 8),
        static_cast<uint8_t>(pid),
        ifc_->iid()
    }};
    persistent_unique_id_ = fallback_id;

    digest::Digest sha;
    zx_status_t res = sha.Init();
    if (res != ZX_OK) {
        LOG(WARN, "Failed to initialize digest while computing unique ID.  "
                  "Falling back on defaults (res %d)\n", res);
        return;
    }

    // #1: Top level descriptor.
    sha.Update(&parent_.desc(), sizeof(parent_.desc()));

    // #2: The descriptor list
    const auto& desc_list = parent_.desc_list();
    ZX_DEBUG_ASSERT((desc_list != nullptr) && (desc_list->size() > 0));
    sha.Update(desc_list->data(), desc_list->size());

    // #3: The various descriptor strings which may exist.
    const fbl::Array<uint8_t>* desc_strings[] = {
        &parent_.mfr_name(),
        &parent_.prod_name(),
        &parent_.serial_num()
    };
    for (const auto str : desc_strings) {
        if (str->size()) {
            sha.Update(str->get(), str->size());
        }
    }

    // #4: The stream interface's ID.
    auto iid = ifc_->iid();
    sha.Update(&iid, sizeof(iid));

    // Finish the SHA and attempt to copy as much of the results to our internal
    // cached representation as we can.
    uint8_t digest_out[digest::Digest::kLength];
    sha.Final();
    res = sha.CopyTo(digest_out, sizeof(digest_out));
    if (res != ZX_OK) {
        LOG(WARN, "Failed to copy digest while computing unique ID.  "
                  "Falling back on defaults (res %d)\n", res);
        return;
    }

    constexpr size_t todo = fbl::min(sizeof(digest_out), sizeof(persistent_unique_id_.data));
    if (todo < sizeof(persistent_unique_id_.data)) {
        ::memset(&persistent_unique_id_.data, 0, sizeof(persistent_unique_id_.data));
    }
    ::memcpy(persistent_unique_id_.data, digest_out, todo);
}

void UsbAudioStream::ReleaseRingBufferLocked() {
    if (ring_buffer_virt_ != nullptr) {
        ZX_DEBUG_ASSERT(ring_buffer_size_ != 0);
        zx::vmar::root_self().unmap(reinterpret_cast<uintptr_t>(ring_buffer_virt_),
                                    ring_buffer_size_);
        ring_buffer_virt_ = nullptr;
        ring_buffer_size_ = 0;
    }
    ring_buffer_vmo_.reset();
}

void UsbAudioStream::DdkUnbind() {
    // Close all of our client event sources if we have not already.
    default_domain_->Deactivate();

    // Unpublish our device node.
    DdkRemove();
}

void UsbAudioStream::DdkRelease() {
    // Reclaim our reference from the driver framework and let it go out of
    // scope.  If this is our last reference (it should be), we will destruct
    // immediately afterwards.
    auto stream = fbl::internal::MakeRefPtrNoAdopt(this);

    // Make sure that our parent is no longer holding a reference to us.
    parent_.RemoveAudioStream(stream);
}

zx_status_t UsbAudioStream::DdkIoctl(uint32_t op,
                                     const void* in_buf, size_t in_len,
                                     void* out_buf, size_t out_len, size_t* out_actual) {
    // The only IOCTL we support is get channel.
    if (op != AUDIO_IOCTL_GET_CHANNEL) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    if ((out_buf == nullptr) ||
        (out_actual == nullptr) ||
        (out_len != sizeof(zx_handle_t))) {
        return ZX_ERR_INVALID_ARGS;
    }

    fbl::AutoLock lock(&lock_);

    // Attempt to allocate a new driver channel and bind it to us.  If we don't
    // already have an stream_channel_, flag this channel is the privileged
    // connection (The connection which is allowed to do things like change
    // formats).
    bool privileged = (stream_channel_ == nullptr);
    auto channel = dispatcher::Channel::Create();
    if (channel == nullptr)
        return ZX_ERR_NO_MEMORY;

    dispatcher::Channel::ProcessHandler phandler(
    [stream = fbl::WrapRefPtr(this), privileged](dispatcher::Channel* channel) -> zx_status_t {
        OBTAIN_EXECUTION_DOMAIN_TOKEN(t, stream->default_domain_);
        return stream->ProcessStreamChannel(channel, privileged);
    });

    dispatcher::Channel::ChannelClosedHandler chandler;
    if (privileged) {
        chandler = dispatcher::Channel::ChannelClosedHandler(
        [stream = fbl::WrapRefPtr(this)](const dispatcher::Channel* channel) -> void {
            OBTAIN_EXECUTION_DOMAIN_TOKEN(t, stream->default_domain_);
            stream->DeactivateStreamChannel(channel);
        });
    }

    zx::channel client_endpoint;
    zx_status_t res = channel->Activate(&client_endpoint,
                                        default_domain_,
                                        fbl::move(phandler),
                                        fbl::move(chandler));
    if (res == ZX_OK) {
        if (privileged) {
            ZX_DEBUG_ASSERT(stream_channel_ == nullptr);
            stream_channel_ = channel;
        }

        *(reinterpret_cast<zx_handle_t*>(out_buf)) = client_endpoint.release();
        *out_actual = sizeof(zx_handle_t);
    }

    return res;
}

#define HREQ(_cmd, _payload, _handler, _allow_noack, ...)         \
case _cmd:                                                        \
    if (req_size != sizeof(req._payload)) {                       \
        LOG(TRACE, "Bad " #_cmd " response length (%u != %zu)\n", \
            req_size, sizeof(req._payload));                      \
        return ZX_ERR_INVALID_ARGS;                               \
    }                                                             \
    if (!_allow_noack && (req.hdr.cmd & AUDIO_FLAG_NO_ACK)) {     \
        LOG(TRACE, "NO_ACK flag not allowed for " #_cmd "\n");    \
        return ZX_ERR_INVALID_ARGS;                               \
    }                                                             \
    return _handler(channel, req._payload, ##__VA_ARGS__);
zx_status_t UsbAudioStream::ProcessStreamChannel(dispatcher::Channel* channel, bool priv) {
    ZX_DEBUG_ASSERT(channel != nullptr);
    fbl::AutoLock lock(&lock_);

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
        audio_proto::GetUniqueIdReq   get_unique_id;
        audio_proto::GetStringReq     get_string;
        // TODO(johngro) : add more commands here
    } req;

    static_assert(sizeof(req) <= 256,
                  "Request buffer is getting to be too large to hold on the stack!");

    uint32_t req_size;
    zx_status_t res = channel->Read(&req, sizeof(req), &req_size);
    if (res != ZX_OK)
        return res;

    if ((req_size < sizeof(req.hdr) ||
        (req.hdr.transaction_id == AUDIO_INVALID_TRANSACTION_ID)))
        return ZX_ERR_INVALID_ARGS;

    // Strip the NO_ACK flag from the request before selecting the dispatch target.
    auto cmd = static_cast<audio_proto::Cmd>(req.hdr.cmd & ~AUDIO_FLAG_NO_ACK);
    switch (cmd) {
    HREQ(AUDIO_STREAM_CMD_GET_FORMATS,   get_formats,   OnGetStreamFormatsLocked, false);
    HREQ(AUDIO_STREAM_CMD_SET_FORMAT,    set_format,    OnSetStreamFormatLocked,  false, priv);
    HREQ(AUDIO_STREAM_CMD_GET_GAIN,      get_gain,      OnGetGainLocked,          false);
    HREQ(AUDIO_STREAM_CMD_SET_GAIN,      set_gain,      OnSetGainLocked,          true);
    HREQ(AUDIO_STREAM_CMD_PLUG_DETECT,   plug_detect,   OnPlugDetectLocked,       true);
    HREQ(AUDIO_STREAM_CMD_GET_UNIQUE_ID, get_unique_id, OnGetUniqueIdLocked,      false);
    HREQ(AUDIO_STREAM_CMD_GET_STRING,    get_string,    OnGetStringLocked,        false);
    default:
        LOG(TRACE, "Unrecognized stream command 0x%04x\n", req.hdr.cmd);
        return ZX_ERR_NOT_SUPPORTED;
    }
}

zx_status_t UsbAudioStream::ProcessRingBufferChannel(dispatcher::Channel* channel) {
    ZX_DEBUG_ASSERT(channel != nullptr);
    fbl::AutoLock lock(&lock_);

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
    zx_status_t res = channel->Read(&req, sizeof(req), &req_size);
    if (res != ZX_OK)
        return res;

    if ((req_size < sizeof(req.hdr) ||
        (req.hdr.transaction_id == AUDIO_INVALID_TRANSACTION_ID)))
        return ZX_ERR_INVALID_ARGS;

    // Strip the NO_ACK flag from the request before selecting the dispatch target.
    auto cmd = static_cast<audio_proto::Cmd>(req.hdr.cmd & ~AUDIO_FLAG_NO_ACK);
    switch (cmd) {
    HREQ(AUDIO_RB_CMD_GET_FIFO_DEPTH, get_fifo_depth, OnGetFifoDepthLocked, false);
    HREQ(AUDIO_RB_CMD_GET_BUFFER,     get_buffer,     OnGetBufferLocked,    false);
    HREQ(AUDIO_RB_CMD_START,          rb_start,       OnStartLocked,        false);
    HREQ(AUDIO_RB_CMD_STOP,           rb_stop,        OnStopLocked,         false);
    default:
        LOG(TRACE, "Unrecognized ring buffer command 0x%04x\n", req.hdr.cmd);
        return ZX_ERR_NOT_SUPPORTED;
    }

    return ZX_ERR_NOT_SUPPORTED;
}
#undef HREQ

zx_status_t UsbAudioStream::OnGetStreamFormatsLocked(dispatcher::Channel* channel,
                                                     const audio_proto::StreamGetFmtsReq& req) {
    ZX_DEBUG_ASSERT(channel != nullptr);
    audio_proto::StreamGetFmtsResp resp = { };

    const auto& formats = ifc_->formats();
    if (formats.size() > fbl::numeric_limits<uint16_t>::max()) {
        LOG(ERROR, "Too many formats (%zu) to send during AUDIO_STREAM_CMD_GET_FORMATS request!\n",
            formats.size());
        return ZX_ERR_INTERNAL;
    }

    size_t formats_sent = 0;
    resp.hdr = req.hdr;
    resp.format_range_count = static_cast<uint16_t>(formats.size());

    do {
        size_t todo, payload_sz, __UNUSED to_send;
        zx_status_t res;

        todo = fbl::min<size_t>(formats.size() - formats_sent,
                                AUDIO_STREAM_CMD_GET_FORMATS_MAX_RANGES_PER_RESPONSE);
        payload_sz = sizeof(resp.format_ranges[0]) * todo;
        to_send = offsetof(audio_proto::StreamGetFmtsResp, format_ranges) + payload_sz;

        resp.first_format_range_ndx = static_cast<uint16_t>(formats_sent);
        for (uint32_t i = 0; i < todo; ++i) {
            resp.format_ranges[i] = formats[formats_sent + i].range_;
        }

        res = channel->Write(&resp, sizeof(resp));
        if (res != ZX_OK) {
            LOG(TRACE, "Failed to send get stream formats response (res %d)\n", res);
            return res;
        }

        formats_sent += todo;
    } while (formats_sent < formats.size());

    return ZX_OK;
}

zx_status_t UsbAudioStream::OnSetStreamFormatLocked(dispatcher::Channel* channel,
                                                    const audio_proto::StreamSetFmtReq& req,
                                                    bool privileged) {
    ZX_DEBUG_ASSERT(channel != nullptr);

    zx::channel client_rb_channel;
    audio_proto::StreamSetFmtResp resp = { };
    resp.hdr = req.hdr;

    // Only the privileged stream channel is allowed to change the format.
    if (!privileged) {
        ZX_DEBUG_ASSERT(channel != stream_channel_.get());
        resp.result = ZX_ERR_ACCESS_DENIED;
        goto finished;
    }

    // Look up the details about the interface and the endpoint which will be
    // used for the requested format.
    size_t format_ndx;
    resp.result = ifc_->LookupFormat(req.frames_per_second,
                                     req.channels,
                                     req.sample_format,
                                     &format_ndx);
    if (resp.result != ZX_OK) {
        goto finished;
    }

    // Determine the frame size needed for this requested format, then compute
    // the size of our short packets, and the constants used to generate the
    // short/long packet cadence.  For now, assume that we will be operating at
    // a 1mSec isochronous rate.
    //
    // Make sure that we can fit our longest payload length into one of our
    // usb requests.
    //
    // Store the results of all of these calculations in local variables.  Do
    // not commit them to member variables until we are certain that we are
    // going to go ahead with this format change.
    //
    // TODO(johngro) : Unless/until we can find some way to set the USB bus
    // driver to perform direct DMA to/from the Ring Buffer VMO without the need
    // for software intervention, we may want to expose ways to either increase
    // the isochronous interval (to minimize load) or to use USB 2.0 125uSec
    // sub-frame timing (to decrease latency) if possible.
    uint32_t frame_size;
    frame_size = audio::utils::ComputeFrameSize(req.channels, req.sample_format);
    if (!frame_size) {
        LOG(ERROR, "Failed to compute frame size (ch %hu fmt 0x%08x)\n",
            req.channels, req.sample_format);
        resp.result = ZX_ERR_INTERNAL;
        goto finished;
    }

    static constexpr uint32_t iso_packet_rate = 1000;
    uint32_t bytes_per_packet, fractional_bpp_inc, long_payload_len;
    bytes_per_packet   = (req.frames_per_second / iso_packet_rate) * frame_size;
    fractional_bpp_inc = (req.frames_per_second % iso_packet_rate);
    long_payload_len   = bytes_per_packet + (fractional_bpp_inc ? frame_size : 0);

    ZX_DEBUG_ASSERT(format_ndx < ifc_->formats().size());
    if (long_payload_len > ifc_->formats()[format_ndx].max_req_size_) {
        resp.result = ZX_ERR_INVALID_ARGS;
        goto finished;
    }

    // Deny the format change request if the ring buffer is not currently stopped.
    {
        // TODO(johngro) : If the ring buffer is running, should we automatically
        // stop it instead of returning bad state?
        fbl::AutoLock req_lock(&req_lock_);
        if (ring_buffer_state_ != RingBufferState::STOPPED) {
            resp.result = ZX_ERR_BAD_STATE;
            goto finished;
        }
    }

    // Looks like we are going ahead with this format change.  Tear down any
    // exiting ring buffer interface before proceeding.
    if (rb_channel_ != nullptr) {
        rb_channel_->Deactivate();
        rb_channel_.reset();
    }

    // Record the details of our cadence and format selection
    selected_format_ndx_ = format_ndx;
    selected_frame_rate_ = req.frames_per_second;
    frame_size_          = frame_size;
    iso_packet_rate_     = iso_packet_rate;
    bytes_per_packet_    = bytes_per_packet;
    fractional_bpp_inc_  = fractional_bpp_inc;

    // Compute the effective fifo depth for this stream.  Right now, we assume
    // that the controller will never get farther ahead than two isochronous usb
    // requests, so we report this as the worst case fifo_depth.
    //
    // Based on our cadence generation parameters, adjust this number based on
    // whether or not it is possible to have 0, 1 or 2 long packets back to back
    // at any point in time during the sequence.
    //
    // TODO(johngro): This is not the proper way to report the FIFO depth.  How
    // far ahead the USB controller will read ahead into its FIFO is going to be
    // a property of the controller and the properties of the endpoint.  It is
    // possible that this is negotiable to some extent as well.  I need to work
    // with voydanof@ to determine what we can expose from the USB bus driver in
    // order to report this accurately.
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

    // Create a new ring buffer channel which can be used to move bulk data and
    // bind it to us.
    rb_channel_ = dispatcher::Channel::Create();
    if (rb_channel_ == nullptr) {
        resp.result = ZX_ERR_NO_MEMORY;
    } else {
        dispatcher::Channel::ProcessHandler phandler(
        [stream = fbl::WrapRefPtr(this)](dispatcher::Channel* channel) -> zx_status_t {
            OBTAIN_EXECUTION_DOMAIN_TOKEN(t, stream->default_domain_);
            return stream->ProcessRingBufferChannel(channel);
        });

        dispatcher::Channel::ChannelClosedHandler chandler(
        [stream = fbl::WrapRefPtr(this)](const dispatcher::Channel* channel) -> void {
            OBTAIN_EXECUTION_DOMAIN_TOKEN(t, stream->default_domain_);
            stream->DeactivateRingBufferChannel(channel);
        });

        resp.result = rb_channel_->Activate(&client_rb_channel,
                                            default_domain_,
                                            fbl::move(phandler),
                                            fbl::move(chandler));
        if (resp.result != ZX_OK) {
            rb_channel_.reset();
        }
    }

finished:
    if (resp.result == ZX_OK) {
        // TODO(johngro): Report the actual external delay.
        resp.external_delay_nsec = 0;
        return channel->Write(&resp, sizeof(resp), fbl::move(client_rb_channel));
    } else {
        return channel->Write(&resp, sizeof(resp));
    }
}

zx_status_t UsbAudioStream::OnGetGainLocked(dispatcher::Channel* channel,
                                            const audio_proto::GetGainReq& req) {
    ZX_DEBUG_ASSERT(channel != nullptr);
    audio_proto::GetGainResp resp = { };
    resp.hdr = req.hdr;

    ZX_DEBUG_ASSERT(ifc_->path() != nullptr);
    const auto& path = *(ifc_->path());

    resp.can_mute  = path.has_mute();
    resp.cur_mute  = path.cur_mute();
    resp.can_agc   = path.has_agc();
    resp.cur_agc   = path.cur_agc();
    resp.cur_gain  = path.cur_gain();
    resp.min_gain  = path.min_gain();
    resp.max_gain  = path.max_gain();
    resp.gain_step = path.gain_res();

    return channel->Write(&resp, sizeof(resp));
}

zx_status_t UsbAudioStream::OnSetGainLocked(dispatcher::Channel* channel,
                                            const audio_proto::SetGainReq& req) {
    // TODO(johngro): Actually perform the set operation on our audio path.
    ZX_DEBUG_ASSERT(channel != nullptr);

    audio_proto::SetGainResp resp = { };
    resp.hdr = req.hdr;

    ZX_DEBUG_ASSERT(ifc_->path() != nullptr);
    auto& path = *(ifc_->path());
    bool req_mute = req.flags & AUDIO_SGF_MUTE;
    bool req_agc  = req.flags & AUDIO_SGF_AGC;
    bool illegal_mute = (req.flags & AUDIO_SGF_MUTE_VALID) && req_mute && !path.has_mute();
    bool illegal_agc  = (req.flags & AUDIO_SGF_AGC_VALID)  && req_agc  && !path.has_agc();
    bool illegal_gain = (req.flags & AUDIO_SGF_GAIN_VALID) && (req.gain != 0) && !path.has_gain();

    if (illegal_mute || illegal_agc || illegal_gain) {
        // If this request is illegal, make no changes but attempt to report the
        // current state of the world.
        resp.cur_mute = path.cur_mute();
        resp.cur_agc  = path.cur_agc();
        resp.cur_gain = path.cur_gain();
        resp.result = ZX_ERR_INVALID_ARGS;
    } else {
        if (req.flags & AUDIO_SGF_MUTE_VALID) {
            resp.cur_mute = path.SetMute(parent_.usb_proto(), req_mute);
        }

        if (req.flags & AUDIO_SGF_AGC_VALID) {
            resp.cur_agc = path.SetAgc(parent_.usb_proto(), req_agc);
        }

        if (req.flags & AUDIO_SGF_GAIN_VALID) {
            resp.cur_gain = path.SetGain(parent_.usb_proto(), req.gain);
        }

        resp.result = ZX_OK;
    }

    return (req.hdr.cmd & AUDIO_FLAG_NO_ACK) ? ZX_OK : channel->Write(&resp, sizeof(resp));
}

zx_status_t UsbAudioStream::OnPlugDetectLocked(dispatcher::Channel* channel,
                                               const audio_proto::PlugDetectReq& req) {
    if (req.hdr.cmd & AUDIO_FLAG_NO_ACK)
        return ZX_OK;

    audio_proto::PlugDetectResp resp = { };
    resp.hdr   = req.hdr;
    resp.flags = static_cast<audio_pd_notify_flags_t>(AUDIO_PDNF_HARDWIRED | AUDIO_PDNF_PLUGGED);
    resp.plug_state_time = create_time_;

    return channel->Write(&resp, sizeof(resp));
}

zx_status_t UsbAudioStream::OnGetUniqueIdLocked(dispatcher::Channel* channel,
                                                const audio_proto::GetUniqueIdReq& req) {
    audio_proto::GetUniqueIdResp resp;

    static_assert(sizeof(resp.unique_id) == sizeof(persistent_unique_id_),
                  "Unique ID sizes much match!");
    resp.hdr = req.hdr;
    resp.unique_id = persistent_unique_id_;

    return channel->Write(&resp, sizeof(resp));
}

zx_status_t UsbAudioStream::OnGetStringLocked(dispatcher::Channel* channel,
                                              const audio_proto::GetStringReq& req) {
    audio_proto::GetStringResp resp;
    const fbl::Array<uint8_t>* str;

    resp.hdr = req.hdr;
    resp.id = req.id;

    switch (req.id) {
        case AUDIO_STREAM_STR_ID_MANUFACTURER: str = &parent_.mfr_name(); break;
        case AUDIO_STREAM_STR_ID_PRODUCT:      str = &parent_.prod_name(); break;
        default:                               str = nullptr; break;
    }

    if (str == nullptr) {
        resp.result = ZX_ERR_NOT_FOUND;
        resp.strlen = 0;
    } else {
        size_t todo = fbl::min<size_t>(sizeof(resp.str), str->size());
        ZX_DEBUG_ASSERT(todo <= fbl::numeric_limits<uint32_t>::max());

        ::memset(resp.str, 0, sizeof(resp.str));
        if (todo) {
            ::memcpy(resp.str, str->get(), todo);
        }

        resp.result = ZX_OK;
        resp.strlen = static_cast<uint32_t>(todo);
    }

    return channel->Write(&resp, sizeof(resp));
}

zx_status_t UsbAudioStream::OnGetFifoDepthLocked(dispatcher::Channel* channel,
                                                 const audio_proto::RingBufGetFifoDepthReq& req) {
    audio_proto::RingBufGetFifoDepthResp resp = { };

    resp.hdr = req.hdr;
    resp.result = ZX_OK;
    resp.fifo_depth = fifo_bytes_;

    return channel->Write(&resp, sizeof(resp));
}

zx_status_t UsbAudioStream::OnGetBufferLocked(dispatcher::Channel* channel,
                                              const audio_proto::RingBufGetBufferReq& req) {
    audio_proto::RingBufGetBufferResp resp = { };
    zx::vmo client_rb_handle;
    uint32_t map_flags, client_rights;

    resp.hdr    = req.hdr;
    resp.result = ZX_ERR_INTERNAL;

    {
        // We cannot create a new ring buffer if we are not currently stopped.
        fbl::AutoLock req_lock(&req_lock_);
        if (ring_buffer_state_ != RingBufferState::STOPPED) {
            resp.result = ZX_ERR_BAD_STATE;
            goto finished;
        }
    }

    // Unmap and release any previous ring buffer.
    ReleaseRingBufferLocked();

    // Compute the ring buffer size.  It needs to be at least as big
    // as the virtual fifo depth.
    ZX_DEBUG_ASSERT(frame_size_ && ((fifo_bytes_ % frame_size_) == 0));
    ZX_DEBUG_ASSERT(fifo_bytes_ && ((fifo_bytes_ % fifo_bytes_) == 0));
    ring_buffer_size_  = req.min_ring_buffer_frames;
    ring_buffer_size_ *= frame_size_;
    if (ring_buffer_size_ < fifo_bytes_)
        ring_buffer_size_ = fbl::round_up(fifo_bytes_, frame_size_);

    // Set up our state for generating notifications.
    if (req.notifications_per_ring) {
        bytes_per_notification_ = ring_buffer_size_ / req.notifications_per_ring;
    } else {
        bytes_per_notification_ = 0;
    }

    // Create the ring buffer vmo we will use to share memory with the client.
    resp.result = zx::vmo::create(ring_buffer_size_, 0, &ring_buffer_vmo_);
    if (resp.result != ZX_OK) {
        LOG(ERROR, "Failed to create ring buffer (size %u, res %d)\n",
            ring_buffer_size_, resp.result);
        goto finished;
    }

    // Map the VMO into our address space.
    //
    // TODO(johngro): skip this step when APIs in the USB bus driver exist to
    // DMA directly from the VMO.
    map_flags = ZX_VM_FLAG_PERM_READ;
    if (is_input())
        map_flags |= ZX_VM_FLAG_PERM_WRITE;

    resp.result = zx::vmar::root_self().map(0, ring_buffer_vmo_,
                                            0, ring_buffer_size_,
                                            map_flags,
                                            reinterpret_cast<uintptr_t*>(&ring_buffer_virt_));
    if (resp.result != ZX_OK) {
        LOG(ERROR, "Failed to map ring buffer (size %u, res %d)\n",
            ring_buffer_size_, resp.result);
        goto finished;
    }

    // Create the client's handle to the ring buffer vmo and set it back to them.
    client_rights = ZX_RIGHT_TRANSFER | ZX_RIGHT_MAP | ZX_RIGHT_READ;
    if (!is_input())
        client_rights |= ZX_RIGHT_WRITE;

    resp.result = ring_buffer_vmo_.duplicate(client_rights, &client_rb_handle);
    if (resp.result != ZX_OK) {
        LOG(ERROR, "Failed to duplicate ring buffer handle (res %d)\n", resp.result);
        goto finished;
    }
    resp.num_ring_buffer_frames = ring_buffer_size_ / frame_size_;

finished:
    zx_status_t res;
    if (resp.result == ZX_OK) {
        ZX_DEBUG_ASSERT(client_rb_handle.is_valid());
        res = channel->Write(&resp, sizeof(resp), fbl::move(client_rb_handle));
    } else {
        res = channel->Write(&resp, sizeof(resp));
    }

    if (res != ZX_OK)
        ReleaseRingBufferLocked();

    return res;
}

zx_status_t UsbAudioStream::OnStartLocked(dispatcher::Channel* channel,
                                          const audio_proto::RingBufStartReq& req) {
    audio_proto::RingBufStartResp resp = { };
    resp.hdr = req.hdr;

    fbl::AutoLock req_lock(&req_lock_);

    if (ring_buffer_state_ != RingBufferState::STOPPED) {
        // The ring buffer is running, do not linger in the lock while we send
        // the error code back to the user.
        req_lock.release();
        resp.result = ZX_ERR_BAD_STATE;
        return channel->Write(&resp, sizeof(resp));
    }

    // We are idle, all of our usb requests should be sitting in the free list.
    ZX_DEBUG_ASSERT(allocated_req_cnt_ == free_req_cnt_);

    // Activate the format.
    resp.result = ifc_->ActivateFormat(selected_format_ndx_, selected_frame_rate_);
    if (resp.result != ZX_OK) {
        return channel->Write(&resp, sizeof(resp));
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
    resp.result = device_ioctl(parent_.parent(), IOCTL_USB_GET_CURRENT_FRAME,
                               NULL, 0,
                               &usb_frame_num_, sizeof(usb_frame_num_),
                               &read_amt);
    if ((resp.result != ZX_OK) || (read_amt != sizeof(usb_frame_num_))) {
        LOG(ERROR, "Failed to fetch USB frame number!  (res %d, amt %zu)\n", resp.result, read_amt);
        ifc_->ActivateIdleFormat();
        return channel->Write(&resp, sizeof(resp));
    }

    usb_frame_num_ += 2;

    // Flag ourselves as being in the starting state, then queue up all of our
    // transactions.
    ring_buffer_state_ = RingBufferState::STARTING;
    while (!list_is_empty(&free_req_))
        QueueRequestLocked();

    // Record the transaction ID we will send back to our client when we have
    // successfully started, then get out.
    pending_job_resp_.start = resp;
    return ZX_OK;
}

zx_status_t UsbAudioStream::OnStopLocked(dispatcher::Channel* channel,
                                         const audio_proto::RingBufStopReq& req) {
    fbl::AutoLock req_lock(&req_lock_);

    // TODO(johngro): Fix this to use the cancel transaction capabilities added
    // to the USB bus driver.
    //
    // Also, investigate whether or not the cancel interface is syncronous or
    // whether we will need to maintain an intermediate stopping state.
    if (ring_buffer_state_ != RingBufferState::STARTED) {
        audio_proto::RingBufStopResp resp = { };

        req_lock.release();
        resp.hdr = req.hdr;
        resp.result = ZX_ERR_BAD_STATE;

        return channel->Write(&resp, sizeof(resp));
    }

    ring_buffer_state_ = RingBufferState::STOPPING;
    pending_job_resp_.stop.hdr = req.hdr;

    return ZX_OK;
}

void UsbAudioStream::RequestComplete(usb_request_t* req) {
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

    uint64_t complete_time = zx_clock_get(ZX_CLOCK_MONOTONIC);
    Action when_finished = Action::NONE;

    // TODO(johngro) : See MG-940.  Eliminate this as soon as we have a more
    // official way of meeting real-time latency requirements.  Also, the fact
    // that this boosting gets done after the first transaction completes
    // degrades the quality of the startup time estimate (if the system is under
    // high load when the system starts up).  As a general issue, there are
    // better ways of refining this estimate than bumping the thread prio before
    // the first transaction gets queued.  Therefor, we just have a poor
    // estimate for now and will need to live with the consequences.
    if (!req_complete_prio_bumped_) {
        zx_thread_set_priority(24 /* HIGH_PRIORITY in LK */);
        req_complete_prio_bumped_ = true;
    }

    {
        fbl::AutoLock req_lock(&req_lock_);

        // Cache the status and length of this usb request.
        zx_status_t req_status = req->response.status;
        uint32_t req_length = static_cast<uint32_t>(req->header.length);

        // Complete the usb request.  This will return the transaction to the free
        // list and (in the case of an input stream) copy the payload to the
        // ring buffer, and update the ring buffer position.
        //
        // TODO(johngro): copying the payload out of the ring buffer is an
        // operation which goes away when we get to the zero copy world.
        CompleteRequestLocked(req);

        // Did the transaction fail because the device was unplugged?  If so,
        // enter the stopping state and close the connections to our clients.
        if (req_status == ZX_ERR_IO_NOT_PRESENT) {
            ring_buffer_state_ = RingBufferState::STOPPING_AFTER_UNPLUG;
        } else {
            // If we are supposed to be delivering notifications, check to see
            // if it is time to do so.
            if (bytes_per_notification_) {
                notification_acc_ += req_length;

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
            if (free_req_cnt_ == allocated_req_cnt_) {
                resp.stop = pending_job_resp_.stop;
                when_finished = Action::SIGNAL_STOPPED;
            }
            break;

        case RingBufferState::STOPPING_AFTER_UNPLUG:
            if (free_req_cnt_ == allocated_req_cnt_) {
                resp.stop = pending_job_resp_.stop;
                when_finished = Action::HANDLE_UNPLUG;
            }
            break;

        case RingBufferState::STARTING:
            resp.start = pending_job_resp_.start;
            when_finished = Action::SIGNAL_STARTED;
            break;

        case RingBufferState::STARTED:
            QueueRequestLocked();
            break;

        case RingBufferState::STOPPED:
        default:
            LOG(ERROR, "Invalid state (%u) in %s\n",
                static_cast<uint32_t>(ring_buffer_state_), __PRETTY_FUNCTION__);
            ZX_DEBUG_ASSERT(false);
            break;
        }
    }

    if (when_finished != Action::NONE) {
        fbl::AutoLock lock(&lock_);
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
                resp.start.result = ZX_OK;
                resp.start.start_time = complete_time - ZX_MSEC(1);
                rb_channel_->Write(&resp.start, sizeof(resp.start));
            }
            {
                fbl::AutoLock req_lock(&req_lock_);
                ring_buffer_state_ = RingBufferState::STARTED;
            }
            break;

        case Action::HANDLE_UNPLUG:
            if (rb_channel_ != nullptr) {
                rb_channel_->Deactivate();
                rb_channel_.reset();
            }

            if (stream_channel_ != nullptr) {
                stream_channel_->Deactivate();
                stream_channel_.reset();
            }

            {
                fbl::AutoLock req_lock(&req_lock_);
                ring_buffer_state_ = RingBufferState::STOPPED;
            }
            break;

        case Action::SIGNAL_STOPPED:
            if (rb_channel_ != nullptr) {
                resp.stop.result = ZX_OK;
                rb_channel_->Write(&resp.stop, sizeof(resp.stop));
            }
            {
                fbl::AutoLock req_lock(&req_lock_);
                ring_buffer_state_ = RingBufferState::STOPPED;
                ifc_->ActivateIdleFormat();
            }
            break;

        case Action::NOTIFY_POSITION:
            resp.notify_pos.hdr.cmd = AUDIO_RB_POSITION_NOTIFY;
            resp.notify_pos.hdr.transaction_id = AUDIO_INVALID_TRANSACTION_ID;
            rb_channel_->Write(&resp.notify_pos, sizeof(resp.notify_pos));
            break;

        default:
            ZX_DEBUG_ASSERT(false);
            break;
        }
    }
}

void UsbAudioStream::QueueRequestLocked() {
    ZX_DEBUG_ASSERT((ring_buffer_state_ == RingBufferState::STARTING) ||
                    (ring_buffer_state_ == RingBufferState::STARTED));
    ZX_DEBUG_ASSERT(!list_is_empty(&free_req_));

    // Figure out how much we want to send or receive this time (short or long
    // packet)
    uint32_t todo = bytes_per_packet_;
    fractional_bpp_acc_ += fractional_bpp_inc_;
    if (fractional_bpp_acc_ >= iso_packet_rate_) {
        fractional_bpp_acc_ -= iso_packet_rate_;
        todo += frame_size_;
        ZX_DEBUG_ASSERT(fractional_bpp_acc_ < iso_packet_rate_);
    }

    // Grab a free usb request.
    auto req = list_remove_head_type(&free_req_, usb_request_t, node);
    ZX_DEBUG_ASSERT(req != nullptr);
    ZX_DEBUG_ASSERT(free_req_cnt_ > 0);
    --free_req_cnt_;

    // If this is an output stream, copy our data into the usb request.
    // TODO(johngro): eliminate this when we can get to a zero-copy world.
    if (!is_input()) {
        uint32_t avail = ring_buffer_size_ - ring_buffer_offset_;
        ZX_DEBUG_ASSERT(ring_buffer_offset_ < ring_buffer_size_);
        ZX_DEBUG_ASSERT((avail % frame_size_) == 0);
        uint32_t amt = fbl::min(avail, todo);

        const uint8_t* src = reinterpret_cast<uint8_t*>(ring_buffer_virt_) + ring_buffer_offset_;
        usb_request_copyto(req, src, amt, 0);
        if (amt == avail) {
            ring_buffer_offset_ = todo - amt;
            if (ring_buffer_offset_ > 0) {
                usb_request_copyto(req, ring_buffer_virt_, ring_buffer_offset_, amt);
            }
        } else {
            ring_buffer_offset_ += amt;
        }
    }

    req->header.frame = usb_frame_num_++;
    req->header.length = todo;
    usb_request_queue(&parent_.usb_proto(), req);
}

void UsbAudioStream::CompleteRequestLocked(usb_request_t* req) {
    ZX_DEBUG_ASSERT(req);

    // If we are an input stream, copy the payload into the ring buffer.
    if (is_input()) {
        uint32_t todo = static_cast<uint32_t>(req->header.length);

        uint32_t avail = ring_buffer_size_ - ring_buffer_offset_;
        ZX_DEBUG_ASSERT(ring_buffer_offset_ < ring_buffer_size_);
        ZX_DEBUG_ASSERT((avail % frame_size_) == 0);

        uint32_t amt = fbl::min(avail, todo);
        uint8_t* dst = reinterpret_cast<uint8_t*>(ring_buffer_virt_) + ring_buffer_offset_;

        if (req->response.status == ZX_OK) {
            usb_request_copyfrom(req, dst, amt, 0);
            if (amt < todo) {
                usb_request_copyfrom(req, ring_buffer_virt_, todo - amt, amt);
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
    ring_buffer_pos_ += static_cast<uint32_t>(req->header.length);
    if (ring_buffer_pos_ >= ring_buffer_size_) {
        ring_buffer_pos_ -= ring_buffer_size_;
        ZX_DEBUG_ASSERT(ring_buffer_pos_ < ring_buffer_size_);
    }

    // If this is an input stream, the ring buffer offset should always be equal
    // to the stream position.
    if (is_input()) {
        ring_buffer_offset_ = ring_buffer_pos_;
    }

    // Return the transaction to the free list.
    list_add_head(&free_req_, &req->node);
    ++free_req_cnt_;
    ZX_DEBUG_ASSERT(free_req_cnt_ <= allocated_req_cnt_);
}

void UsbAudioStream::DeactivateStreamChannel(const dispatcher::Channel* channel) {
    fbl::AutoLock lock(&lock_);

    ZX_DEBUG_ASSERT(stream_channel_.get() == channel);
    ZX_DEBUG_ASSERT(rb_channel_.get() != channel);
    stream_channel_.reset();
}

void UsbAudioStream::DeactivateRingBufferChannel(const dispatcher::Channel* channel) {
    fbl::AutoLock lock(&lock_);

    ZX_DEBUG_ASSERT(stream_channel_.get() != channel);
    ZX_DEBUG_ASSERT(rb_channel_.get() == channel);

    {
        fbl::AutoLock req_lock(&req_lock_);
        if (ring_buffer_state_ != RingBufferState::STOPPED) {
            ring_buffer_state_ = RingBufferState::STOPPING;
        }
    }

    rb_channel_.reset();
}

}  // namespace usb
}  // namespace audio
