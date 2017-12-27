// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/pci.h>
#include <zircon/types.h>
#include <fbl/atomic.h>
#include <fbl/intrusive_single_list.h>
#include <fbl/recycler.h>
#include <fbl/unique_ptr.h>
#include <threads.h>

#include <dispatcher-pool/dispatcher-execution-domain.h>
#include <intel-hda/utils/codec-commands.h>
#include <intel-hda/utils/intel-hda-registers.h>
#include <intel-hda/utils/intel-hda-proto.h>

#include "codec-cmd-job.h"
#include "intel-hda-codec.h"
#include "thread-annotations.h"
#include "utils.h"

namespace audio {
namespace intel_hda {

class IntelHDAController : public fbl::RefCounted<IntelHDAController> {
public:
    IntelHDAController();
    ~IntelHDAController();

    zx_status_t Init(zx_device_t* pci_dev);
    void PrintDebugPrefix() const;
    const char*  dev_name() const { return device_get_name(dev_node_); }
    zx_device_t* dev_node() { return dev_node_; }
    unsigned int id() const { return id_; }

    // CORB/RIRB
    zx_status_t QueueCodecCmd(fbl::unique_ptr<CodecCmdJob>&& job) TA_EXCL(corb_lock_);

    // DMA Streams
    fbl::RefPtr<IntelHDAStream> AllocateStream(IntelHDAStream::Type type)
        TA_EXCL(stream_pool_lock_);
    void ReturnStream(fbl::RefPtr<IntelHDAStream>&& stream)
        TA_EXCL(stream_pool_lock_);

    static zx_status_t DriverInit(void** out_ctx);
    static zx_status_t DriverBind(void* ctx, zx_device_t* device);
    static void        DriverRelease(void* ctx);

private:
    using StateStorage = uint32_t;
    enum class State : StateStorage {
        STARTING,
        OPERATING,
        SHUTTING_DOWN,
        SHUT_DOWN,
    };

    static constexpr uint RIRB_RESERVED_RESPONSE_SLOTS = 8u;

    int  IRQThread();
    void WakeupIRQThread();
    void ShutdownIRQThread();

    // Internal stream bookkeeping.
    void    ReturnStreamLocked(fbl::RefPtr<IntelHDAStream>&& stream) TA_REQ (stream_pool_lock_);
    uint8_t AllocateStreamTagLocked(bool input)                       TA_REQ (stream_pool_lock_);
    void    ReleaseStreamTagLocked (bool input, uint8_t tag_num)      TA_REQ (stream_pool_lock_);

    // Device interface implementation
    void        DeviceShutdown();
    void        DeviceRelease();
    zx_status_t DeviceIoctl(uint32_t op, void* out_buf, size_t out_len, size_t* out_actual);

    // Root device interface implementation
    void RootDeviceRelease();

    // State control
    // TODO(johngro) : extend fbl::atomic to support enum classes as well.
    void  SetState(State state) { state_.store(static_cast<StateStorage>(state)); }
    State GetState()            { return static_cast<State>(state_.load()); }

    // Codec lifetime maanagement
    fbl::RefPtr<IntelHDACodec> GetCodec(uint id);

    // Methods used during initialization
    zx_status_t InitInternal(zx_device_t* pci_dev);
    zx_status_t ResetControllerHW();
    zx_status_t SetupPCIDevice(zx_device_t* pci_dev);
    zx_status_t SetupPCIInterrupts();
    zx_status_t SetupStreamDescriptors() TA_EXCL(stream_pool_lock_);
    zx_status_t SetupCommandBufferSize(uint8_t* size_reg, unsigned int* entry_count);
    zx_status_t SetupCommandBuffer() TA_EXCL(corb_lock_, rirb_lock_);

    void WaitForIrqOrWakeup();

    zx_status_t ResetCORBRdPtrLocked() TA_REQ(corb_lock_);

    void SnapshotRIRB() TA_EXCL(corb_lock_, rirb_lock_);
    void ProcessRIRB()  TA_EXCL(corb_lock_, rirb_lock_);

    void ProcessCORB()                        TA_EXCL(corb_lock_, rirb_lock_);
    void ComputeCORBSpaceLocked()             TA_REQ(corb_lock_);
    void CommitCORBLocked()                   TA_REQ(corb_lock_);
    void SendCodecCmdLocked(CodecCommand cmd) TA_REQ(corb_lock_);

    void ProcessStreamIRQ(uint32_t intsts);
    void ProcessControllerIRQ();

