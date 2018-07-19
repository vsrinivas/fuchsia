// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <zircon/assert.h>

#include "intel-hda-dsp.h"
#include "intel-hda-controller.h"
#include "utils.h"

namespace audio {
namespace intel_hda {

#define DEV  (static_cast<IntelHDADSP*>(ctx))
zx_protocol_device_t IntelHDADSP::DSP_DEVICE_THUNKS = {
    .version      = DEVICE_OPS_VERSION,
    .get_protocol = [](void* ctx,
                       uint32_t proto_id,
                       void* protocol) -> zx_status_t
                    {
                        return DEV->DeviceGetProtocol(proto_id, protocol);
                    },
    .open         = nullptr,
    .open_at      = nullptr,
    .close        = nullptr,
    .unbind       = [](void* ctx)
                    {
                        DEV->DeviceUnbind();
                    },
    .release      = nullptr,
    .read         = nullptr,
    .write        = nullptr,
    .get_size     = nullptr,
    .ioctl        = [](void* ctx,
                       uint32_t op,
                       const void* in_buf,
                       size_t in_len,
                       void* out_buf,
                       size_t out_len,
                       size_t* out_actual) -> zx_status_t
                    {
                        return DEV->DeviceIoctl(op, out_buf, out_len, out_actual);
                    },
    .suspend      = nullptr,
    .resume       = nullptr,
    .rxrpc        = nullptr,
    .message      = nullptr,
};

ihda_dsp_protocol_ops_t IntelHDADSP::DSP_PROTO_THUNKS = {
    .get_dev_info = [](void* ctx, zx_pcie_device_info_t* out_info)
    {
        ZX_DEBUG_ASSERT(ctx);
        return DEV->GetDevInfo(out_info);
    },
    .get_mmio = [](void* ctx, zx_handle_t* out_vmo, size_t* out_size) -> zx_status_t
    {
        ZX_DEBUG_ASSERT(ctx);
        return DEV->GetMmio(out_vmo, out_size);
    },
    .get_bti = [](void* ctx, zx_handle_t* out_handle) -> zx_status_t
    {
        ZX_DEBUG_ASSERT(ctx);
        return DEV->GetBti(out_handle);
    },
    .enable = [](void* ctx)
    {
        ZX_DEBUG_ASSERT(ctx);
        DEV->Enable();
    },
    .disable = [](void* ctx)
    {
        ZX_DEBUG_ASSERT(ctx);
        DEV->Disable();
    },
    .irq_enable = [](void* ctx, ihda_dsp_irq_callback_t* callback, void* cookie) -> zx_status_t
    {
        ZX_DEBUG_ASSERT(ctx);
        return DEV->IrqEnable(callback, cookie);
    },
    .irq_disable = [](void* ctx)
    {
        ZX_DEBUG_ASSERT(ctx);
        return DEV->IrqDisable();
    }
};

ihda_codec_protocol_ops_t IntelHDADSP::CODEC_PROTO_THUNKS = {
    .get_driver_channel = [](void* ctx, zx_handle_t* channel_out) -> zx_status_t
    {
        ZX_DEBUG_ASSERT(ctx);
        return DEV->CodecGetDispatcherChannel(channel_out);
    },
};
#undef DEV

IntelHDADSP::IntelHDADSP(IntelHDAController& controller,
                         hda_pp_registers_t* pp_regs,
                         const fbl::RefPtr<RefCountedBti>& pci_bti)
    : controller_(controller),
      pp_regs_(pp_regs),
      pci_bti_(pci_bti) {
    default_domain_ = dispatcher::ExecutionDomain::Create();

    const auto& info = controller_.dev_info();
    snprintf(log_prefix_, sizeof(log_prefix_),
             "IHDA DSP %02x:%02x.%01x",
             info.bus_id, info.dev_id, info.func_id);
}

fbl::RefPtr<IntelHDADSP> IntelHDADSP::Create(IntelHDAController& controller,
                                             hda_pp_registers_t* pp_regs,
                                             const fbl::RefPtr<RefCountedBti>& pci_bti) {
    fbl::AllocChecker ac;
    auto ret = fbl::AdoptRef(new (&ac) IntelHDADSP(controller, pp_regs, pci_bti));
    if (!ac.check()) {
        GLOBAL_LOG(ERROR, "Out of memory attempting to allocate DSP\n");
        return nullptr;
    }

    if (ret->default_domain_ == nullptr) {
        GLOBAL_LOG(ERROR, "Out of memory attempting to allocate execution domain\n");
        return nullptr;
    }

    zx_status_t res = ret->PublishDevice();
    if (res != ZX_OK) {
        GLOBAL_LOG(ERROR, "Failed to publish DSP device (res %d)\n", res);
        return nullptr;
    }

    return ret;
}

zx_status_t IntelHDADSP::PublishDevice() {
    char dev_name[ZX_DEVICE_NAME_MAX] = { 0 };
    snprintf(dev_name, sizeof(dev_name), "intel-sst-dsp-%03u", controller_.id());

    zx_device_prop_t props[3];
    props[0].id = BIND_PROTOCOL;
    props[0].value = ZX_PROTOCOL_IHDA_DSP;
    props[1].id = BIND_PCI_VID;
    props[1].value = controller_.dev_info().vendor_id;
    props[2].id = BIND_PCI_DID;
    props[2].value = controller_.dev_info().device_id;

    device_add_args_t args = {};
    args.version = DEVICE_ADD_ARGS_VERSION;
    args.name = dev_name;
    args.ctx = this;
    args.ops = &DSP_DEVICE_THUNKS;
    args.proto_id = ZX_PROTOCOL_IHDA_DSP;
    args.props = props;
    args.prop_count = countof(props);

    return device_add(controller_.dev_node(), &args, &dev_node_);
}

void IntelHDADSP::ProcessIRQ() {
    fbl::AutoLock dsp_lock(&dsp_lock_);
    if (pp_regs() == nullptr) {
        return;
    }
    if (irq_callback_ == nullptr) {
        return;
    }
    ZX_DEBUG_ASSERT(irq_cookie_ != nullptr);
    ZX_DEBUG_ASSERT(pp_regs() != nullptr);
    uint32_t ppsts = REG_RD(&pp_regs()->ppsts);
    if (!(ppsts & HDA_PPSTS_PIS)) {
        return;
    }
    irq_callback_(irq_cookie_);
}

zx_status_t IntelHDADSP::DeviceGetProtocol(uint32_t proto_id, void* protocol) {
    switch (proto_id) {
    case ZX_PROTOCOL_IHDA_CODEC: {
        auto proto = static_cast<ihda_codec_protocol_t*>(protocol);
        proto->ops = &CODEC_PROTO_THUNKS;
        proto->ctx = this;
        return ZX_OK;
    }
    case ZX_PROTOCOL_IHDA_DSP: {
        auto proto = static_cast<ihda_dsp_protocol_t*>(protocol);
        proto->ops = &DSP_PROTO_THUNKS;
        proto->ctx = this;
        return ZX_OK;
    }
    default:
        LOG(ERROR, "Unsupported protocol 0x%08x\n", proto_id);
        return ZX_ERR_NOT_SUPPORTED;
    }
}

zx_status_t IntelHDADSP::DeviceIoctl(uint32_t op,
                                     void*    out_buf,
                                     size_t   out_len,
                                     size_t*  out_actual) {
    dispatcher::Channel::ProcessHandler phandler(
    [dsp = fbl::WrapRefPtr(this)](dispatcher::Channel* channel) -> zx_status_t {
        OBTAIN_EXECUTION_DOMAIN_TOKEN(t, dsp->default_domain_);
        return dsp->ProcessClientRequest(channel, false);
    });

    return HandleDeviceIoctl(op, out_buf, out_len, out_actual,
                             default_domain_,
                             fbl::move(phandler),
                             nullptr);
}

void IntelHDADSP::DeviceUnbind() {
    // Close all existing connections and synchronize with any client threads
    // who are currently processing requests.
    default_domain_->Deactivate();

    // Give any active streams we had back to our controller.
    IntelHDAStream::Tree streams;
    {
        fbl::AutoLock lock(&active_streams_lock_);
        streams.swap(active_streams_);
    }

    while (!streams.is_empty()) {
        controller_.ReturnStream(streams.pop_front());
    }
}

void IntelHDADSP::GetDevInfo(zx_pcie_device_info_t* out_info) {
    if (!out_info) {
        return;
    }
    memcpy(out_info, &controller_.dev_info(), sizeof(*out_info));
}

zx_status_t IntelHDADSP::GetMmio(zx_handle_t* out_vmo, size_t* out_size) {
    // Fetch the BAR which the Audio DSP registers (BAR 4), then sanity check the type
    // and size.
    zx_pci_bar_t bar_info;
    zx_status_t res = pci_get_bar(controller_.pci(), 4u, &bar_info);
    if (res != ZX_OK) {
        LOG(ERROR, "Error attempting to fetch registers from PCI (res %d)\n", res);
        return res;
    }

    if (bar_info.type != PCI_BAR_TYPE_MMIO) {
        LOG(ERROR, "Bad register window type (expected %u got %u)\n",
                PCI_BAR_TYPE_MMIO, bar_info.type);
        return ZX_ERR_INTERNAL;
    }

    *out_vmo = bar_info.handle;
    *out_size = bar_info.size;
    return ZX_OK;
}

zx_status_t IntelHDADSP::GetBti(zx_handle_t* out_handle) {
    ZX_DEBUG_ASSERT(pci_bti_ != nullptr);
    zx::bti bti;
    zx_status_t res = pci_bti_->initiator().duplicate(ZX_RIGHT_SAME_RIGHTS, &bti);
    if (res != ZX_OK) {
        LOG(ERROR, "Error duplicating BTI for DSP (res %d)\n", res);
        return res;
    }
    *out_handle = bti.release();
    return ZX_OK;
}

void IntelHDADSP::Enable() {
    // Note: The GPROCEN bit does not really enable or disable the Audio DSP
    // operation, but mainly to work around some legacy Intel HD Audio driver
    // software such that if GPROCEN = 0, ADSPxBA (BAR2) is mapped to the Intel
    // HD Audio memory mapped configuration registers, for compliancy with some
    // legacy SW implementation. If GPROCEN = 1, only then ADSPxBA (BAR2) is
    // mapped to the actual Audio DSP memory mapped configuration registers.
    REG_SET_BITS<uint32_t>(&pp_regs()->ppctl, HDA_PPCTL_GPROCEN);
}

void IntelHDADSP::Disable() {
    REG_WR(&pp_regs()->ppctl, 0u);
}

zx_status_t IntelHDADSP::IrqEnable(ihda_dsp_irq_callback_t* callback, void* cookie) {
    fbl::AutoLock dsp_lock(&dsp_lock_);
    if (irq_callback_ != nullptr) {
        return ZX_ERR_ALREADY_EXISTS;
    }
    ZX_DEBUG_ASSERT(irq_cookie_ == nullptr);

    irq_callback_ = callback;
    irq_cookie_ = cookie;

    REG_SET_BITS<uint32_t>(&pp_regs()->ppctl, HDA_PPCTL_PIE);

    return ZX_OK;
}

void IntelHDADSP::IrqDisable() {
    fbl::AutoLock dsp_lock(&dsp_lock_);
    REG_CLR_BITS<uint32_t>(&pp_regs()->ppctl, HDA_PPCTL_PIE);
    irq_callback_ = nullptr;
    irq_cookie_ = nullptr;
}

zx_status_t IntelHDADSP::CodecGetDispatcherChannel(zx_handle_t* remote_endpoint_out) {
    if (!remote_endpoint_out)
        return ZX_ERR_INVALID_ARGS;

    dispatcher::Channel::ProcessHandler phandler(
    [codec = fbl::WrapRefPtr(this)](dispatcher::Channel* channel) -> zx_status_t {
        OBTAIN_EXECUTION_DOMAIN_TOKEN(t, codec->default_domain_);
        return codec->ProcessClientRequest(channel, true);
    });

    dispatcher::Channel::ChannelClosedHandler chandler(
    [codec = fbl::WrapRefPtr(this)](const dispatcher::Channel* channel) -> void {
        OBTAIN_EXECUTION_DOMAIN_TOKEN(t, codec->default_domain_);
        codec->ProcessClientDeactivate(channel);
    });

    // Enter the driver channel lock.  If we have already connected to a codec
    // driver, simply fail the request.  Otherwise, attempt to build a driver channel
    // and activate it.
    fbl::AutoLock lock(&codec_driver_channel_lock_);

    if (codec_driver_channel_ != nullptr)
        return ZX_ERR_BAD_STATE;

    zx::channel client_channel;
    zx_status_t res;
    res = CreateAndActivateChannel(default_domain_,
                                   fbl::move(phandler),
                                   fbl::move(chandler),
                                   &codec_driver_channel_,
                                   &client_channel);
    if (res == ZX_OK) {
        // If things went well, release the reference to the remote endpoint
        // from the zx::channel instance into the unmanaged world of DDK
        // protocols.
        *remote_endpoint_out = client_channel.release();
    }

    return res;
}

#define PROCESS_CMD(_req_ack, _req_driver_chan, _ioctl, _payload, _handler) \
case _ioctl:                                                                \
    if (req_size != sizeof(req._payload)) {                                 \
        LOG(TRACE, "Bad " #_payload                                         \
                   " request length (%u != %zu)\n",                         \
                   req_size, sizeof(req._payload));                         \
        return ZX_ERR_INVALID_ARGS;                                         \
    }                                                                       \
    if (_req_ack && (req.hdr.cmd & IHDA_NOACK_FLAG))  {                     \
        LOG(TRACE, "Cmd " #_payload                                         \
                   " requires acknowledgement, but the "                    \
                   "NOACK flag was set!\n");                                \
        return ZX_ERR_INVALID_ARGS;                                         \
    }                                                                       \
    if (_req_driver_chan  && !is_driver_channel) {                          \
        LOG(TRACE, "Cmd " #_payload                                         \
                   " requires a privileged driver channel.\n");             \
        return ZX_ERR_ACCESS_DENIED;                                        \
    }                                                                       \
    return _handler(channel, req._payload)
zx_status_t IntelHDADSP::ProcessClientRequest(dispatcher::Channel* channel,
                                                bool is_driver_channel) {
    zx_status_t res;
    uint32_t req_size;
    union {
        ihda_proto::CmdHdr           hdr;
        ihda_proto::RequestStreamReq request_stream;
        ihda_proto::ReleaseStreamReq release_stream;
        ihda_proto::SetStreamFmtReq  set_stream_fmt;
    } req;
    // TODO(johngro) : How large is too large?
    static_assert(sizeof(req) <= 256, "Request buffer is too large to hold on the stack!");

    // Read the client request.
    ZX_DEBUG_ASSERT(channel != nullptr);
    res = channel->Read(&req, sizeof(req), &req_size);
    if (res != ZX_OK) {
        LOG(TRACE, "Failed to read client request (res %d)\n", res);
        return res;
    }

    // Sanity checks.
    if (req_size < sizeof(req.hdr)) {
        LOG(TRACE,"Client request too small to contain header (%u < %zu)\n",
                req_size, sizeof(req.hdr));
        return ZX_ERR_INVALID_ARGS;
    }

    auto cmd_id = static_cast<ihda_cmd_t>(req.hdr.cmd & ~IHDA_NOACK_FLAG);
    if (req.hdr.transaction_id == IHDA_INVALID_TRANSACTION_ID) {
        LOG(TRACE, "Invalid transaction ID in client request 0x%04x\n", cmd_id);
        return ZX_ERR_INVALID_ARGS;
    }

    // Dispatch
    LOG(SPEW, "Client Request (cmd 0x%04x tid %u) len %u\n",
            req.hdr.cmd,
            req.hdr.transaction_id,
            req_size);

    switch (cmd_id) {
    PROCESS_CMD(true,  true,  IHDA_CODEC_REQUEST_STREAM,    request_stream, ProcessRequestStream);
    PROCESS_CMD(false, true,  IHDA_CODEC_RELEASE_STREAM,    release_stream, ProcessReleaseStream);
    PROCESS_CMD(false, true,  IHDA_CODEC_SET_STREAM_FORMAT, set_stream_fmt, ProcessSetStreamFmt);
    default:
        LOG(TRACE, "Unrecognized command ID 0x%04x\n", req.hdr.cmd);
        return ZX_ERR_INVALID_ARGS;
    }
}

#undef PROCESS_CMD

void IntelHDADSP::ProcessClientDeactivate(const dispatcher::Channel* channel) {
    ZX_DEBUG_ASSERT(channel != nullptr);

    // This should be the driver channel (client channels created with IOCTL do
    // not register a deactivate handler).  Start by releasing the internal
    // channel reference from within the codec_driver_channel_lock.
    {
        fbl::AutoLock lock(&codec_driver_channel_lock_);
        ZX_DEBUG_ASSERT(channel == codec_driver_channel_.get());
        codec_driver_channel_.reset();
    }

    // Return any DMA streams the codec driver had owned back to the controller.
    IntelHDAStream::Tree tmp;
    {
        fbl::AutoLock lock(&active_streams_lock_);
        tmp = fbl::move(active_streams_);
    }

    while (!tmp.is_empty()) {
        auto stream = tmp.pop_front();
        stream->Deactivate();
        controller_.ReturnStream(fbl::move(stream));
    }
}

zx_status_t IntelHDADSP::ProcessRequestStream(dispatcher::Channel* channel,
                                                const ihda_proto::RequestStreamReq& req) {
    ZX_DEBUG_ASSERT(channel != nullptr);

    ihda_proto::RequestStreamResp resp;
    resp.hdr = req.hdr;

    // Attempt to get a stream of the proper type.
    auto type = req.input
              ? IntelHDAStream::Type::INPUT
              : IntelHDAStream::Type::OUTPUT;
    auto stream = controller_.AllocateStream(type);

    if (stream != nullptr) {
        LOG(TRACE, "Decouple stream #%u\n", stream->id());
        // Decouple stream
        REG_SET_BITS<uint32_t>(&pp_regs()->ppctl, (1 << stream->dma_id()));

        // Success, send its ID and its tag back to the codec and add it to the
        // set of active streams owned by this codec.
        resp.result     = ZX_OK;
        resp.stream_id  = stream->id();
        resp.stream_tag = stream->tag();

        fbl::AutoLock lock(&active_streams_lock_);
        active_streams_.insert(fbl::move(stream));
    } else {
        // Failure; tell the codec that we are out of streams.
        resp.result     = ZX_ERR_NO_MEMORY;
        resp.stream_id  = 0;
        resp.stream_tag = 0;
    }

    return channel->Write(&resp, sizeof(resp));
}

zx_status_t IntelHDADSP::ProcessReleaseStream(dispatcher::Channel* channel,
                                                const ihda_proto::ReleaseStreamReq& req) {
    ZX_DEBUG_ASSERT(channel != nullptr);

    // Remove the stream from the active set.
    fbl::RefPtr<IntelHDAStream> stream;
    {
        fbl::AutoLock lock(&active_streams_lock_);
        stream = active_streams_.erase(req.stream_id);
    }

    // If the stream was not active, our codec driver is crazy.  Hang up the
    // phone on it.
    if (stream == nullptr)
        return ZX_ERR_BAD_STATE;

    LOG(TRACE, "Couple stream #%u\n", stream->id());

    // Couple stream
    REG_CLR_BITS<uint32_t>(&pp_regs()->ppctl, (1 << stream->dma_id()));

    // Give the stream back to the controller and (if an ack was requested) tell
    // our codec driver that things went well.
    stream->Deactivate();
    controller_.ReturnStream(fbl::move(stream));

    if (req.hdr.cmd & IHDA_NOACK_FLAG)
        return ZX_OK;

    ihda_proto::RequestStreamResp resp;
    resp.hdr = req.hdr;
    return channel->Write(&resp, sizeof(resp));
}

zx_status_t IntelHDADSP::ProcessSetStreamFmt(dispatcher::Channel* channel,
                                               const ihda_proto::SetStreamFmtReq& req) {
    ZX_DEBUG_ASSERT(channel != nullptr);

    // Sanity check the requested format.
    if (!StreamFormat(req.format).SanityCheck()) {
        LOG(TRACE, "Invalid encoded stream format 0x%04hx!\n", req.format);
        return ZX_ERR_INVALID_ARGS;
    }

    // Grab a reference to the stream from the active set.
    fbl::RefPtr<IntelHDAStream> stream;
    {
        fbl::AutoLock lock(&active_streams_lock_);
        auto iter = active_streams_.find(req.stream_id);
        if (iter.IsValid())
            stream = iter.CopyPointer();
    }

    // If the stream was not active, our codec driver is crazy.  Hang up the
    // phone on it.
    if (stream == nullptr)
        return ZX_ERR_BAD_STATE;

    // Set the stream format and assign the client channel to the stream.  If
    // this stream is already bound to a client, this will cause that connection
    // to be closed.
    zx::channel client_channel;
    zx_status_t res = stream->SetStreamFormat(default_domain_,
                                              req.format,
                                              &client_channel);
    if (res != ZX_OK) {
        LOG(TRACE, "Failed to set stream format 0x%04hx for stream %hu (res %d)\n",
                  req.format, req.stream_id, res);
        return res;
    }

    // Send the channel back to the codec driver.
    ZX_DEBUG_ASSERT(client_channel.is_valid());
    ihda_proto::SetStreamFmtResp resp;
    resp.hdr = req.hdr;
    res = channel->Write(&resp, sizeof(resp), fbl::move(client_channel));

    if (res != ZX_OK)
        LOG(TRACE, "Failed to send stream channel back to codec driver (res %d)\n", res);

    return res;
}

}  // namespace intel_hda
}  // namespace audio
