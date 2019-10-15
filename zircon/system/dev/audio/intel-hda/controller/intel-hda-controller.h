// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_AUDIO_INTEL_HDA_CONTROLLER_INTEL_HDA_CONTROLLER_H_
#define ZIRCON_SYSTEM_DEV_AUDIO_INTEL_HDA_CONTROLLER_INTEL_HDA_CONTROLLER_H_

#include <fuchsia/hardware/intel/hda/c/fidl.h>
#include <lib/fzl/pinned-vmo.h>
#include <lib/fzl/vmar-manager.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/interrupt.h>
#include <threads.h>
#include <zircon/types.h>

#include <atomic>
#include <memory>

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/pci.h>
#include <dispatcher-pool/dispatcher-execution-domain.h>
#include <dispatcher-pool/dispatcher-interrupt.h>
#include <dispatcher-pool/dispatcher-wakeup-event.h>
#include <fbl/intrusive_single_list.h>
#include <fbl/recycler.h>
#include <intel-hda/utils/codec-commands.h>
#include <intel-hda/utils/intel-hda-proto.h>
#include <intel-hda/utils/intel-hda-registers.h>
#include <intel-hda/utils/utils.h>

#include "codec-cmd-job.h"
#include "codec-connection.h"
#include "debug-logging.h"
#include "intel-dsp.h"
#include "utils.h"

namespace audio {
namespace intel_hda {

class IntelHDAController : public fbl::RefCounted<IntelHDAController> {
 public:
  IntelHDAController();
  ~IntelHDAController();

  zx_status_t Init(zx_device_t* pci_dev);

  // one-liner accessors.
  const char* dev_name() const { return device_get_name(dev_node_); }
  zx_device_t* dev_node() { return dev_node_; }
  const zx_pcie_device_info_t& dev_info() const { return pci_dev_info_; }
  unsigned int id() const { return id_; }
  const char* log_prefix() const { return log_prefix_; }
  const pci_protocol_t* pci() const { return &pci_; }
  const fbl::RefPtr<RefCountedBti>& pci_bti() const { return pci_bti_; }

  const fbl::RefPtr<dispatcher::ExecutionDomain>& default_domain() const { return default_domain_; }

  // CORB/RIRB
  zx_status_t QueueCodecCmd(std::unique_ptr<CodecCmdJob>&& job) TA_EXCL(corb_lock_);

  // DMA Streams
  fbl::RefPtr<IntelHDAStream> AllocateStream(IntelHDAStream::Type type) TA_EXCL(stream_pool_lock_);
  void ReturnStream(fbl::RefPtr<IntelHDAStream>&& ptr) TA_EXCL(stream_pool_lock_);

  static zx_status_t DriverInit(void** out_ctx);
  static zx_status_t DriverBind(void* ctx, zx_device_t* device);
  static void DriverRelease(void* ctx);

 private:
  enum class State : uint32_t {
    STARTING,
    OPERATING,
    SHUTTING_DOWN,
    SHUT_DOWN,
  };

  static constexpr uint RIRB_RESERVED_RESPONSE_SLOTS = 8u;

  // Accessor for our mapped registers
  hda_registers_t* regs() const {
    return &reinterpret_cast<hda_all_registers_t*>(mapped_regs_.start())->regs;
  }

  // Internal stream bookkeeping.
  void ReturnStreamLocked(fbl::RefPtr<IntelHDAStream>&& ptr) TA_REQ(stream_pool_lock_);
  uint8_t AllocateStreamTagLocked(bool input) TA_REQ(stream_pool_lock_);
  void ReleaseStreamTagLocked(bool input, uint8_t tag_num) TA_REQ(stream_pool_lock_);

  // Device interface implementation
  zx_status_t DeviceGetProtocol(uint32_t proto_id, void* protocol);
  void DeviceShutdown();
  void DeviceRelease();
  zx_status_t GetChannel(fidl_txn_t* txn);

  // Root device interface implementation
  void RootDeviceRelease();

  // State control
  void SetState(State state) { state_.store(state); }
  State GetState() { return state_.load(); }

  // Codec lifetime maanagement
  fbl::RefPtr<CodecConnection> GetCodec(uint id);

  // Methods used during initialization
  zx_status_t InitInternal(zx_device_t* pci_dev);
  zx_status_t ResetControllerHW();
  zx_status_t SetupPCIDevice(zx_device_t* pci_dev);
  zx_status_t SetupPCIInterrupts();
  zx_status_t SetupStreamDescriptors() TA_EXCL(stream_pool_lock_);
  zx_status_t SetupCommandBufferSize(uint8_t* size_reg, unsigned int* entry_count);
  zx_status_t SetupCommandBuffer() TA_EXCL(corb_lock_, rirb_lock_);
  zx_status_t ProbeAudioDSP(zx_device_t* dsp_dev);

  zx_status_t ResetCORBRdPtrLocked() TA_REQ(corb_lock_);

  void SnapshotRIRB() TA_EXCL(corb_lock_, rirb_lock_);
  void ProcessRIRB() TA_EXCL(corb_lock_, rirb_lock_);

  void ProcessCORB() TA_EXCL(corb_lock_, rirb_lock_);
  void ComputeCORBSpaceLocked() TA_REQ(corb_lock_);
  void CommitCORBLocked() TA_REQ(corb_lock_);
  void SendCodecCmdLocked(CodecCommand cmd) TA_REQ(corb_lock_);