    // Thunk for interacting with client channels
    zx_status_t ProcessClientRequest(dispatcher::Channel* channel);
    zx_status_t SnapshotRegs(dispatcher::Channel* channel,
                             const ihda_controller_snapshot_regs_req_t& req);

    // Dispatcher framework state
    fbl::RefPtr<dispatcher::ExecutionDomain> default_domain_;

    // IRQ thread and state machine.
    fbl::atomic<StateStorage> state_;
    thrd_t                     irq_thread_;
    bool                       irq_thread_started_ = false;

    // Debug stuff
    char debug_tag_[ZX_DEVICE_NAME_MAX] = { 0 };

    // Upstream PCI device, protocol interface, and device info.
    zx_device_t*          pci_dev_ = nullptr;
    pci_protocol_t        pci_ = { nullptr, nullptr };
    zx_pcie_device_info_t pci_dev_info_;
    static zx_protocol_device_t ROOT_DEVICE_THUNKS;

    // Unique ID and published HDA device node.
    const uint32_t id_;
    zx_device_t* dev_node_ = nullptr;

    // PCI Registers and IRQ
    zx_handle_t      irq_handle_  = ZX_HANDLE_INVALID;
    zx_handle_t      regs_handle_ = ZX_HANDLE_INVALID;
    hda_registers_t* regs_        = nullptr;

    // Contiguous physical memory allocated for the command buffer (CORB/RIRB)
    // and Stream Buffer Desctiptor Lists (BDLs)
    ContigPhysMem  bdl_mem_     TA_GUARDED(stream_pool_lock_);
    ContigPhysMem  cmd_buf_mem_ TA_GUARDED(corb_lock_);

    // Stream state
    fbl::Mutex          stream_pool_lock_;
    IntelHDAStream::Tree free_input_streams_  TA_GUARDED(stream_pool_lock_);
    IntelHDAStream::Tree free_output_streams_ TA_GUARDED(stream_pool_lock_);
    IntelHDAStream::Tree free_bidir_streams_  TA_GUARDED(stream_pool_lock_);
    uint16_t             free_input_tags_     TA_GUARDED(stream_pool_lock_) = 0xFFFEu;
    uint16_t             free_output_tags_    TA_GUARDED(stream_pool_lock_) = 0xFFFEu;

    // Array of pointers to all possible streams (used for O(1) lookup during IRQ dispatch)
    fbl::RefPtr<IntelHDAStream> all_streams_[IntelHDAStream::MAX_STREAMS_PER_CONTROLLER];

    // Codec bus command ring-buffer state (CORB/RIRB)
    fbl::Mutex    corb_lock_;
    CodecCommand*  corb_               TA_GUARDED(corb_lock_) = nullptr;
    unsigned int   corb_entry_count_   TA_GUARDED(corb_lock_) = 0;
    unsigned int   corb_mask_          TA_GUARDED(corb_lock_) = 0;
    unsigned int   corb_wr_ptr_        TA_GUARDED(corb_lock_) = 0;
    unsigned int   corb_space_         TA_GUARDED(corb_lock_) = 0;
    unsigned int   corb_max_in_flight_ TA_GUARDED(corb_lock_) = 0;

    fbl::Mutex    rirb_lock_          TA_ACQ_BEFORE(corb_lock_);
    CodecResponse* rirb_               TA_GUARDED(rirb_lock_) = nullptr;
    unsigned int   rirb_entry_count_   TA_GUARDED(rirb_lock_) = 0;
    unsigned int   rirb_mask_          TA_GUARDED(rirb_lock_) = 0;
    unsigned int   rirb_rd_ptr_        TA_GUARDED(rirb_lock_) = 0;
    unsigned int   rirb_snapshot_cnt_  TA_GUARDED(rirb_lock_) = 0;
    CodecResponse  rirb_snapshot_[HDA_RIRB_MAX_ENTRIES] TA_GUARDED(rirb_lock_);

    fbl::DoublyLinkedList<fbl::unique_ptr<CodecCmdJob>> in_flight_corb_jobs_
        TA_GUARDED(corb_lock_);
    fbl::DoublyLinkedList<fbl::unique_ptr<CodecCmdJob>> pending_corb_jobs_
        TA_GUARDED(corb_lock_);

    fbl::Mutex codec_lock_;
    fbl::RefPtr<IntelHDACodec> codecs_[HDA_MAX_CODECS];

    static fbl::atomic_uint32_t device_id_gen_;
    static zx_protocol_device_t  CONTROLLER_DEVICE_THUNKS;
};

}  // namespace intel_hda
}  // namespace audio
