// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MSD_VSL_DEVICE_H
#define MSD_VSL_DEVICE_H

#include <list>
#include <memory>
#include <mutex>
#include <thread>

#include "device_request.h"
#include "gpu_features.h"
#include "magma_util/macros.h"
#include "magma_util/register_io.h"
#include "magma_util/thread.h"
#include "magma_vsl_gc_types.h"
#include "msd.h"
#include "msd_vsl_connection.h"
#include "page_table_arrays.h"
#include "page_table_slot_allocator.h"
#include "platform_bus_mapper.h"
#include "platform_device.h"
#include "platform_semaphore.h"
#include "ringbuffer.h"

class MsdVslDevice : public msd_device_t, public MsdVslConnection::Owner {
 public:
  using DeviceRequest = DeviceRequest<MsdVslDevice>;

  // Creates a device for the given |device_handle| and returns ownership.
  // If |start_device_thread| is false, then StartDeviceThread should be called
  // to enable device request processing.
  static std::unique_ptr<MsdVslDevice> Create(void* device_handle, bool start_device_thread);

  MsdVslDevice() { magic_ = kMagic; }

  virtual ~MsdVslDevice();

  uint32_t device_id() { return device_id_; }

  bool IsIdle();
  bool StopRingbuffer();

  std::unique_ptr<MsdVslConnection> Open(msd_client_id_t client_id);

  magma_status_t ChipIdentity(magma_vsl_gc_chip_identity* out_identity);
  magma_status_t ChipOption(magma_vsl_gc_chip_option* out_option);

  static MsdVslDevice* cast(msd_device_t* dev) {
    DASSERT(dev);
    DASSERT(dev->magic_ == kMagic);
    return static_cast<MsdVslDevice*>(dev);
  }

 private:
  // The hardware provides 30 bits for interrupt events and 2 bits for errors.
  static constexpr uint32_t kNumEvents = 30;
  struct Event {
    bool allocated = false;
    bool submitted = false;
    // TODO(fxb/43238): this should link to the command buffer which stores the semaphores.
    std::shared_ptr<magma::PlatformSemaphore> signal;
  };

#define CHECK_THREAD_IS_CURRENT(x) \
  if (x)                           \
  DASSERT(magma::ThreadIdCheck::IsCurrent(*x))

#define CHECK_THREAD_NOT_CURRENT(x) \
  if (x)                            \
  DASSERT(!magma::ThreadIdCheck::IsCurrent(*x))

  bool Init(void* device_handle);
  bool HardwareInit();
  void Reset();
  void DisableInterrupts();

  void StartDeviceThread();
  int DeviceThreadLoop();
  void EnqueueDeviceRequest(std::unique_ptr<DeviceRequest> request);

  int InterruptThreadLoop();
  magma::Status ProcessInterrupt();

  // Events for triggering interrupts.
  bool AllocInterruptEvent(uint32_t* out_event_id);
  bool FreeInterruptEvent(uint32_t event_id);
  // Writes a new interrupt event to the end of the ringbuffer. The event must have been allocated
  // using |AllocInterruptEvent|.
  bool WriteInterruptEvent(uint32_t event_id, std::shared_ptr<magma::PlatformSemaphore> signal);
  bool CompleteInterruptEvent(uint32_t event_id);

  // Returns true if starting the ringbuffer succeeded, or the ringbuffer was already running.
  bool StartRingbuffer(std::shared_ptr<AddressSpace> address_space);
  // Adds a WAIT-LINK to the end of the ringbuffer.
  bool AddRingbufferWaitLink();
  // Modifies the last WAIT in the ringbuffer to link to |gpu_addr|.
  // |wait_link_offset| is the offset into the ringbuffer of the WAIT-LINK to replace.
  // |dest_prefetch| is the prefetch of the buffer we are linking to.
  bool LinkRingbuffer(uint32_t wait_link_offset, uint32_t gpu_addr, uint32_t dest_prefetch);

  // Writes a LINK command at the end of the given buffer.
  bool WriteLinkCommand(magma::PlatformBuffer* buf, uint32_t length,
                        uint16_t prefetch, uint32_t link_addr);

  // Returns whether the device became idle before |timeout_ms| elapsed.
  bool WaitUntilIdle(uint32_t timeout_ms);
  bool LoadInitialAddressSpace(std::shared_ptr<AddressSpace>, uint32_t address_space_index);

  // If |prefetch_out| is not null, it will be populated with the prefetch that was submitted
  // to the device.
  bool SubmitCommandBufferNoMmu(uint64_t bus_addr, uint32_t length,
                                uint16_t* prefetch_out = nullptr);
  bool SubmitCommandBuffer(std::shared_ptr<AddressSpace>, uint32_t address_space_index,
                           magma::PlatformBuffer* buf, uint32_t gpu_addr, uint32_t length,
                           uint32_t event_id, std::shared_ptr<magma::PlatformSemaphore> signal,
                           uint16_t* prefetch_out);

  magma::RegisterIo* register_io() { return register_io_.get(); }

  // MsdVslConnection::Owner
  magma::PlatformBusMapper* GetBusMapper() override { return bus_mapper_.get(); }

  void ConnectionReleased(MsdVslConnection* connection) override {
    page_table_slot_allocator_->Free(connection->page_table_array_slot());
  }

  PageTableArrays* page_table_arrays() { return page_table_arrays_.get(); }

  static const uint32_t kMagic = 0x64657669;  //"devi"

  std::unique_ptr<magma::PlatformDevice> platform_device_;
  std::unique_ptr<magma::RegisterIo> register_io_;
  std::unique_ptr<GpuFeatures> gpu_features_;
  uint32_t device_id_ = 0;
  std::unique_ptr<magma::PlatformBusMapper> bus_mapper_;
  std::unique_ptr<PageTableArrays> page_table_arrays_;
  std::unique_ptr<PageTableSlotAllocator> page_table_slot_allocator_;

  // The command queue.
  std::unique_ptr<Ringbuffer> ringbuffer_;

  std::thread interrupt_thread_;
  std::unique_ptr<magma::PlatformInterrupt> interrupt_;
  std::atomic_bool stop_interrupt_thread_{false};

  std::thread device_thread_;
  std::unique_ptr<magma::PlatformThreadId> device_thread_id_;
  std::atomic_bool stop_device_thread_{false};

  class InterruptRequest;

  // Thread-shared data members
  std::unique_ptr<magma::PlatformSemaphore> device_request_semaphore_;
  std::mutex device_request_mutex_;
  std::list<std::unique_ptr<DeviceRequest>> device_request_list_;

  MAGMA_GUARDED(events_mutex_) Event events_[kNumEvents] = {};
  // TODO(fxb/43235): this can be removed once we process events on the device thread.
  std::mutex events_mutex_;

  friend class TestMsdVslDevice;
  friend class TestEvents;
  friend class TestEvents_AllocAndFree_Test;
  friend class TestEvents_Submit_Test;
  friend class TestEvents_Write_Test;
  friend class MsdVslDeviceTest_FetchEngineDma_Test;
  friend class MsdVslDeviceTest_LoadAddressSpace_Test;
};

#endif  // MSD_VSL_DEVICE_H
