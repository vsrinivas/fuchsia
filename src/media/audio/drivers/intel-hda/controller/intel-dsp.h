// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_INTEL_HDA_CONTROLLER_INTEL_DSP_H_
#define SRC_MEDIA_AUDIO_DRIVERS_INTEL_HDA_CONTROLLER_INTEL_DSP_H_

#include <lib/fzl/vmo-mapper.h>
#include <lib/mmio/mmio.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <threads.h>

#include <map>
#include <optional>

#include <ddk/device.h>
#include <ddk/protocol/intelhda/codec.h>
#include <fbl/mutex.h>
#include <fbl/string.h>
#include <intel-hda/codec-utils/codec-driver-base.h>
#include <intel-hda/utils/intel-audio-dsp-ipc.h>
#include <intel-hda/utils/intel-hda-registers.h>
#include <intel-hda/utils/nhlt.h>
#include <intel-hda/utils/status.h>
#include <intel-hda/utils/status_or.h>
#include <intel-hda/utils/utils.h>

#include "debug-logging.h"
#include "intel-dsp-ipc.h"
#include "intel-dsp-modules.h"
#include "intel-dsp-stream.h"
#include "intel-dsp-topology.h"
#include "intel-hda-stream.h"
#include "nhlt.h"

namespace audio {
namespace intel_hda {

class IntelHDAController;

class IntelDsp : public codecs::IntelHDACodecDriverBase {
 public:
  IntelDsp(IntelHDAController* controller, MMIO_PTR hda_pp_registers_t* pp_regs);
  ~IntelDsp();

  Status Init(zx_device_t* dsp_dev);

  const char* log_prefix() const { return log_prefix_; }

  // Interrupt handler.
  void ProcessIrq();

  // Start and stop DSP pipelines.
  Status StartPipeline(DspPipeline pipeline);
  Status PausePipeline(DspPipeline pipeline);

  void DeviceShutdown();
  zx_status_t Suspend(uint8_t requested_state, bool enable_wake, uint8_t suspend_reason,
                      uint8_t* out_state) override final;

  // ZX_PROTOCOL_IHDA_CODEC Interface
  zx_status_t CodecGetDispatcherChannel(zx_handle_t* remote_endpoint_out);

 private:
  // Accessor for our mapped registers
  MMIO_PTR adsp_registers_t* regs() const;
  MMIO_PTR adsp_fw_registers_t* fw_regs() const;

  Status SetupDspDevice();
  Status ParseNhlt();

  int InitThread();

  zx_status_t Boot();
  zx_status_t StripFirmware(const zx::vmo& fw, void* out, size_t* size_inout);
  zx_status_t LoadFirmware();

  zx_status_t GetI2SBlob(uint8_t bus_id, uint8_t direction, const AudioDataFormat& format,
                         const void** out_blob, size_t* out_size);

  zx_status_t GetModulesInfo();
  StatusOr<std::vector<uint8_t>> GetI2SModuleConfig(uint8_t i2s_instance_id, uint8_t direction,
                                                    const CopierCfg& base_cfg);
  bool IsCoreEnabled(uint8_t core_mask);

  zx_status_t ResetCore(uint8_t core_mask);
  zx_status_t UnResetCore(uint8_t core_mask);
  zx_status_t PowerDownCore(uint8_t core_mask);
  zx_status_t PowerUpCore(uint8_t core_mask);
  void RunCore(uint8_t core_mask);

  void EnableInterrupts();

  void Enable();
  void Disable();
  void IrqEnable();
  void IrqDisable();

  // Thunks for interacting with clients and codec drivers.
  zx_status_t ProcessClientRequest(dispatcher::Channel* channel, bool is_driver_channel);
  void ProcessClientDeactivate(const dispatcher::Channel* channel);
  zx_status_t ProcessRequestStream(dispatcher::Channel* channel,
                                   const ihda_proto::RequestStreamReq& req);
  zx_status_t ProcessReleaseStream(dispatcher::Channel* channel,
                                   const ihda_proto::ReleaseStreamReq& req);
  zx_status_t ProcessSetStreamFmt(dispatcher::Channel* channel,
                                  const ihda_proto::SetStreamFmtReq& req);

  zx_status_t CreateAndStartStreams();

  // Receive a notification from the DSP.
  void DspNotificationReceived(NotificationType type);

  // Debug
  void DumpRegs();
  void DumpFirmwareConfig(const TLVHeader* config, size_t length);
  void DumpHardwareConfig(const TLVHeader* config, size_t length);
  void DumpModulesInfo(const ModuleEntry* info, uint32_t count);
  void DumpPipelineListInfo(const PipelineListInfo* info);
  void DumpPipelineProps(const PipelineProps* props);

  enum class State : uint8_t {
    START,
    INITIALIZING,  // init thread running
    OPERATING,
    SHUT_DOWN,
    ERROR = 0xFF,
  };
  State state_ = State::START;

  // Pointer to our owner.
  IntelHDAController* controller_ = nullptr;

  // Pipe processintg registers
  MMIO_PTR hda_pp_registers_t* pp_regs_ = nullptr;

  // PCI registers
  std::optional<ddk::MmioBuffer> mapped_regs_;

  // IPC Channel and controller for DSP hardware.
  std::unique_ptr<DspChannel> ipc_;
  std::unique_ptr<DspModuleController> module_controller_;

  // Notified when the DSP has notified us that the DSP firmware is ready.
  sync_completion_t firmware_ready_;

  std::unique_ptr<Nhlt> nhlt_;

  // Init thread
  thrd_t init_thread_;

  // Log prefix storage
  char log_prefix_[LOG_PREFIX_STORAGE] = {0};

  // Driver connection state
  fbl::Mutex codec_driver_channel_lock_;
  fbl::RefPtr<dispatcher::Channel> codec_driver_channel_ TA_GUARDED(codec_driver_channel_lock_);

  // Active DMA streams
  fbl::Mutex active_streams_lock_;
  IntelHDAStream::Tree active_streams_ TA_GUARDED(active_streams_lock_);
};

}  // namespace intel_hda
}  // namespace audio

#endif  // SRC_MEDIA_AUDIO_DRIVERS_INTEL_HDA_CONTROLLER_INTEL_DSP_H_
