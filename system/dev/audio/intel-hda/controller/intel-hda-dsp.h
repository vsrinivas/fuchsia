// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <ddk/protocol/intel-hda-codec.h>
#include <ddk/protocol/intel-hda-dsp.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <zircon/thread_annotations.h>
#include <stdint.h>
#include <string.h>

#include <dispatcher-pool/dispatcher-channel.h>
#include <dispatcher-pool/dispatcher-execution-domain.h>
#include <intel-hda/utils/intel-hda-proto.h>
#include <intel-hda/utils/intel-hda-registers.h>

#include "debug-logging.h"
#include "intel-hda-stream.h"

namespace audio {
namespace intel_hda {

class IntelHDAController;

class IntelHDADSP : public fbl::RefCounted<IntelHDADSP> {
public:
    static fbl::RefPtr<IntelHDADSP> Create(IntelHDAController& controller,
                                           hda_pp_registers_t* pp_regs,
                                           const fbl::RefPtr<RefCountedBti>& pci_bti);

    const char* log_prefix() const { return log_prefix_; }

    void ProcessIRQ();

private:
    friend class fbl::RefPtr<IntelHDADSP>;

    static zx_protocol_device_t DSP_DEVICE_THUNKS;
    static ihda_codec_protocol_ops_t CODEC_PROTO_THUNKS;
    static ihda_dsp_protocol_ops_t DSP_PROTO_THUNKS;

    IntelHDADSP(IntelHDAController& controller,
                hda_pp_registers_t* pp_regs,
                const fbl::RefPtr<RefCountedBti>& pci_bti);
    ~IntelHDADSP() { };

    hda_pp_registers_t* pp_regs() const {
        return pp_regs_;
    }

    zx_status_t PublishDevice();

    // Device interface
    zx_status_t DeviceGetProtocol(uint32_t proto_id, void* protocol);
    zx_status_t DeviceIoctl(uint32_t op, void* out_buf, size_t out_len, size_t* out_actual);
    void DeviceUnbind();

    // ZX_PROTOCOL_IHDA_DSP interface
    void GetDevInfo(zx_pcie_device_info_t* out_info);
    zx_status_t GetMmio(zx_handle_t* out_vmo, size_t* out_size);
    zx_status_t GetBti(zx_handle_t* out_handle);
    void Enable();
    void Disable();
    zx_status_t IrqEnable(ihda_dsp_irq_callback_t* callback, void* cookie);
    void IrqDisable();

    // ZX_PROTOCOL_IHDA_CODEC Interface
    zx_status_t CodecGetDispatcherChannel(zx_handle_t* channel_out);

    // Thunks for interacting with clients and codec drivers.
    zx_status_t ProcessClientRequest(dispatcher::Channel* channel, bool is_driver_channel);
    void ProcessClientDeactivate(const dispatcher::Channel* channel);
    zx_status_t ProcessRequestStream(dispatcher::Channel* channel,
                                     const ihda_proto::RequestStreamReq& req);
    zx_status_t ProcessReleaseStream(dispatcher::Channel* channel,
                                     const ihda_proto::ReleaseStreamReq& req);
    zx_status_t ProcessSetStreamFmt(dispatcher::Channel* channel,
                                    const ihda_proto::SetStreamFmtReq& req);

    // Reference to our owner.
    IntelHDAController& controller_;

    fbl::Mutex dsp_lock_;
    ihda_dsp_irq_callback_t* irq_callback_ TA_GUARDED(dsp_lock_) = nullptr;
    void* irq_cookie_ TA_GUARDED(dsp_lock_) = nullptr;

    // Driver connection state
    fbl::Mutex codec_driver_channel_lock_;
    fbl::RefPtr<dispatcher::Channel> codec_driver_channel_ TA_GUARDED(codec_driver_channel_lock_);

    // Log prefix storage
    char log_prefix_[LOG_PREFIX_STORAGE] = { 0 };

    // Published device node.
    zx_device_t* dev_node_ = nullptr;

    // Pipe processintg registers
    hda_pp_registers_t* pp_regs_ = nullptr;

    // A handle to the Bus Transaction Initiator for the controller.
    fbl::RefPtr<RefCountedBti> pci_bti_;

    // Dispatcher framework state.
    fbl::RefPtr<dispatcher::ExecutionDomain> default_domain_;

    // Active DMA streams
    fbl::Mutex          active_streams_lock_;
    IntelHDAStream::Tree active_streams_ TA_GUARDED(active_streams_lock_);
};

}  // namespace intel_hda
}  // namespace audio
