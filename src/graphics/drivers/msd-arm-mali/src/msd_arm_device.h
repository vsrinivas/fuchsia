// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MSD_ARM_DEVICE_H
#define MSD_ARM_DEVICE_H

#include <fuchsia/hardware/gpu/mali/cpp/banjo.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/inspect/cpp/inspect.h>
#include <zircon/compiler.h>

#include <deque>
#include <list>
#include <mutex>
#include <thread>
#include <vector>

#include "magma_util/macros.h"
#include "magma_util/register_io.h"
#include "magma_util/thread.h"
#include "mali_register_io.h"
#include "msd.h"
#include "msd_defs.h"
#include "platform_device.h"
#include "platform_interrupt.h"
#include "platform_semaphore.h"
#include "src/graphics/drivers/msd-arm-mali/src/address_manager.h"
#include "src/graphics/drivers/msd-arm-mali/src/device_request.h"
#include "src/graphics/drivers/msd-arm-mali/src/gpu_features.h"
#include "src/graphics/drivers/msd-arm-mali/src/job_scheduler.h"
#include "src/graphics/drivers/msd-arm-mali/src/msd_arm_connection.h"
#include "src/graphics/drivers/msd-arm-mali/src/performance_counters.h"
#include "src/graphics/drivers/msd-arm-mali/src/power_manager.h"

