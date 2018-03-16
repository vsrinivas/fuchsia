// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/protocol/intel-hda-codec.h>
#include <lib/zx/handle.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <stdint.h>
#include <string.h>

#include <dispatcher-pool/dispatcher-channel.h>
#include <dispatcher-pool/dispatcher-execution-domain.h>
#include <intel-hda/utils/codec-commands.h>
#include <intel-hda/utils/intel-hda-proto.h>
#include <intel-hda/utils/intel-hda-registers.h>

#include "codec-cmd-job.h"
#include "debug-logging.h"
#include "intel-hda-stream.h"
#include "thread-annotations.h"

namespace audio {
namespace intel_hda {

class IntelHDAController;
struct CodecResponse;

class IntelHDACodec : public fbl::RefCounted<IntelHDACodec> {
public:
    enum class State {
        PROBING,
        FINDING_DRIVER,
        OPERATING,
        SHUTTING_DOWN,
        SHUT_DOWN,
        FATAL_ERROR,
    };

    static fbl::RefPtr<IntelHDACodec> Create(IntelHDAController& controller, uint8_t codec_id);

    zx_status_t Startup();
    void ProcessSolicitedResponse(const CodecResponse& resp, fbl::unique_ptr<CodecCmdJob>&& job);
    void ProcessUnsolicitedResponse(const CodecResponse& resp);
    void ProcessWakeupEvt();

    // TODO (johngro) : figure out shutdown... Currently, this is called from
    // the controller's irq thread and expected to execute synchronously, which
    // does not allow codec drivers any opportunity to perform a graceful
    // shutdown.
    void BeginShutdown();
    void FinishShutdown();

    uint8_t id()  const { return codec_id_; }
    State state() const { return state_; }
    const char* log_prefix() const { return log_prefix_; }

    // Debug/Diags
    void DumpState();

private:
    friend class fbl::RefPtr<IntelHDACodec>;

    using ProbeParseCbk = zx_status_t (IntelHDACodec::*)(const CodecResponse& resp);
    struct ProbeCommandListEntry {
        CodecVerb     verb;
        ProbeParseCbk parse;
    };

    static constexpr size_t PROP_PROTOCOL    = 0;
    static constexpr size_t PROP_VID         = 1;
    static constexpr size_t PROP_DID         = 2;
    static constexpr size_t PROP_MAJOR_REV   = 3;
    static constexpr size_t PROP_MINOR_REV   = 4;
    static constexpr size_t PROP_VENDOR_REV  = 5;
    static constexpr size_t PROP_VENDOR_STEP = 6;
    static constexpr size_t PROP_COUNT       = 7;

    static zx_protocol_device_t CODEC_DEVICE_THUNKS;
    static ihda_codec_protocol_ops_t CODEC_PROTO_THUNKS;

    IntelHDACodec(IntelHDAController& controller, uint8_t codec_id);
    virtual ~IntelHDACodec() { ZX_DEBUG_ASSERT(state_ == State::SHUT_DOWN); }

    zx_status_t PublishDevice();

    void SendCORBResponse(const fbl::RefPtr<dispatcher::Channel>& channel,
                          const CodecResponse& resp,
                          uint32_t transaction_id = IHDA_INVALID_TRANSACTION_ID);

    // Parsers for device probing
    zx_status_t ParseVidDid(const CodecResponse& resp);
    zx_status_t ParseRevisionId(const CodecResponse& resp);

    // ZX_PROTOCOL_IHDA_CODEC Interface
    zx_status_t CodecGetDispatcherChannel(zx_handle_t* channel_out);

    // Thunks for interacting with clients and codec drivers.
    zx_status_t DeviceIoctl(uint32_t op, void* out_buf, size_t out_len, size_t* out_actual);
    zx_status_t ProcessClientRequest(dispatcher::Channel* channel, bool is_driver_channel);
    void ProcessClientDeactivate(const dispatcher::Channel* channel);
    zx_status_t ProcessGetIDs(dispatcher::Channel* channel, const ihda_proto::GetIDsReq& req);
    zx_status_t ProcessSendCORBCmd(dispatcher::Channel* channel,
                                   const ihda_proto::SendCORBCmdReq& req);
    zx_status_t ProcessRequestStream(dispatcher::Channel* channel,
                                     const ihda_proto::RequestStreamReq& req);
    zx_status_t ProcessReleaseStream(dispatcher::Channel* channel,
                                     const ihda_proto::ReleaseStreamReq& req);
    zx_status_t ProcessSetStreamFmt(dispatcher::Channel* channel,
                                    const ihda_proto::SetStreamFmtReq& req);

    // Reference to our owner.
    IntelHDAController& controller_;

    // State management.
    State state_ = State::PROBING;
    uint probe_rx_ndx_ = 0;

    // Driver connection state
    fbl::Mutex codec_driver_channel_lock_;
    fbl::RefPtr<dispatcher::Channel> codec_driver_channel_ TA_GUARDED(codec_driver_channel_lock_);

    // Device properties.
    const uint8_t codec_id_;
    zx_device_prop_t dev_props_[PROP_COUNT];
    zx_device_t* dev_node_ = nullptr;
    struct {
        uint16_t vid;
        uint16_t did;
        uint8_t  ihda_vmaj;
        uint8_t  ihda_vmin;
        uint8_t  rev_id;
        uint8_t  step_id;
    } props_;

    // Log prefix storage
    char log_prefix_[LOG_PREFIX_STORAGE] = { 0 };

    // Dispatcher framework state.
    fbl::RefPtr<dispatcher::ExecutionDomain> default_domain_;

    // Active DMA streams
    fbl::Mutex          active_streams_lock_;
    IntelHDAStream::Tree active_streams_ TA_GUARDED(active_streams_lock_);

    static ProbeCommandListEntry PROBE_COMMANDS[];
};

}  // namespace intel_hda
}  // namespace audio