  void ProcessStreamIRQ(uint32_t intsts);
  void ProcessControllerIRQ();
  zx_status_t HandleIrq() TA_REQ(default_domain_->token());
  void WakeupIrqHandler();

  // Thunk for interacting with client channels
  zx_status_t ProcessClientRequest(dispatcher::Channel* channel);
  zx_status_t SnapshotRegs(dispatcher::Channel* channel,
                           const ihda_controller_snapshot_regs_req_t& req);

  // VMAR for memory mapped registers.
  fbl::RefPtr<fzl::VmarManager> vmar_manager_;

  // Dispatcher framework state
  fbl::RefPtr<dispatcher::ExecutionDomain> default_domain_;

  // State machine and IRQ related events.
  std::atomic<State> state_;
  fbl::RefPtr<dispatcher::Interrupt> irq_;
  fbl::RefPtr<dispatcher::WakeupEvent> irq_wakeup_event_;

  // Log prefix storage
  char log_prefix_[LOG_PREFIX_STORAGE] = {0};

  // Upstream PCI device, protocol interface, and device info.
  zx_device_t* pci_dev_ = nullptr;
  pci_protocol_t pci_ = {nullptr, nullptr};
  zx_pcie_device_info_t pci_dev_info_;
  static zx_protocol_device_t ROOT_DEVICE_THUNKS;

  // Unique ID and published HDA device node.
  const uint32_t id_;
  zx_device_t* dev_node_ = nullptr;

  // PCI Registers
  fzl::VmoMapper mapped_regs_;

  // A handle to the Bus Transaction Initiator for this PCI device.  Used to
  // grant access to specific regions of physical mememory to the controller
  // hardware so that it may DMA.
  fbl::RefPtr<RefCountedBti> pci_bti_;

  // Physical memory allocated for the command buffer (CORB/RIRB)
  fzl::VmoMapper cmd_buf_cpu_mem_ TA_GUARDED(corb_lock_);
  fzl::PinnedVmo cmd_buf_hda_mem_ TA_GUARDED(corb_lock_);

  // Stream state
  fbl::Mutex stream_pool_lock_;
  IntelHDAStream::Tree free_input_streams_ TA_GUARDED(stream_pool_lock_);
  IntelHDAStream::Tree free_output_streams_ TA_GUARDED(stream_pool_lock_);
  IntelHDAStream::Tree free_bidir_streams_ TA_GUARDED(stream_pool_lock_);
  uint16_t free_input_tags_ TA_GUARDED(stream_pool_lock_) = 0xFFFEu;
  uint16_t free_output_tags_ TA_GUARDED(stream_pool_lock_) = 0xFFFEu;

  // Array of pointers to all possible streams (used for O(1) lookup during IRQ dispatch)
  fbl::RefPtr<IntelHDAStream> all_streams_[MAX_STREAMS_PER_CONTROLLER];

  // Codec bus command ring-buffer state (CORB/RIRB)
  fbl::Mutex corb_lock_;
  CodecCommand* corb_ TA_GUARDED(corb_lock_) = nullptr;
  unsigned int corb_entry_count_ TA_GUARDED(corb_lock_) = 0;
  unsigned int corb_mask_ TA_GUARDED(corb_lock_) = 0;
  unsigned int corb_wr_ptr_ TA_GUARDED(corb_lock_) = 0;
  unsigned int corb_space_ TA_GUARDED(corb_lock_) = 0;
  unsigned int corb_max_in_flight_ TA_GUARDED(corb_lock_) = 0;

  fbl::Mutex rirb_lock_ TA_ACQ_BEFORE(corb_lock_);
  CodecResponse* rirb_ TA_GUARDED(rirb_lock_) = nullptr;
  unsigned int rirb_entry_count_ TA_GUARDED(rirb_lock_) = 0;
  unsigned int rirb_mask_ TA_GUARDED(rirb_lock_) = 0;
  unsigned int rirb_rd_ptr_ TA_GUARDED(rirb_lock_) = 0;
  unsigned int rirb_snapshot_cnt_ TA_GUARDED(rirb_lock_) = 0;
  CodecResponse rirb_snapshot_[HDA_RIRB_MAX_ENTRIES] TA_GUARDED(rirb_lock_);

  fbl::DoublyLinkedList<std::unique_ptr<CodecCmdJob>> in_flight_corb_jobs_ TA_GUARDED(corb_lock_);
  fbl::DoublyLinkedList<std::unique_ptr<CodecCmdJob>> pending_corb_jobs_ TA_GUARDED(corb_lock_);

  fbl::Mutex codec_lock_;
  fbl::RefPtr<CodecConnection> codecs_[HDA_MAX_CODECS];

  fbl::RefPtr<IntelDsp> dsp_;

  static std::atomic_uint32_t device_id_gen_;
  static fuchsia_hardware_intel_hda_ControllerDevice_ops_t CONTROLLER_FIDL_THUNKS;
  static zx_protocol_device_t CONTROLLER_DEVICE_THUNKS;
  static ihda_codec_protocol_ops_t CODEC_PROTO_THUNKS;
};

}  // namespace intel_hda
}  // namespace audio

#endif  // ZIRCON_SYSTEM_DEV_AUDIO_INTEL_HDA_CONTROLLER_INTEL_HDA_CONTROLLER_H_
