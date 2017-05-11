// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/pci.h>
#include <magenta/types.h>
#include <mxtl/atomic.h>
#include <mxtl/intrusive_single_list.h>
#include <mxtl/recycler.h>
#include <mxtl/unique_ptr.h>
#include <threads.h>

#include "drivers/audio/dispatcher-pool/dispatcher-channel.h"
#include "drivers/audio/intel-hda/utils/codec-commands.h"
#include "drivers/audio/intel-hda/utils/intel-hda-registers.h"
#include "drivers/audio/intel-hda/utils/intel-hda-proto.h"

#include "codec-cmd-job.h"
#include "intel-hda-codec.h"
#include "intel-hda-device.h"
#include "thread-annotations.h"
#include "utils.h"

namespace audio {
namespace intel_hda {

class IntelHDAController : public IntelHDADevice<IntelHDAController> {
public:
    // Type export for IntelHDADevice<>
    union RequestBufferType {
        ihda_cmd_hdr_t                      hdr;
        ihda_get_ids_req_t                  get_ids;
        ihda_controller_snapshot_regs_req_t snapshot_regs;
    };

    IntelHDAController();
    ~IntelHDAController();

    mx_status_t Init(mx_device_t* pci_dev);
    void PrintDebugPrefix() const;
    const char*  dev_name() const { return device_get_name(dev_node_); }
    mx_device_t* dev_node() { return dev_node_; }
    unsigned int id() const { return id_; }

    // CORB/RIRB
    mx_status_t QueueCodecCmd(mxtl::unique_ptr<CodecCmdJob>&& job) TA_EXCL(corb_lock_);

    // DMA Streams
    mxtl::RefPtr<IntelHDAStream> AllocateStream(IntelHDAStream::Type type)
        TA_EXCL(stream_pool_lock_);
    void ReturnStream(mxtl::RefPtr<IntelHDAStream>&& stream)
        TA_EXCL(stream_pool_lock_);

    static mx_status_t DriverInit(mx_driver_t* driver);
    static mx_status_t DriverBind(mx_driver_t* driver, mx_device_t* device, void** cookie);
    static void        DriverUnbind(mx_driver_t* driver, mx_device_t* device, void* cookie);
    static mx_status_t DriverRelease(mx_driver_t* driver);
    static mx_driver_t* driver() { return driver_; }

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
    void    ReturnStreamLocked(mxtl::RefPtr<IntelHDAStream>&& stream) TA_REQ (stream_pool_lock_);
    uint8_t AllocateStreamTagLocked(bool input)                       TA_REQ (stream_pool_lock_);
    void    ReleaseStreamTagLocked (bool input, uint8_t tag_num)      TA_REQ (stream_pool_lock_);

    // Device interface implementation
    void        DeviceShutdown();
    mx_status_t DeviceRelease();

    // State control
    // TODO(johngro) : extend mxtl::atomic to support enum classes as well.
    void  SetState(State state) { state_.store(static_cast<StateStorage>(state)); }
    State GetState()            { return static_cast<State>(state_.load()); }

    // Codec lifetime maanagement
    mxtl::RefPtr<IntelHDACodec> GetCodec(uint id);

    // Methods used during initialization
    mx_status_t InitInternal(mx_device_t* pci_dev);
    mx_status_t ResetControllerHW();
    mx_status_t SetupPCIDevice(mx_device_t* pci_dev);
    mx_status_t SetupStreamDescriptors() TA_EXCL(stream_pool_lock_);
    mx_status_t SetupCommandBufferSize(uint8_t* size_reg, unsigned int* entry_count);
    mx_status_t SetupCommandBuffer() TA_EXCL(corb_lock_, rirb_lock_);

    void WaitForIrqOrWakeup();

    mx_status_t ResetCORBRdPtrLocked() TA_REQ(corb_lock_);

    void SnapshotRIRB() TA_EXCL(corb_lock_, rirb_lock_);
    void ProcessRIRB()  TA_EXCL(corb_lock_, rirb_lock_);

    void ProcessCORB()                        TA_EXCL(corb_lock_, rirb_lock_);
    void ComputeCORBSpaceLocked()             TA_REQ(corb_lock_);
    void CommitCORBLocked()                   TA_REQ(corb_lock_);
    void SendCodecCmdLocked(CodecCommand cmd) TA_REQ(corb_lock_);

    void ProcessStreamIRQ(uint32_t intsts);
    void ProcessControllerIRQ();