class MsdArmDevice : public msd_device_t,
                     public JobScheduler::Owner,
                     public MsdArmConnection::Owner,
                     public AddressManager::Owner,
                     public PerformanceCounters::Owner {
 public:
  // Creates a device for the given |device_handle| and returns ownership.
  // If |start_device_thread| is false, then StartDeviceThread should be called
  // to enable device request processing.
  static std::unique_ptr<MsdArmDevice> Create(void* device_handle, bool start_device_thread,
                                              inspect::Node* parent_node = nullptr);

  MsdArmDevice();

  virtual ~MsdArmDevice();

  static MsdArmDevice* cast(msd_device_t* dev) {
    DASSERT(dev);
    DASSERT(dev->magic_ == kMagic);
    return static_cast<MsdArmDevice*>(dev);
  }

  void set_inspect(inspect::Node node) { inspect_ = std::move(node); }

  bool Init(void* device_handle);
  bool Init(std::unique_ptr<magma::PlatformDevice> platform_device,
            std::unique_ptr<magma::PlatformBusMapper> bus_mapper);

  std::shared_ptr<MsdArmConnection> Open(msd_client_id_t client_id);

  uint64_t GpuId() { return gpu_features_.gpu_id.reg_value(); }

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
  static void DumpRegisters(const GpuFeatures& features, mali::RegisterIo* io,
                            DumpState* dump_state);
  void Dump(DumpState* dump_state, bool from_device_thread);
  void DumpToString(std::vector<std::string>* dump_string, bool from_device_thread);
  void FormatDump(DumpState& dump_state, std::vector<std::string>* dump_string);
  void DumpStatusToLog();
  magma::Status ProcessTimestampRequest(std::shared_ptr<magma::PlatformBuffer> buffer);

  void RefCycleCounter();
  void DerefCycleCounter();

  magma::Status QueryTimestamp(std::unique_ptr<magma::PlatformBuffer> buffer);

  void SetMemoryPressureLevel(MagmaMemoryPressureLevel level);

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
  std::thread::id GetDeviceThreadId() override { return device_thread_.get_id(); }
  MagmaMemoryPressureLevel GetCurrentMemoryPressureLevel() override {
    std::lock_guard lock(connection_list_mutex_);
    return current_memory_pressure_level_;
  }

  magma_status_t QueryInfo(uint64_t id, uint64_t* value_out);
  magma_status_t QueryReturnsBuffer(uint64_t id, uint32_t* buffer_out);

  // PerformanceCounters::Owner implementation.
  AddressManager* address_manager() override { return address_manager_.get(); }
  MsdArmConnection::Owner* connection_owner() override { return this; }

  // Used for testing - allows the driver to assume reset happened without an interrupt.
  void set_assume_reset_happened(bool assume) { assume_reset_happened_ = assume; }

 private:
#define CHECK_THREAD_IS_CURRENT(x) \
  if (x)                           \
  DASSERT(magma::ThreadIdCheck::IsCurrent(*x))

#define CHECK_THREAD_NOT_CURRENT(x) \
  if (x)                            \
  DASSERT(!magma::ThreadIdCheck::IsCurrent(*x))

  friend class TestMsdArmDevice;
  friend class TestNonHardwareMsdArmDevice;

  class DumpRequest;
  class PerfCounterSampleCompletedRequest;
  class JobInterruptRequest;
  class MmuInterruptRequest;
  class ScheduleAtomRequest;
  class CancelAtomsRequest;
  class TaskRequest;
  class TimestampRequest;

  struct InspectEvent {
    InspectEvent(inspect::Node* parent, std::string type);

    inspect::Node node;
  };

  mali::RegisterIo* register_io() override {
    DASSERT(register_io_);
    return register_io_.get();
  }

  void set_register_io(std::unique_ptr<mali::RegisterIo> register_io) {
    register_io_ = std::move(register_io);
  }

  void Destroy();
  void StartGpuInterruptThread();
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
  static void InitializeHardwareQuirks(GpuFeatures* features, mali::RegisterIo* registers);
  bool PowerDownL2();
  bool PowerDownShaders();
  bool FlushL2();
  bool ResetDevice();
  void InitInspect();
  void UpdateProtectedModeSupported();
  void AppendInspectEvent(InspectEvent event);
  // Power on all GPU cores.
  void EnableAllCores();
  void HandleResetInterrupt();

  magma::Status ProcessDumpStatusToLog();
  magma::Status ProcessPerfCounterSampleCompleted();
  magma::Status ProcessJobInterrupt(uint64_t time);
  magma::Status ProcessMmuInterrupt();
  magma::Status ProcessScheduleAtoms();
  magma::Status ProcessCancelAtoms(std::weak_ptr<MsdArmConnection> connection);
  // Called periodically when in a critical memory state to force all contexts to clear JIT memory.
  // If |force_instant| is true, this callback was called directly from a change in the critical
  // memory pressure state.
  void PeriodicCriticalMemoryPressureCallback(bool force_instant);

  void ExecuteAtomOnDevice(MsdArmAtom* atom, mali::RegisterIo* registers);

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
  void OutputHangMessage(bool hardware_hang) override;

  static const uint32_t kMagic = 0x64657669;  //"devi"

  inspect::Node inspect_;
  inspect::Node events_;

  inspect::UintProperty hang_timeout_count_;
  inspect::UintProperty last_hang_timeout_ns_;
  inspect::UintProperty semaphore_hang_timeout_count_;
  inspect::UintProperty last_semaphore_hang_timeout_ns_;
  inspect::BoolProperty protected_mode_supported_property_;
  inspect::UintProperty memory_pressure_level_property_;

  std::mutex inspect_events_mutex_;
  MAGMA_GUARDED(inspect_events_mutex_) std::deque<InspectEvent> inspect_events_;

  ddk::ArmMaliProtocolClient mali_protocol_client_;
  // Flag is set to true if reset completion should trigger FinishExitProtectedMode.
  std::atomic_bool exiting_protected_mode_flag_{false};

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

  async::Loop loop_{&kAsyncLoopConfigNeverAttachToThread};

  std::unique_ptr<magma::PlatformSemaphore> device_request_semaphore_;
  std::unique_ptr<magma::PlatformPort> device_port_;
  std::mutex device_request_mutex_;
  std::list<std::unique_ptr<DeviceRequest>> device_request_list_;

  // Triggered on device reset.
  std::unique_ptr<magma::PlatformSemaphore> reset_semaphore_;
  bool assume_reset_happened_ = false;

  std::unique_ptr<magma::PlatformSemaphore> cache_clean_semaphore_;

  std::mutex schedule_mutex_;
  __TA_GUARDED(schedule_mutex_) std::vector<std::shared_ptr<MsdArmAtom>> atoms_to_schedule_;

  std::unique_ptr<magma::PlatformDevice> platform_device_;
  std::unique_ptr<mali::RegisterIo> register_io_;
  std::unique_ptr<magma::PlatformInterrupt> gpu_interrupt_;
  std::unique_ptr<magma::PlatformInterrupt> job_interrupt_;
  std::unique_ptr<magma::PlatformInterrupt> mmu_interrupt_;

  std::unique_ptr<magma::PlatformHandle> default_profile_;

  mali_properties_t mali_properties_{};
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
  MAGMA_GUARDED(connection_list_mutex_)
  MagmaMemoryPressureLevel current_memory_pressure_level_ = MAGMA_MEMORY_PRESSURE_LEVEL_NORMAL;
  MAGMA_GUARDED(connection_list_mutex_)
  uint32_t scheduled_memory_pressure_task_count_ = 0;
  MAGMA_GUARDED(connection_list_mutex_)
  zx::time next_scheduled_memory_pressure_task_time_{};
};

#endif  // MSD_ARM_DEVICE_H
