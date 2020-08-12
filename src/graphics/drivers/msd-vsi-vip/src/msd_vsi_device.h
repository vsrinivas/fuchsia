// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MSD_VSI_DEVICE_H
#define MSD_VSI_DEVICE_H

#include <list>
#include <memory>
#include <mutex>
#include <thread>

#include "device_request.h"
#include "gpu_features.h"
#include "gpu_progress.h"
#include "magma_util/macros.h"
#include "magma_util/register_io.h"
#include "magma_util/thread.h"
#include "magma_vsi_vip_types.h"
#include "mapped_batch.h"
#include "msd.h"
#include "msd_vsi_connection.h"
#include "msd_vsi_platform_device.h"
#include "page_table_arrays.h"
#include "page_table_slot_allocator.h"
#include "platform_bus_mapper.h"
#include "platform_semaphore.h"
#include "ringbuffer.h"
#include "sequencer.h"

class MsdVsiDevice : public msd_device_t,
                     public AddressSpace::Owner,
                     public MsdVsiConnection::Owner {
 public:
  using DeviceRequest = DeviceRequest<MsdVsiDevice>;

  // Creates a device for the given |device_handle| and returns ownership.
  // If |start_device_thread| is false, then StartDeviceThread should be called
  // to enable device request processing.
  static std::unique_ptr<MsdVsiDevice> Create(void* device_handle, bool start_device_thread);

  MsdVsiDevice() { magic_ = kMagic; }

  virtual ~MsdVsiDevice();

  uint32_t device_id() const { return device_id_; }
  uint32_t revision() const { return revision_; }

  bool Shutdown();
  bool IsIdle();
  bool StopRingbuffer();

  std::unique_ptr<MsdVsiConnection> Open(msd_client_id_t client_id);

  magma_status_t ChipIdentity(magma_vsi_vip_chip_identity* out_identity);
  magma_status_t ChipOption(magma_vsi_vip_chip_option* out_option);
  magma_status_t QuerySram(uint32_t* handle_out);

  struct DumpState {
    uint64_t last_completed_sequence_number;
    uint64_t last_submitted_sequence_number;
    bool idle;
    // This may be false if no batch has been submitted yet.
    bool page_table_arrays_enabled;
    uint32_t exec_addr;

    std::vector<MappedBatch*> inflight_batches;

    bool fault_present;
    uint32_t fault_type;
    uint64_t fault_gpu_address;
  };

  // Since the mmu exception register resets on read, we need to pass it on to the dump functions.
  void Dump(DumpState* dump_state, bool fault_present);
  void DumpToString(std::vector<std::string>* dump_out, bool fault_present);
  void DumpStatusToLog();

  std::vector<MappedBatch*> GetInflightBatches();

  static MsdVsiDevice* cast(msd_device_t* dev) {
    DASSERT(dev);
    DASSERT(dev->magic_ == kMagic);
    return static_cast<MsdVsiDevice*>(dev);
  }

 private:
  // Number of new commands added to the ringbuffer for each submitted batch:
  // EVENT, WAIT, LINK
  static constexpr uint32_t kRbInstructionsPerBatch = 3;
  // Number of new instructions added to the ringbuffer for flushing the TLB:
  // LOAD_STATE, SEMAPHORE, STALL, WAIT, LINK
  // This is in addition to |kRbInstructionsPerBatch|.
  static constexpr uint32_t kRbInstructionsPerFlush = 5;
  // Includes an additional instruction for address space switching.
  static constexpr uint32_t kRbMaxInstructionsPerEvent =
      kRbInstructionsPerBatch + kRbInstructionsPerFlush + 1;

  static constexpr uint32_t kInvalidRingbufferOffset = ~0;

  // The hardware provides 30 bits for interrupt events and 2 bits for errors.
  static constexpr uint32_t kNumEvents = 30;
  struct Event {
    bool allocated = false;
    bool submitted = false;
    bool free_on_complete = false;

    // The offset following this event in the ringbuffer.
    uint32_t ringbuffer_offset = kInvalidRingbufferOffset;
    std::unique_ptr<MappedBatch> mapped_batch;
    // If |mapped_batch| requires an address space switch, this will be populated with the
    // address space the ringbuffer was last configured with, to ensure it stays alive until the
    // switch is completed by hardware.
    std::shared_ptr<AddressSpace> prev_address_space;
  };

#define CHECK_THREAD_IS_CURRENT(x) \
  if (x)                           \
  DASSERT(magma::ThreadIdCheck::IsCurrent(*x))

#define CHECK_THREAD_NOT_CURRENT(x) \
  if (x)                            \
  DASSERT(!magma::ThreadIdCheck::IsCurrent(*x))

  void HangCheckTimeout();
  bool Init(void* device_handle);
  void HardwareInit();
  bool HardwareReset();
  // Kills the context of the batch currently being executed.
  void KillCurrentContext();
  // Moves pending batches to the backlog and resets the hardware and driver state.
  void Reset();
  void DisableInterrupts();

  // Appends the formatted string constructed from |fmt| and var args to |dump_out|.
  void OutputFormattedString(std::vector<std::string>* dump_out, const char* fmt, ...);
  // Populates |dump_out| with a formatted representation of |dump_state|.
  void FormatDump(DumpState* dump_state, std::vector<std::string>* dump_out);
  // Populates |dump_out| with a formatted representation of |buf|, starting from |start_dword|
  // for |dword_count| number of elements, wrapping around if it reaches the end of the buffer.
  // The element corresponding to |active_head_dword| will be specially annotated.
  void DumpDecodedBuffer(std::vector<std::string>* dump_out, uint32_t* buf,
                         uint32_t buf_size_dwords, uint32_t start_dword, uint32_t dword_count,
                         uint32_t active_head_dword);

  void StartDeviceThread();
  int DeviceThreadLoop();
  void EnqueueDeviceRequest(std::unique_ptr<DeviceRequest> request);

  int InterruptThreadLoop();
  magma::Status ProcessInterrupt();
  magma::Status ProcessDumpStatusToLog();
  void ProcessRequestBacklog();

  // Events for triggering interrupts.
  // If |free_on_complete| is true, the event will be freed automatically after the corresponding
  // interrupt is received.
  bool AllocInterruptEvent(bool free_on_complete, uint32_t* out_event_id);
  bool FreeInterruptEvent(uint32_t event_id);
  // Writes a new interrupt event to the end of the ringbuffer. The event must have been allocated
  // using |AllocInterruptEvent|.
  bool WriteInterruptEvent(uint32_t event_id, std::unique_ptr<MappedBatch> mapped_batch,
                           std::shared_ptr<AddressSpace> address_space);
  bool CompleteInterruptEvent(uint32_t event_id);

  bool MapRingbuffer(std::shared_ptr<MsdVsiContext> context);

  // Returns true if starting the ringbuffer succeeded, or the ringbuffer was already running.
  bool StartRingbuffer(std::shared_ptr<MsdVsiContext> context);
  // Adds a WAIT-LINK to the end of the ringbuffer.
  bool AddRingbufferWaitLink();
  // Modifies the last WAIT in the ringbuffer to link to |gpu_addr|.
  // |wait_link_offset| is the offset into the ringbuffer of the WAIT-LINK to replace.
  // |dest_prefetch| is the prefetch of the buffer we are linking to.
  void LinkRingbuffer(uint32_t wait_link_offset, uint32_t gpu_addr, uint32_t dest_prefetch);

  // Writes a LINK command at the end of the given buffer.
  bool WriteLinkCommand(magma::PlatformBuffer* buf, uint32_t write_offset, uint16_t prefetch,
                        uint32_t link_addr);

  // Returns whether the device became idle before |timeout_ms| elapsed.
  bool WaitUntilIdle(uint32_t timeout_ms);
  bool LoadInitialAddressSpace(std::shared_ptr<MsdVsiContext> context,
                               uint32_t address_space_index);

  // If |prefetch_out| is not null, it will be populated with the prefetch that was submitted
  // to the device.
  bool SubmitCommandBufferNoMmu(uint64_t bus_addr, uint32_t length,
                                uint16_t* prefetch_out = nullptr);

  // If address space of |context| is not the same as |configured_address_space|,
  // the hardware will be configured with the new address space.
  bool SubmitFlushTlb(std::shared_ptr<MsdVsiContext> context);

  bool SubmitCommandBuffer(std::shared_ptr<MsdVsiContext> context, uint32_t address_space_index,
                           bool do_flush, std::unique_ptr<MappedBatch> mapped_batch,
                           uint32_t event_id);

  magma::Status ProcessBatch(std::unique_ptr<MappedBatch> batch, bool do_flush);

  magma::RegisterIo* register_io() { return register_io_.get(); }

  // AddressSpace::Owner
  magma::PlatformBusMapper* GetBusMapper() override { return bus_mapper_.get(); }

  void AddressSpaceReleased(AddressSpace* address_space) override {
    // Free is thread safe.
    page_table_slot_allocator_->Free(address_space->page_table_array_slot());
  }

  // MsdVsiConnection::Owner
  Ringbuffer* GetRingbuffer() override { return ringbuffer_.get(); }

  // If |do_flush| is true, a flush TLB command will be queued before the batch commands.
  magma::Status SubmitBatch(std::unique_ptr<MappedBatch> batch, bool do_flush) override;

  PageTableArrays* page_table_arrays() { return page_table_arrays_.get(); }

  static const uint32_t kMagic = 0x64657669;  //"devi"

  std::unique_ptr<MsdVsiPlatformDevice> platform_device_;
  std::unique_ptr<magma::RegisterIo> register_io_;
  std::unique_ptr<magma::PlatformBuffer> external_sram_;
  std::unique_ptr<GpuFeatures> gpu_features_;
  uint32_t device_id_ = 0;
  uint32_t revision_ = 0;
  std::unique_ptr<magma::PlatformBusMapper> bus_mapper_;
  std::unique_ptr<PageTableArrays> page_table_arrays_;
  std::unique_ptr<PageTableSlotAllocator> page_table_slot_allocator_;

  // The command queue.
  std::unique_ptr<Ringbuffer> ringbuffer_;
  // This holds the address space that the hardware would be configured with at the current point
  // in the ringbuffer. If a client's address_space differs from |configured_address_space_|,
  // |SubmitFlushTlb| will write the commands for loading the client's address space and flushing
  // the TLB prior to linking to the new command buffer.
  std::shared_ptr<AddressSpace> configured_address_space_;
  // The context of the last command buffer that was linked to the ringbuffer to be executed.
  std::weak_ptr<MsdVsiContext> prev_executed_context_;

  std::thread interrupt_thread_;
  std::unique_ptr<magma::PlatformInterrupt> interrupt_;
  std::atomic_uint64_t last_interrupt_timestamp_;
  std::atomic_bool stop_interrupt_thread_{false};

  std::thread device_thread_;
  std::unique_ptr<magma::PlatformThreadId> device_thread_id_;
  std::atomic_bool stop_device_thread_{false};

  std::unique_ptr<Sequencer> sequencer_;
  std::unique_ptr<GpuProgress> progress_;

  class BatchRequest;
  class DumpRequest;
  class InterruptRequest;
  class MappingReleaseRequest;

  // Thread-shared data members
  std::unique_ptr<magma::PlatformSemaphore> device_request_semaphore_;
  std::mutex device_request_mutex_;
  std::list<std::unique_ptr<DeviceRequest>> device_request_list_;

  struct DeferredRequest {
    std::unique_ptr<MappedBatch> batch;
    bool do_flush;
  };

  std::list<DeferredRequest> request_backlog_;

  Event events_[kNumEvents] = {};

  // For testing and debugging purposes.
  uint32_t num_events_completed_ = 0;

  friend class TestMsdVsiDevice;
  friend class TestCommandBuffer;
  friend class TestDeviceDump_DumpBasic_Test;
  friend class TestDeviceDump_DumpCommandBuffer_Test;
  friend class TestDeviceDump_DumpCommandBufferMultipleResources_Test;
  friend class TestDeviceDump_DumpCommandBufferWithFault_Test;
  friend class TestDeviceDump_DumpDecodedBuffer_Test;
  friend class TestDeviceDump_DumpEventBatch_Test;
  friend class TestDeviceDump_DumpRingbufferWithWraparound_Test;
  friend class TestExec;
  friend class TestExec_Backlog_Test;
  friend class TestExec_BacklogWithInvalidBatch_Test;
  friend class TestExec_ReuseGpuAddress_Test;
  friend class TestExec_SubmitContextStateBufferMultipleAddressSpaces_Test;
  friend class TestExec_SubmitContextStateBufferMultipleContexts_Test;
  friend class TestExec_SubmitContextStateBufferSameContext_Test;
  friend class TestExec_SubmitBatchWithOffset_Test;
  friend class TestExec_SubmitBatchesMultipleContexts_Test;
  friend class TestExec_SubmitEventBeforeContextStateBuffer_Test;
  friend class TestExec_SwitchAddressSpace_Test;
  friend class TestExec_SwitchMultipleAddressSpaces_Test;
  friend class TestExec_ResetAfterSubmit_Test;
  friend class TestEvents;
  friend class TestEvents_AllocAndFree_Test;
  friend class TestEvents_Submit_Test;
  friend class TestEvents_WriteSameEvent_Test;
  friend class TestEvents_WriteUnorderedEventIds_Test;
  friend class TestFaultRecovery_ManyBatches_Test;
  friend class TestFaultRecovery_MultipleContexts_Test;
  friend class MsdVsiDeviceTest_FetchEngineDma_Test;
  friend class MsdVsiDeviceTest_LoadAddressSpace_Test;
  friend class MsdVsiDeviceTest_RingbufferCanHoldMaxEvents_Test;
  friend class MsdVsiDeviceTest_PulseEater_Test;
  friend class MsdVsiDeviceTest_Reset_Test;
  friend class MsdVsiDeviceTest_Shutdown_Test;
};

#endif  // MSD_VSI_DEVICE_H
