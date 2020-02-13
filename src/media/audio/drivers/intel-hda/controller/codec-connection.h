// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_INTEL_HDA_CONTROLLER_CODEC_CONNECTION_H_
#define SRC_MEDIA_AUDIO_DRIVERS_INTEL_HDA_CONTROLLER_CODEC_CONNECTION_H_

#include <fuchsia/hardware/intel/hda/c/fidl.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/handle.h>
#include <stdint.h>
#include <string.h>

#include <memory>

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/protocol/intelhda/codec.h>
#include <dispatcher-pool/dispatcher-channel.h>
#include <dispatcher-pool/dispatcher-execution-domain.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <intel-hda/utils/codec-commands.h>
#include <intel-hda/utils/intel-hda-proto.h>
#include <intel-hda/utils/intel-hda-registers.h>

#include "codec-cmd-job.h"
#include "debug-logging.h"
#include "intel-hda-stream.h"

namespace audio {
namespace intel_hda {

class IntelHDAController;
struct CodecResponse;

// CodecConnection manages a connection to a child codec driver.
class CodecConnection : public fbl::RefCounted<CodecConnection> {
 public:
  enum class State {
    PROBING,
    FINDING_DRIVER,
    OPERATING,
    SHUTTING_DOWN,
    SHUT_DOWN,
    FATAL_ERROR,
  };

  static fbl::RefPtr<CodecConnection> Create(IntelHDAController& controller, uint8_t codec_id);

  zx_status_t Startup();
  void ProcessSolicitedResponse(const CodecResponse& resp, std::unique_ptr<CodecCmdJob>&& job);
  void ProcessUnsolicitedResponse(const CodecResponse& resp);
  void ProcessWakeupEvt();

  // TODO (johngro) : figure out shutdown... Currently, this expected to
  // execute synchronously, which does not allow codec drivers any opportunity
  // to perform a graceful shutdown.
  //
  // OTOH - If our driver is being unloaded by the device manager, in theory,
  // it should have already unloaded all of the codecs, giving them a chances
  // to quiesce their hardware in the process.
  void Shutdown();

  uint8_t id() const { return codec_id_; }
  State state() const { return state_; }
  const char* log_prefix() const { return log_prefix_; }

  // Debug/Diags
  void DumpState();

 private:
  friend class fbl::RefPtr<CodecConnection>;

  using ProbeParseCbk = zx_status_t (CodecConnection::*)(const CodecResponse& resp);
  struct ProbeCommandListEntry {
    CodecVerb verb;
    ProbeParseCbk parse;
  };

  static constexpr size_t PROP_PROTOCOL = 0;
  static constexpr size_t PROP_VID = 1;
  static constexpr size_t PROP_DID = 2;
  static constexpr size_t PROP_MAJOR_REV = 3;
  static constexpr size_t PROP_MINOR_REV = 4;
  static constexpr size_t PROP_VENDOR_REV = 5;
  static constexpr size_t PROP_VENDOR_STEP = 6;
  static constexpr size_t PROP_COUNT = 7;

  static fuchsia_hardware_intel_hda_CodecDevice_ops_t CODEC_FIDL_THUNKS;
  static zx_protocol_device_t CODEC_DEVICE_THUNKS;
  static ihda_codec_protocol_ops_t CODEC_PROTO_THUNKS;

  CodecConnection(IntelHDAController& controller, uint8_t codec_id);
  virtual ~CodecConnection() { ZX_DEBUG_ASSERT(state_ == State::SHUT_DOWN); }

  zx_status_t PublishDevice();

  void SendCORBResponse(const fbl::RefPtr<dispatcher::Channel>& channel, const CodecResponse& resp,
                        uint32_t transaction_id = IHDA_INVALID_TRANSACTION_ID);

  // Parsers for device probing
  zx_status_t ParseVidDid(const CodecResponse& resp);
  zx_status_t ParseRevisionId(const CodecResponse& resp);

  // ZX_PROTOCOL_IHDA_CODEC Interface
  zx_status_t CodecGetDispatcherChannel(zx_handle_t* remote_endpoint_out);

  // Thunks for interacting with clients and codec drivers.
  zx_status_t GetChannel(fidl_txn_t* txn);
  zx_status_t ProcessUserRequest(dispatcher::Channel* channel);
  zx_status_t ProcessCodecRequest(dispatcher::Channel* channel);
  void ProcessCodecDeactivate(const dispatcher::Channel* channel);
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
    uint8_t ihda_vmaj;
    uint8_t ihda_vmin;
    uint8_t rev_id;
    uint8_t step_id;
  } props_;

  // Log prefix storage
  char log_prefix_[LOG_PREFIX_STORAGE] = {0};

  // Dispatcher framework state.
  fbl::RefPtr<dispatcher::ExecutionDomain> default_domain_;

  // Active DMA streams
  fbl::Mutex active_streams_lock_;
  IntelHDAStream::Tree active_streams_ TA_GUARDED(active_streams_lock_);

  static ProbeCommandListEntry PROBE_COMMANDS[];
};

}  // namespace intel_hda
}  // namespace audio

#endif  // SRC_MEDIA_AUDIO_DRIVERS_INTEL_HDA_CONTROLLER_CODEC_CONNECTION_H_