    // Implementation of IntelHDADevice<> callback.
    friend class IntelHDADevice<IntelHDAController>;
    mx_status_t ProcessClientRequest(DispatcherChannel* channel,
                                     const RequestBufferType& req,
                                     uint32_t req_size,
                                     mx::handle&& rxed_handle)
        TA_REQ(process_lock());

    // Client requests
    mx_status_t SnapshotRegs(const DispatcherChannel& channel,
                             const ihda_controller_snapshot_regs_req_t& req)
        TA_REQ(process_lock());

    // IRQ thread and state machine.
    mxtl::atomic<StateStorage> state_;
    thrd_t                     irq_thread_;
    bool                       irq_thread_started_ = false;

    // Debug stuff
    char debug_tag_[MX_DEVICE_NAME_MAX] = { 0 };

    // Upstream PCI device, protocol interface, and device info.
    mx_device_t*          pci_dev_   = nullptr;
    pci_protocol_t*       pci_proto_ = nullptr;
    mx_pcie_device_info_t pci_dev_info_;

    // Unique ID and published HDA device node.
    const uint32_t id_;
    mx_device_t* dev_node_ = nullptr;

    // PCI Registers and IRQ
    mx_handle_t      irq_handle_  = MX_HANDLE_INVALID;
    bool             msi_irq_     = false;
    mx_handle_t      regs_handle_ = MX_HANDLE_INVALID;
    hda_registers_t* regs_        = nullptr;

    // Contiguous physical memory allocated for the command buffer (CORB/RIRB)
    // and Stream Buffer Desctiptor Lists (BDLs)
    ContigPhysMem  bdl_mem_     TA_GUARDED(stream_pool_lock_);
    ContigPhysMem  cmd_buf_mem_ TA_GUARDED(corb_lock_);

    // Stream state
    mxtl::Mutex          stream_pool_lock_;
    IntelHDAStream::Tree free_input_streams_  TA_GUARDED(stream_pool_lock_);
    IntelHDAStream::Tree free_output_streams_ TA_GUARDED(stream_pool_lock_);
    IntelHDAStream::Tree free_bidir_streams_  TA_GUARDED(stream_pool_lock_);
    uint16_t             free_input_tags_     TA_GUARDED(stream_pool_lock_) = 0xFFFEu;
    uint16_t             free_output_tags_    TA_GUARDED(stream_pool_lock_) = 0xFFFEu;

    // Array of pointers to all possible streams (used for O(1) lookup during IRQ dispatch)
    mxtl::RefPtr<IntelHDAStream> all_streams_[IntelHDAStream::MAX_STREAMS_PER_CONTROLLER];

    // Codec bus command ring-buffer state (CORB/RIRB)
    mxtl::Mutex    corb_lock_;
    CodecCommand*  corb_               TA_GUARDED(corb_lock_) = nullptr;
    unsigned int   corb_entry_count_   TA_GUARDED(corb_lock_) = 0;
    unsigned int   corb_mask_          TA_GUARDED(corb_lock_) = 0;
    unsigned int   corb_wr_ptr_        TA_GUARDED(corb_lock_) = 0;
    unsigned int   corb_space_         TA_GUARDED(corb_lock_) = 0;
    unsigned int   corb_max_in_flight_ TA_GUARDED(corb_lock_) = 0;

    mxtl::Mutex    rirb_lock_          TA_ACQ_BEFORE(corb_lock_);
    CodecResponse* rirb_               TA_GUARDED(rirb_lock_) = nullptr;
    unsigned int   rirb_entry_count_   TA_GUARDED(rirb_lock_) = 0;
    unsigned int   rirb_mask_          TA_GUARDED(rirb_lock_) = 0;
    unsigned int   rirb_rd_ptr_        TA_GUARDED(rirb_lock_) = 0;
    unsigned int   rirb_snapshot_cnt_  TA_GUARDED(rirb_lock_) = 0;
    CodecResponse  rirb_snapshot_[HDA_RIRB_MAX_ENTRIES] TA_GUARDED(rirb_lock_);

    mxtl::DoublyLinkedList<mxtl::unique_ptr<CodecCmdJob>> in_flight_corb_jobs_
        TA_GUARDED(corb_lock_);
    mxtl::DoublyLinkedList<mxtl::unique_ptr<CodecCmdJob>> pending_corb_jobs_
        TA_GUARDED(corb_lock_);

    mxtl::Mutex codec_lock_;
    mxtl::RefPtr<IntelHDACodec> codecs_[HDA_MAX_CODECS];

    static mxtl::atomic_uint32_t device_id_gen_;
    static mx_driver_t*          driver_;
    static mx_protocol_device_t  CONTROLLER_DEVICE_THUNKS;
};

}  // namespace intel_hda
}  // namespace audio
