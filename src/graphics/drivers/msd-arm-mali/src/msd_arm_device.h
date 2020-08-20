// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MSD_ARM_DEVICE_H
#define MSD_ARM_DEVICE_H

#include <zircon/compiler.h>

#include <deque>
#include <list>
#include <mutex>
#include <thread>
#include <vector>

#include "address_manager.h"
#include "device_request.h"
#include "gpu_features.h"
#include "job_scheduler.h"
#include "magma_util/macros.h"
#include "magma_util/register_io.h"
#include "magma_util/thread.h"
#include "msd.h"
#include "msd_arm_connection.h"
#include "performance_counters.h"
#include "platform_device.h"
#include "platform_interrupt.h"
#include "platform_semaphore.h"
#include "power_manager.h"

class MsdArmDevice : public msd_device_t,
                     public JobScheduler::Owner,
                     public MsdArmConnection::Owner,
                     public AddressManager::Owner,
                     public PerformanceCounters::Owner {
 public:
  // Creates a device for the given |device_handle| and returns ownership.
  // If |start_device_thread| is false, then StartDeviceThread should be called
  // to enable device request processing.
  static std::unique_ptr<MsdArmDevice> Create(void* device_handle, bool start_device_thread);

  MsdArmDevice();

  virtual ~MsdArmDevice();

  static MsdArmDevice* cast(msd_device_t* dev) {
    DASSERT(dev);
    DASSERT(dev->magic_ == kMagic);
    return static_cast<MsdArmDevice*>(dev);
  }

  bool Init(void* device_handle);
  std::shared_ptr<MsdArmConnection> Open(msd_client_id_t client_id);

  struct DumpState {
    struct CorePowerState {
      const char* core_type;
      const char* status_type;
      uint64_t bitmask;
    };
    std::vector<CorePowerState> power_states;
    // Only accounts for recent past.
    uint64_t total_time_ms;
    uint64_t active_time_ms;

    uint32_t gpu_fault_status;
    uint64_t gpu_fault_address;
    uint32_t gpu_status;
    uint64_t cycle_count;
    uint64_t timestamp;

    uint32_t gpu_irq_rawstat;
    uint32_t gpu_irq_status;
    uint32_t gpu_irq_mask;
    bool handling_gpu_interrupt{};
    uint64_t gpu_interrupt_delay{};
    uint64_t gpu_interrupt_time{};

    uint32_t job_irq_rawstat;
    uint32_t job_irq_status;
    uint32_t job_irq_mask;
    uint32_t job_irq_js_state;
    bool handling_job_interrupt{};
    uint64_t job_interrupt_delay{};
    uint64_t job_interrupt_time{};
    uint64_t job_interrupt_time_processed{};

    uint32_t mmu_irq_rawstat;
    uint32_t mmu_irq_status;
    uint32_t mmu_irq_mask;
    bool handling_mmu_interrupt{};
    uint64_t mmu_interrupt_delay{};
    uint64_t mmu_interrupt_time{};

    struct JobSlotStatus {
      uint32_t status;
      uint64_t head;
      uint64_t tail;
      uint32_t config;
    };

    std::vector<JobSlotStatus> job_slot_status;
    struct AddressSpaceStatus {
      uint32_t status;
      uint32_t fault_status;
      uint64_t fault_address;
    };
    std::vector<AddressSpaceStatus> address_space_status;
  };
  static void DumpRegisters(const GpuFeatures& features, magma::RegisterIo* io,
                            DumpState* dump_state);
  void Dump(DumpState* dump_state, bool from_device_thread);
  void DumpToString(std::vector<std::string>* dump_string, bool from_device_thread);
  void FormatDump(DumpState& dump_state, std::vector<std::string>* dump_string);
  void DumpStatusToLog();

  // MsdArmConnection::Owner implementation.
  void ScheduleAtom(std::shared_ptr<MsdArmAtom> atom) override;
  void CancelAtoms(std::shared_ptr<MsdArmConnection> connection) override;
  AddressSpaceObserver* GetAddressSpaceObserver() override { return address_manager_.get(); }
  ArmMaliCacheCoherencyStatus cache_coherency_status() override { return cache_coherency_status_; }
  magma::PlatformBusMapper* GetBusMapper() override { return bus_mapper_.get(); }
  bool IsProtectedModeSupported() override;
  void DeregisterConnection() override;
  void SetCurrentThreadToDefaultPriority() override;
  PerformanceCounters* performance_counters() override { return perf_counters_.get(); }
  std::shared_ptr<DeviceRequest::Reply> RunTaskOnDeviceThread(FitCallbackTask task) override;

  magma_status_t QueryInfo(uint64_t id, uint64_t* value_out);
  magma_status_t QueryReturnsBuffer(uint64_t id, uint32_t* buffer_out);

  // PerformanceCounters::Owner implementation.
  AddressManager* address_manager() override { return address_manager_.get(); }
  MsdArmConnection::Owner* connection_owner() override { return this; }

 private:
#define CHECK_THREAD_IS_CURRENT(x) \
  if (x)                           \
  DASSERT(magma::ThreadIdCheck::IsCurrent(*x))

#define CHECK_THREAD_NOT_CURRENT(x) \
  if (x)                            \
  DASSERT(!magma::ThreadIdCheck::IsCurrent(*x))

  friend class TestMsdArmDevice;

  class DumpRequest;
  class PerfCounterSampleCompletedRequest;
  class JobInterruptRequest;
  class MmuInterruptRequest;
  class ScheduleAtomRequest;
  class CancelAtomsRequest;
  class TaskRequest;

  magma::RegisterIo* register_io() override {
    DASSERT(register_io_);
    return register_io_.get();
  }

  void set_register_io(std::unique_ptr<magma::RegisterIo> register_io) {
    register_io_ = std::move(register_io);
  }

  void Destroy();
  void StartDeviceThread();
  int DeviceThreadLoop();
  int GpuInterruptThreadLoop();
  int JobInterruptThreadLoop();
  int MmuInterruptThreadLoop();
  bool InitializeInterrupts();
  void EnableInterrupts();
  void DisableInterrupts();
  bool InitializeHardware();
  void EnqueueDeviceRequest(std::unique_ptr<DeviceRequest> request, bool enqueue_front = false);
  static void InitializeHardwareQuirks(GpuFeatures* features, magma::RegisterIo* registers);
  bool PowerDownL2();
  bool PowerDownShaders();
  bool ResetDevice();

  magma::Status ProcessDumpStatusToLog();
  magma::Status ProcessPerfCounterSampleCompleted();
  magma::Status ProcessJobInterrupt(uint64_t time);
  magma::Status ProcessMmuInterrupt();
  magma::Status ProcessScheduleAtoms();
  magma::Status ProcessCancelAtoms(std::weak_ptr<MsdArmConnection> connection);

  void ExecuteAtomOnDevice(MsdArmAtom* atom, magma::RegisterIo* registers);

  // JobScheduler::Owner implementation.
  void RunAtom(MsdArmAtom* atom) override;
  void AtomCompleted(MsdArmAtom* atom, ArmMaliResultCode result) override;
  void HardStopAtom(MsdArmAtom* atom) override;
  void SoftStopAtom(MsdArmAtom* atom) override;
  void ReleaseMappingsForAtom(MsdArmAtom* atom) override;
  magma::PlatformPort* GetPlatformPort() override;
  void UpdateGpuActive(bool active) override;
  void EnterProtectedMode() override;
  bool ExitProtectedMode() override;
  bool IsInProtectedMode() override;
  void OutputHangMessage() override;

  static const uint32_t kMagic = 0x64657669;  //"devi"

  std::thread device_thread_;
  std::unique_ptr<magma::PlatformThreadId> device_thread_id_;
  std::atomic_bool device_thread_quit_flag_{false};

  std::atomic_bool interrupt_thread_quit_flag_{false};
  std::thread gpu_interrupt_thread_;
  std::thread job_interrupt_thread_;
  std::thread mmu_interrupt_thread_;

  std::atomic_bool handling_job_interrupt_;
  std::atomic_bool handling_gpu_interrupt_;
  std::atomic_bool handling_mmu_interrupt_;
  std::atomic<uint64_t> job_interrupt_delay_{};
  std::atomic<uint64_t> gpu_interrupt_delay_{};
  std::atomic<uint64_t> mmu_interrupt_delay_{};
  std::atomic<uint64_t> job_interrupt_time_{};
  std::atomic<uint64_t> gpu_interrupt_time_{};
  std::atomic<uint64_t> mmu_interrupt_time_{};
  uint64_t job_interrupt_time_processed_ = {};

  std::unique_ptr<magma::PlatformSemaphore> device_request_semaphore_;
  std::unique_ptr<magma::PlatformPort> device_port_;
  std::mutex device_request_mutex_;
  std::list<std::unique_ptr<DeviceRequest>> device_request_list_;

  // Triggered on device reset.
  std::unique_ptr<magma::PlatformSemaphore> reset_semaphore_;

  std::mutex schedule_mutex_;
  __TA_GUARDED(schedule_mutex_) std::vector<std::shared_ptr<MsdArmAtom>> atoms_to_schedule_;

  std::unique_ptr<magma::PlatformDevice> platform_device_;
  std::unique_ptr<magma::RegisterIo> register_io_;
  std::unique_ptr<magma::PlatformInterrupt> gpu_interrupt_;
  std::unique_ptr<magma::PlatformInterrupt> job_interrupt_;
  std::unique_ptr<magma::PlatformInterrupt> mmu_interrupt_;

  std::unique_ptr<magma::PlatformHandle> default_profile_;

  GpuFeatures gpu_features_;
  ArmMaliCacheCoherencyStatus cache_coherency_status_ = kArmMaliCacheCoherencyNone;

  std::unique_ptr<PowerManager> power_manager_;
  std::unique_ptr<AddressManager> address_manager_;
  std::unique_ptr<JobScheduler> scheduler_;
  std::unique_ptr<magma::PlatformBusMapper> bus_mapper_;
  uint64_t cycle_counter_refcount_ = 0;

  std::unique_ptr<PerformanceCounters> perf_counters_;

  std::mutex connection_list_mutex_;
  MAGMA_GUARDED(connection_list_mutex_)
  std::vector<std::weak_ptr<MsdArmConnection>> connection_list_;

  bool use_status2_ = false;
};

#endif  // MSD_ARM_DEVICE_H
