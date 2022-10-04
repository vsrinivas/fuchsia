// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/drivers/msd-arm-mali/src/msd_arm_device.h"

#include <lib/async/cpp/task.h>
#include <lib/fit/defer.h>

#include <bitset>
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <iterator>
#include <string>

#include <fbl/algorithm.h>
#include <fbl/string_printf.h>

#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include "msd_defs.h"
#include "platform_barriers.h"
#include "platform_logger.h"
#include "platform_port.h"
#include "platform_trace.h"
#include "src/graphics/drivers/msd-arm-mali/include/magma_vendor_queries.h"
#include "src/graphics/drivers/msd-arm-mali/src/job_scheduler.h"
#include "src/graphics/drivers/msd-arm-mali/src/registers.h"

using std::chrono_literals::operator""ms;
using std::chrono_literals::operator""us;

// This is the index into the mmio section of the mdi.
enum MmioIndex {
  kMmioIndexRegisters = 0,
};

enum InterruptIndex {
  kInterruptIndexJob = 0,
  kInterruptIndexMmu = 1,
  kInterruptIndexGpu = 2,
};

class MsdArmDevice::DumpRequest : public DeviceRequest {
 public:
  DumpRequest() {}

 protected:
  magma::Status Process(MsdArmDevice* device) override { return device->ProcessDumpStatusToLog(); }
};

class MsdArmDevice::PerfCounterSampleCompletedRequest : public DeviceRequest {
 public:
  PerfCounterSampleCompletedRequest() {}

 protected:
  magma::Status Process(MsdArmDevice* device) override {
    return device->ProcessPerfCounterSampleCompleted();
  }
};

class MsdArmDevice::JobInterruptRequest : public DeviceRequest {
 public:
  JobInterruptRequest(uint64_t time) : time_(time) {}

 protected:
  magma::Status Process(MsdArmDevice* device) override {
    return device->ProcessJobInterrupt(time_);
  }

 private:
  uint64_t time_;
};

class MsdArmDevice::MmuInterruptRequest : public DeviceRequest {
 public:
  MmuInterruptRequest() {}

 protected:
  magma::Status Process(MsdArmDevice* device) override { return device->ProcessMmuInterrupt(); }
};

class MsdArmDevice::ScheduleAtomRequest : public DeviceRequest {
 public:
  ScheduleAtomRequest() {}

 protected:
  magma::Status Process(MsdArmDevice* device) override { return device->ProcessScheduleAtoms(); }
};

class MsdArmDevice::CancelAtomsRequest : public DeviceRequest {
 public:
  CancelAtomsRequest(std::shared_ptr<MsdArmConnection> connection) : connection_(connection) {}

 protected:
  magma::Status Process(MsdArmDevice* device) override {
    return device->ProcessCancelAtoms(connection_);
  }

  std::weak_ptr<MsdArmConnection> connection_;
};

class MsdArmDevice::TaskRequest : public DeviceRequest {
 public:
  explicit TaskRequest(FitCallbackTask task) : task_(std::move(task)) {}

  magma::Status Process(MsdArmDevice* device) override {
    magma::Status status = task_(device);
    return status;
  }

 private:
  FitCallbackTask task_;
};

class MsdArmDevice::TimestampRequest : public DeviceRequest {
 public:
  TimestampRequest(std::shared_ptr<magma::PlatformBuffer> buffer) : buffer_(std::move(buffer)) {}

 protected:
  magma::Status Process(MsdArmDevice* device) override {
    return device->ProcessTimestampRequest(std::move(buffer_));
  }

  std::shared_ptr<magma::PlatformBuffer> buffer_;
};

//////////////////////////////////////////////////////////////////////////////////////////////////

std::unique_ptr<MsdArmDevice> MsdArmDevice::Create(void* device_handle, bool start_device_thread,
                                                   inspect::Node* parent_node) {
  auto device = std::make_unique<MsdArmDevice>();
  if (parent_node) {
    device->set_inspect(parent_node->CreateChild("device"));
  }

  if (!device->Init(device_handle))
    return DRETP(nullptr, "Failed to initialize MsdArmDevice");

  if (start_device_thread)
    device->StartDeviceThread();

  return device;
}

MsdArmDevice::MsdArmDevice() { magic_ = kMagic; }

MsdArmDevice::~MsdArmDevice() { Destroy(); }

void MsdArmDevice::Destroy() {
  DLOG("Destroy");
  CHECK_THREAD_NOT_CURRENT(device_thread_id_);

  loop_.Shutdown();

  DisableInterrupts();

  interrupt_thread_quit_flag_ = true;

  if (gpu_interrupt_)
    gpu_interrupt_->Signal();
  if (job_interrupt_)
    job_interrupt_->Signal();
  if (mmu_interrupt_)
    mmu_interrupt_->Signal();

  if (gpu_interrupt_thread_.joinable()) {
    DLOG("joining GPU interrupt thread");
    gpu_interrupt_thread_.join();
    DLOG("joined");
  }
  if (job_interrupt_thread_.joinable()) {
    DLOG("joining Job interrupt thread");
    job_interrupt_thread_.join();
    DLOG("joined");
  }
  if (mmu_interrupt_thread_.joinable()) {
    DLOG("joining MMU interrupt thread");
    mmu_interrupt_thread_.join();
    DLOG("joined");
  }
  device_thread_quit_flag_ = true;

  if (device_request_semaphore_)
    device_request_semaphore_->Signal();

  if (device_thread_.joinable()) {
    DLOG("joining device thread");
    device_thread_.join();
    DLOG("joined");
  }
}

bool MsdArmDevice::Init(void* device_handle) {
  DLOG("Init");
  auto platform_device = magma::PlatformDevice::Create(device_handle);
  if (!platform_device)
    return DRETF(false, "Failed to initialize device");
  auto bus_mapper = magma::PlatformBusMapper::Create(platform_device->GetBusTransactionInitiator());
  if (!bus_mapper)
    return DRETF(false, "Failed to create bus mapper");
  return Init(std::move(platform_device), std::move(bus_mapper));
}

bool MsdArmDevice::Init(std::unique_ptr<magma::PlatformDevice> platform_device,
                        std::unique_ptr<magma::PlatformBusMapper> bus_mapper) {
  DLOG("Init platform_device");
  zx_status_t status = loop_.StartThread("device-loop-thread");
  if (status != ZX_OK)
    return DRETF(false, "FAiled to create device loop thread");
  platform_device_ = std::move(platform_device);
  bus_mapper_ = std::move(bus_mapper);
  InitInspect();

  std::unique_ptr<magma::PlatformMmio> mmio = platform_device_->CpuMapMmio(
      kMmioIndexRegisters, magma::PlatformMmio::CACHE_POLICY_UNCACHED_DEVICE);
  if (!mmio)
    return DRETF(false, "failed to map registers");

  register_io_ = std::make_unique<mali::RegisterIo>(std::move(mmio));

  gpu_features_.ReadFrom(register_io_.get());
  gpu_features_.InitializeInspect(&inspect_);
  MAGMA_LOG(INFO, "ARM mali ID %x", gpu_features_.gpu_id.reg_value());

#if defined(MSD_ARM_ENABLE_CACHE_COHERENCY)
  if (gpu_features_.coherency_features.ace()) {
    cache_coherency_status_ = kArmMaliCacheCoherencyAce;
  } else {
    MAGMA_LOG(INFO, "Cache coherency unsupported");
  }
#endif

  arm_mali_protocol mali_proto;
  if (platform_device_->GetProtocol(ZX_PROTOCOL_ARM_MALI, &mali_proto)) {
    mali_protocol_client_ = ddk::ArmMaliProtocolClient(&mali_proto);
    DASSERT(mali_protocol_client_.is_valid());
    mali_protocol_client_.GetProperties(&mali_properties_);
  }

  UpdateProtectedModeSupported();

  reset_semaphore_ = magma::PlatformSemaphore::Create();
  cache_clean_semaphore_ = magma::PlatformSemaphore::Create();

  device_request_semaphore_ = magma::PlatformSemaphore::Create();
  device_port_ = magma::PlatformPort::Create();

  power_manager_ = std::make_unique<PowerManager>(register_io_.get());
  perf_counters_ = std::make_unique<PerformanceCounters>(this);
  perf_counters_->SetGpuFeatures(gpu_features_);
  scheduler_ = std::make_unique<JobScheduler>(this, 3);
  address_manager_ = std::make_unique<AddressManager>(this, gpu_features_.address_space_count);

  if (!InitializeInterrupts())
    return false;

  // Start interrupt thread so ResetDevice can wait for the reset interrupt.
  StartGpuInterruptThread();

  return ResetDevice();
}

void MsdArmDevice::StartGpuInterruptThread() {
  DASSERT(!gpu_interrupt_thread_.joinable());
  gpu_interrupt_thread_ = std::thread([this] { this->GpuInterruptThreadLoop(); });
}

void MsdArmDevice::InitInspect() {
  hang_timeout_count_ = inspect_.CreateUint("hang_timeout", 0);
  last_hang_timeout_ns_ = inspect_.CreateUint("last_hang_timeout_ns", 0);
  semaphore_hang_timeout_count_ = inspect_.CreateUint("semaphore_hang_timeout", 0);
  last_semaphore_hang_timeout_ns_ = inspect_.CreateUint("last_semaphore_hang_timeout_ns", 0);
  events_ = inspect_.CreateChild("events");
  protected_mode_supported_property_ = inspect_.CreateBool("protected_mode_supported", false);
  memory_pressure_level_property_ = inspect_.CreateUint("memory_pressure_level", 0);
}

void MsdArmDevice::UpdateProtectedModeSupported() {
  MAGMA_LOG(INFO, "Protected mode supported: %d", IsProtectedModeSupported());
  protected_mode_supported_property_.Set(IsProtectedModeSupported());
}

bool MsdArmDevice::InitializeHardware() {
  cycle_counter_refcount_ = 0;
  DASSERT(registers::GpuStatus::Get().ReadFrom(register_io_.get()).cycle_count_active() == 0);
  EnableInterrupts();
  InitializeHardwareQuirks(&gpu_features_, register_io_.get());
  EnableAllCores();
  return true;
}

void MsdArmDevice::EnableAllCores() {
  uint64_t enabled_cores = 1;
#if defined(MSD_ARM_ENABLE_ALL_CORES)
  enabled_cores = gpu_features_.shader_present;
#endif
  power_manager_->EnableCores(register_io_.get(), enabled_cores);
}

std::shared_ptr<MsdArmConnection> MsdArmDevice::Open(msd_client_id_t client_id) {
  auto connection = MsdArmConnection::Create(client_id, this);
  if (connection) {
    connection->InitializeInspectNode(&inspect_);
    std::lock_guard<std::mutex> lock(connection_list_mutex_);
    connection_list_.push_back(connection);
  }
  return connection;
}

void MsdArmDevice::DeregisterConnection() {
  std::lock_guard<std::mutex> lock(connection_list_mutex_);
  connection_list_.erase(std::remove_if(connection_list_.begin(), connection_list_.end(),
                                        [](auto& connection) { return connection.expired(); }),
                         connection_list_.end());
}

void MsdArmDevice::SetMemoryPressureLevel(MagmaMemoryPressureLevel level) {
  {
    std::lock_guard<std::mutex> lock(connection_list_mutex_);
    current_memory_pressure_level_ = level;
    memory_pressure_level_property_.Set(level);
  }

  if (level == MAGMA_MEMORY_PRESSURE_LEVEL_CRITICAL) {
    // Run instantly to free up memory as quickly as possible, even if another callback is already
    // scheduled.
    PeriodicCriticalMemoryPressureCallback(true);
  }
}

void MsdArmDevice::PeriodicCriticalMemoryPressureCallback(bool force_instant) {
  std::vector<std::weak_ptr<MsdArmConnection>> connection_list_copy;
  MagmaMemoryPressureLevel level;
  {
    std::lock_guard<std::mutex> lock(connection_list_mutex_);
    DASSERT(scheduled_memory_pressure_task_count_ >= 0);
    if (!force_instant) {
      DASSERT(scheduled_memory_pressure_task_count_ > 0);
      scheduled_memory_pressure_task_count_--;
    }
    connection_list_copy = connection_list_;
    level = current_memory_pressure_level_;
  }
  // connection_list_mutex_ must be unlocked here because PeriodicMemoryPressureCallback might
  // acquire it again.
  size_t released_size = 0;
  for (auto& connection : connection_list_copy) {
    auto locked = connection.lock();
    if (!locked)
      continue;
    released_size += locked->PeriodicMemoryPressureCallback();
  }

  if ((released_size > 0) && (level == MAGMA_MEMORY_PRESSURE_LEVEL_CRITICAL) && force_instant) {
    MAGMA_LOG(INFO, "Transitioned to critical, released %ld bytes", released_size);
  }
  {
    std::lock_guard<std::mutex> lock(connection_list_mutex_);
    if (current_memory_pressure_level_ == MAGMA_MEMORY_PRESSURE_LEVEL_CRITICAL &&
        !scheduled_memory_pressure_task_count_) {
      scheduled_memory_pressure_task_count_++;
      // 5 seconds is somewhat arbitrary. It's chosen to help clear out stale memory in a reasonable
      // time period, while not causing too much time to be wasted re-allocating hot JIT memory.
      constexpr uint32_t kPressureCallbackPeriodSeconds = 5;
      async::PostDelayedTask(
          loop_.dispatcher(), [this]() { PeriodicCriticalMemoryPressureCallback(false); },
          zx::sec(kPressureCallbackPeriodSeconds));
    }
  }
}

void MsdArmDevice::DumpStatusToLog() { EnqueueDeviceRequest(std::make_unique<DumpRequest>()); }

magma::Status MsdArmDevice::QueryTimestamp(std::unique_ptr<magma::PlatformBuffer> buffer) {
  auto request = std::make_unique<TimestampRequest>(std::move(buffer));
  auto reply = request->GetReply();

  EnqueueDeviceRequest(std::move(request));

  constexpr uint32_t kWaitTimeoutMs = 1000;
  magma::Status status = reply->Wait(kWaitTimeoutMs);
  if (!status.ok())
    return DRET_MSG(status.get(), "reply wait failed");

  return MAGMA_STATUS_OK;
}

static uint64_t get_ns_monotonic(bool raw) {
  struct timespec time;
  int ret = clock_gettime(raw ? CLOCK_MONOTONIC_RAW : CLOCK_MONOTONIC, &time);
  if (ret < 0)
    return 0;
  return static_cast<uint64_t>(time.tv_sec) * 1000000000ULL + time.tv_nsec;
}

magma::Status MsdArmDevice::ProcessTimestampRequest(std::shared_ptr<magma::PlatformBuffer> buffer) {
  magma_arm_mali_device_timestamp_return* return_struct;
  {
    void* ptr;
    if (!buffer->MapCpu(&ptr))
      return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "failed to map query buffer");
    return_struct = reinterpret_cast<magma_arm_mali_device_timestamp_return*>(ptr);
  }
  RefCycleCounter();
  return_struct->monotonic_raw_timestamp_before = get_ns_monotonic(true);
  return_struct->monotonic_timestamp = get_ns_monotonic(false);
  return_struct->device_timestamp =
      registers::Timestamp::Get().FromValue(0).ReadConsistentFrom(register_io_.get()).reg_value();
  return_struct->device_cycle_count =
      registers::CycleCount::Get().FromValue(0).ReadConsistentFrom(register_io_.get()).reg_value();
  return_struct->monotonic_raw_timestamp_after = get_ns_monotonic(true);
  DerefCycleCounter();

  buffer->UnmapCpu();

  return MAGMA_STATUS_OK;
}

void MsdArmDevice::OutputHangMessage(bool hardware_hang) {
  if (hardware_hang) {
    hang_timeout_count_.Add(1);
    last_hang_timeout_ns_.Set(magma::get_monotonic_ns());
  } else {
    semaphore_hang_timeout_count_.Add(1);
    last_semaphore_hang_timeout_ns_.Set(magma::get_monotonic_ns());
  }
  AppendInspectEvent(InspectEvent(&events_, hardware_hang ? "gpu_hang" : "semaphore_hang"));

  MAGMA_LOG(WARNING, "Possible %s hang", hardware_hang ? "GPU" : "semaphore");
  ProcessDumpStatusToLog();
}

int MsdArmDevice::DeviceThreadLoop() {
  magma::PlatformThreadHelper::SetCurrentThreadName("DeviceThread");

  device_thread_id_ = std::make_unique<magma::PlatformThreadId>();
  CHECK_THREAD_IS_CURRENT(device_thread_id_);

  DLOG("DeviceThreadLoop starting thread 0x%lx", device_thread_id_->id());

  const bool applied_role = magma::PlatformThreadHelper::SetRole(
      platform_device_->GetDeviceHandle(), "fuchsia.graphics.drivers.msd-arm-mali.device");
  if (!applied_role) {
    DLOG("Failed to get higher priority!");
  }

  std::unique_lock<std::mutex> lock(device_request_mutex_, std::defer_lock);
  device_request_semaphore_->WaitAsync(device_port_.get(), device_request_semaphore_->global_id());

  uint32_t timeout_count = 0;
  while (!device_thread_quit_flag_) {
    auto timeout_duration = scheduler_->GetCurrentTimeoutDuration();
    if (timeout_duration <= JobScheduler::Clock::duration::zero()) {
      // Don't timeout if the device request semaphore is signaled, because that could be a sign
      // that the current thread just took a really long time to wakeup.
      constexpr uint32_t kMaxConsecutiveTimeouts = 5;
      if (!device_request_semaphore_->WaitNoReset(0).ok() ||
          timeout_count >= kMaxConsecutiveTimeouts) {
        scheduler_->HandleTimedOutAtoms();
        timeout_count = 0;
        continue;
      }
      timeout_count++;
    }
    uint64_t key;
    magma::Status status(MAGMA_STATUS_OK);
    if (timeout_duration < JobScheduler::Clock::duration::max()) {
      // Add 1 to avoid rounding time down and spinning with timeouts close to 0.
      int64_t millisecond_timeout =
          std::chrono::duration_cast<std::chrono::milliseconds>(timeout_duration).count() + 1;
      status = device_port_->Wait(&key, millisecond_timeout);
    } else {
      status = device_port_->Wait(&key);
    }
    if (status.ok()) {
      timeout_count = 0;
      if (key == device_request_semaphore_->global_id()) {
        device_request_semaphore_->Reset();
        device_request_semaphore_->WaitAsync(device_port_.get(),
                                             device_request_semaphore_->global_id());
        while (!device_thread_quit_flag_) {
          lock.lock();
          if (!device_request_list_.size()) {
            lock.unlock();
            break;
          }
          auto request = std::move(device_request_list_.front());
          device_request_list_.pop_front();
          lock.unlock();
          request->ProcessAndReply(this);
        }
      } else {
        scheduler_->PlatformPortSignaled(key);
      }
    }
  }

  DLOG("DeviceThreadLoop exit");
  return 0;
}

void MsdArmDevice::HandleResetInterrupt() {
  DLOG("Received GPU reset completed");
  if (exiting_protected_mode_flag_) {
    exiting_protected_mode_flag_ = false;
    // Call Finish before clearing the irq register because the TEE requires the interrupt is
    // still set to prove that the reset happened.
    zx_status_t status = mali_protocol_client_.FinishExitProtectedMode();
    if (status != ZX_OK) {
      MAGMA_LOG(ERROR, "error from FinishExitProtectedMode: %d", status);
    }
  }
  reset_semaphore_->Signal();
}

int MsdArmDevice::GpuInterruptThreadLoop() {
  magma::PlatformThreadHelper::SetCurrentThreadName("Gpu InterruptThread");
  DLOG("GPU Interrupt thread started");

  const bool applied_role = magma::PlatformThreadHelper::SetRole(
      platform_device_->GetDeviceHandle(), "fuchsia.graphics.drivers.msd-arm-mali.gpu-interrupt");
  if (!applied_role) {
    DLOG("Failed to get higher priority!");
  }

  while (!interrupt_thread_quit_flag_) {
    DLOG("GPU waiting for interrupt");
    gpu_interrupt_->Wait();
    DLOG("GPU Returned from interrupt wait!");
    gpu_interrupt_delay_ = gpu_interrupt_->GetMicrosecondsSinceLastInterrupt();
    gpu_interrupt_time_ = magma::get_monotonic_ns();
    // Resets flag at end of loop iteration.
    handling_gpu_interrupt_ = true;
    auto cleanup = fit::defer([&]() { handling_gpu_interrupt_ = false; });

    if (interrupt_thread_quit_flag_)
      break;

    auto irq_status = registers::GpuIrqFlags::GetStatus().ReadFrom(register_io_.get());

    if (!irq_status.reg_value()) {
      MAGMA_LOG(WARNING, "Got unexpected GPU IRQ with no flags set");
    }

    auto clear_flags = registers::GpuIrqFlags::GetIrqClear().FromValue(irq_status.reg_value());
    // Handle interrupts on the interrupt thread so the device thread can wait for them to
    // complete.
    if (irq_status.reset_completed()) {
      HandleResetInterrupt();
      irq_status.set_reset_completed(0);
    }
    if (irq_status.power_changed_single() || irq_status.power_changed_all()) {
      irq_status.set_power_changed_single(0);
      irq_status.set_power_changed_all(0);
      power_manager_->ReceivedPowerInterrupt(register_io_.get());
      if (power_manager_->l2_ready_status() &&
          (cache_coherency_status_ == kArmMaliCacheCoherencyAce)) {
        auto enable_reg = registers::CoherencyFeatures::GetEnable().FromValue(0);
        enable_reg.set_ace(true);
        enable_reg.WriteTo(register_io_.get());
      }
    }

    if (irq_status.performance_counter_sample_completed()) {
      irq_status.set_performance_counter_sample_completed(0);
      EnqueueDeviceRequest(std::make_unique<PerfCounterSampleCompletedRequest>(), true);
      // Don't wait for a reply, to ensure there's no deadlock. Clearing the interrupt flag
      // before the interrupt is actually processed shouldn't matter, because perf_counters_
      // ensures only one request happens at a time.
    }

    if (irq_status.clean_caches_completed()) {
      irq_status.set_clean_caches_completed(0);
      cache_clean_semaphore_->Signal();
    }

    if (irq_status.reg_value()) {
      MAGMA_LOG(WARNING, "Got unexpected GPU IRQ %d", irq_status.reg_value());
      uint64_t fault_addr =
          registers::GpuFaultAddress::Get().ReadFrom(register_io_.get()).reg_value();
      {
        std::lock_guard<std::mutex> lock(connection_list_mutex_);
        for (auto& connection : connection_list_) {
          auto locked = connection.lock();
          if (locked) {
            uint64_t virtual_address;
            if (locked->GetVirtualAddressFromPhysical(fault_addr, &virtual_address))
              MAGMA_LOG(WARNING, "Client %lx has VA %lx mapped to PA %lx", locked->client_id(),
                        virtual_address, fault_addr);
          }
        }
      }

      // Perform the GPU dump immediately, because clearing the irq flags might cause another
      // GPU fault to be generated, which could overwrite the earlier data.
      std::vector<std::string> dump;
      DumpToString(&dump, false);
      MAGMA_LOG(INFO, "GPU fault status");
      for (auto& str : dump) {
        MAGMA_LOG(INFO, "%s", str.c_str());
      }
      InspectEvent event(&events_, "gpu_irq");
      event.node.RecordUint("irq", irq_status.reg_value());

      AppendInspectEvent(std::move(event));
    }

    if (clear_flags.reg_value()) {
      clear_flags.WriteTo(register_io_.get());
    }
  }

  DLOG("GPU Interrupt thread exited");
  return 0;
}

magma::Status MsdArmDevice::ProcessPerfCounterSampleCompleted() {
  DLOG("Perf Counter sample completed");

  perf_counters_->ReadCompleted();
  return MAGMA_STATUS_OK;
}

int MsdArmDevice::JobInterruptThreadLoop() {
  magma::PlatformThreadHelper::SetCurrentThreadName("Job InterruptThread");
  DLOG("Job Interrupt thread started");

  const bool applied_role = magma::PlatformThreadHelper::SetRole(
      platform_device_->GetDeviceHandle(), "fuchsia.graphics.drivers.msd-arm-mali.job-interrupt");
  if (!applied_role) {
    DLOG("Failed to get higher priority!");
  }

  while (!interrupt_thread_quit_flag_) {
    DLOG("Job waiting for interrupt");
    job_interrupt_->Wait();
    DLOG("Job Returned from interrupt wait!");
    job_interrupt_delay_ = job_interrupt_->GetMicrosecondsSinceLastInterrupt();
    auto now = magma::get_monotonic_ns();
    job_interrupt_time_ = now;
    // Resets flag at end of loop iteration.
    handling_job_interrupt_ = true;
    auto cleanup = fit::defer([&]() { handling_job_interrupt_ = false; });

    if (interrupt_thread_quit_flag_)
      break;
    auto request = std::make_unique<JobInterruptRequest>(now);
    auto reply = request->GetReply();
    EnqueueDeviceRequest(std::move(request), true);
    reply->Wait();
  }

  DLOG("Job Interrupt thread exited");
  return 0;
}

static bool IsHardwareResultCode(uint32_t result) {
  switch (result) {
    case kArmMaliResultSuccess:
    case kArmMaliResultSoftStopped:
    case kArmMaliResultAtomTerminated:

    case kArmMaliResultConfigFault:
    case kArmMaliResultPowerFault:
    case kArmMaliResultReadFault:
    case kArmMaliResultWriteFault:
    case kArmMaliResultAffinityFault:
    case kArmMaliResultBusFault:

    case kArmMaliResultProgramCounterInvalidFault:
    case kArmMaliResultEncodingInvalidFault:
    case kArmMaliResultTypeMismatchFault:
    case kArmMaliResultOperandFault:
    case kArmMaliResultTlsFault:
    case kArmMaliResultBarrierFault:
    case kArmMaliResultAlignmentFault:
    case kArmMaliResultDataInvalidFault:
    case kArmMaliResultTileRangeFault:
    case kArmMaliResultOutOfMemoryFault:
      return true;

    default:
      return false;
  }
}

magma::Status MsdArmDevice::ProcessJobInterrupt(uint64_t time) {
  TRACE_DURATION("magma", "MsdArmDevice::ProcessJobInterrupt");
  job_interrupt_time_processed_ = time;

  while (true) {
    auto irq_status = registers::JobIrqFlags::GetRawStat().ReadFrom(register_io_.get());
    if (!irq_status.reg_value())
      break;
    auto clear_flags = registers::JobIrqFlags::GetIrqClear().FromValue(irq_status.reg_value());
    clear_flags.WriteTo(register_io_.get());
    DLOG("Processing job interrupt status %x", irq_status.reg_value());

    bool dumped_on_failure = false;
    uint32_t failed = irq_status.failed_slots();
    while (failed) {
      uint32_t slot = __builtin_ffs(failed) - 1;
      registers::JobSlotRegisters regs(slot);
      uint32_t raw_result = regs.Status().ReadFrom(register_io_.get()).reg_value();
      uint32_t result = IsHardwareResultCode(raw_result) ? raw_result : kArmMaliResultUnknownFault;

      // Soft stopping isn't counted as an actual failure.
      if (result != kArmMaliResultSoftStopped && !dumped_on_failure) {
        MAGMA_LOG(WARNING, "Got failed slot bitmask %x with result code %x",
                  static_cast<uint32_t>(irq_status.failed_slots()), raw_result);
        ProcessDumpStatusToLog();
        dumped_on_failure = true;
      }

      uint64_t job_tail = regs.Tail().ReadFrom(register_io_.get()).reg_value();

      scheduler_->JobCompleted(slot, static_cast<ArmMaliResultCode>(result), job_tail);
      failed &= ~(1 << slot);
    }

    uint32_t finished = irq_status.finished_slots();
    while (finished) {
      uint32_t slot = __builtin_ffs(finished) - 1;
      scheduler_->JobCompleted(slot, kArmMaliResultSuccess, 0u);
      finished &= ~(1 << slot);
    }
  }
  job_interrupt_->Complete();
  return MAGMA_STATUS_OK;
}

magma::Status MsdArmDevice::ProcessMmuInterrupt() {
  auto irq_status = registers::MmuIrqFlags::GetStatus().ReadFrom(register_io_.get());
  DLOG("Received MMU IRQ status 0x%x", irq_status.reg_value());

  uint32_t faulted_slots = irq_status.pf_flags() | irq_status.bf_flags();
  while (faulted_slots) {
    uint32_t slot = ffs(faulted_slots) - 1;

    // Clear all flags before attempting to page in memory, as otherwise
    // if the atom continues executing the next interrupt may be lost.
    auto clear_flags = registers::MmuIrqFlags::GetIrqClear().FromValue(0);
    clear_flags.set_pf_flags(1 << slot);
    clear_flags.set_bf_flags(1 << slot);
    clear_flags.WriteTo(register_io_.get());

    std::shared_ptr<MsdArmConnection> connection;
    {
      auto mapping = address_manager_->GetMappingForSlot(slot);
      if (!mapping) {
        MAGMA_LOG(WARNING, "Fault on idle slot %d", slot);
      } else {
        connection = mapping->connection();
      }
    }
    if (connection) {
      uint64_t address =
          registers::AsRegisters(slot).FaultAddress().ReadFrom(register_io_.get()).reg_value();
      bool kill_context = true;
      if (irq_status.bf_flags() & (1 << slot)) {
        MAGMA_LOG(WARNING,
                  "Bus fault at address 0x%lx on slot %d, client id: %ld, context count: %ld",
                  address, slot, connection->client_id(), connection->context_count());
      } else {
        if (connection->PageInMemory(address)) {
          DLOG("Paged in address %lx", address);
          kill_context = false;
        } else {
          MAGMA_LOG(
              WARNING,
              "Failed to page in address 0x%lx on slot %d, client id: %ld, context count: %ld",
              address, slot, connection->client_id(), connection->context_count());
        }
      }
      if (kill_context) {
        ProcessDumpStatusToLog();

        connection->set_address_space_lost();
        scheduler_->ReleaseMappingsForConnection(connection);
        // This will invalidate the address slot, causing the job to die
        // with a fault.
        address_manager_->ReleaseSpaceMappings(connection->const_address_space());
      }
    }
    faulted_slots &= ~(1 << slot);
  }

  mmu_interrupt_->Complete();
  return MAGMA_STATUS_OK;
}

int MsdArmDevice::MmuInterruptThreadLoop() {
  magma::PlatformThreadHelper::SetCurrentThreadName("MMU InterruptThread");
  DLOG("MMU Interrupt thread started");

  const bool applied_role = magma::PlatformThreadHelper::SetRole(
      platform_device_->GetDeviceHandle(), "fuchsia.graphics.drivers.msd-arm-mali.mmu-interrupt");
  if (!applied_role) {
    DLOG("Failed to get higher priority!");
  }

  while (!interrupt_thread_quit_flag_) {
    DLOG("MMU waiting for interrupt");
    mmu_interrupt_->Wait();
    DLOG("MMU Returned from interrupt wait!");
    mmu_interrupt_delay_ = mmu_interrupt_->GetMicrosecondsSinceLastInterrupt();
    mmu_interrupt_time_ = magma::get_monotonic_ns();
    // Resets flag at end of loop iteration.
    handling_mmu_interrupt_ = true;
    auto cleanup = fit::defer([&]() { handling_mmu_interrupt_ = false; });

    if (interrupt_thread_quit_flag_)
      break;
    auto request = std::make_unique<MmuInterruptRequest>();
    auto reply = request->GetReply();
    EnqueueDeviceRequest(std::move(request), true);
    reply->Wait();
  }

  DLOG("MMU Interrupt thread exited");
  return 0;
}

void MsdArmDevice::StartDeviceThread() {
  DASSERT(!device_thread_.joinable());
  device_thread_ = std::thread([this] { this->DeviceThreadLoop(); });

  perf_counters_->SetDeviceThreadId(device_thread_.get_id());

  job_interrupt_thread_ = std::thread([this] { this->JobInterruptThreadLoop(); });
  mmu_interrupt_thread_ = std::thread([this] { this->MmuInterruptThreadLoop(); });
}

bool MsdArmDevice::InitializeInterrupts() {
  // When it's initialize the reset completed flag may be set. Clear it so
  // we don't get a useless interrupt.
  auto clear_flags = registers::GpuIrqFlags::GetIrqClear().FromValue(0xffffffff);
  clear_flags.WriteTo(register_io_.get());

  gpu_interrupt_ = platform_device_->RegisterInterrupt(kInterruptIndexGpu);
  if (!gpu_interrupt_)
    return DRETF(false, "failed to register GPU interrupt");

  job_interrupt_ = platform_device_->RegisterInterrupt(kInterruptIndexJob);
  if (!job_interrupt_)
    return DRETF(false, "failed to register JOB interrupt");

  mmu_interrupt_ = platform_device_->RegisterInterrupt(kInterruptIndexMmu);
  if (!mmu_interrupt_)
    return DRETF(false, "failed to register MMU interrupt");

  return true;
}

void MsdArmDevice::EnableInterrupts() {
  auto gpu_flags = registers::GpuIrqFlags::GetIrqMask().FromValue(0xffffffff);
  gpu_flags.WriteTo(register_io_.get());

  auto mmu_flags = registers::MmuIrqFlags::GetIrqMask().FromValue(0xffffffff);
  mmu_flags.WriteTo(register_io_.get());

  auto job_flags = registers::JobIrqFlags::GetIrqMask().FromValue(0xffffffff);
  job_flags.WriteTo(register_io_.get());
}

void MsdArmDevice::DisableInterrupts() {
  if (!register_io_)
    return;
  auto gpu_flags = registers::GpuIrqFlags::GetIrqMask().FromValue(0);
  gpu_flags.WriteTo(register_io_.get());

  auto mmu_flags = registers::MmuIrqFlags::GetIrqMask().FromValue(0);
  mmu_flags.WriteTo(register_io_.get());

  auto job_flags = registers::JobIrqFlags::GetIrqMask().FromValue(0);
  job_flags.WriteTo(register_io_.get());
}

void MsdArmDevice::EnqueueDeviceRequest(std::unique_ptr<DeviceRequest> request,
                                        bool enqueue_front) {
  std::unique_lock<std::mutex> lock(device_request_mutex_);
  request->OnEnqueued();
  if (enqueue_front) {
    device_request_list_.emplace_front(std::move(request));
  } else {
    device_request_list_.emplace_back(std::move(request));
  }
  device_request_semaphore_->Signal();
}

void MsdArmDevice::ScheduleAtom(std::shared_ptr<MsdArmAtom> atom) {
  bool need_schedule;
  {
    std::lock_guard<std::mutex> lock(schedule_mutex_);
    need_schedule = atoms_to_schedule_.empty();
    atoms_to_schedule_.push_back(std::move(atom));
  }
  if (need_schedule)
    EnqueueDeviceRequest(std::make_unique<ScheduleAtomRequest>());
}

void MsdArmDevice::CancelAtoms(std::shared_ptr<MsdArmConnection> connection) {
  EnqueueDeviceRequest(std::make_unique<CancelAtomsRequest>(connection));
}

magma::PlatformPort* MsdArmDevice::GetPlatformPort() { return device_port_.get(); }

void MsdArmDevice::UpdateGpuActive(bool active) { power_manager_->UpdateGpuActive(active); }

void MsdArmDevice::DumpRegisters(const GpuFeatures& features, mali::RegisterIo* io,
                                 DumpState* dump_state) {
  static struct {
    const char* name;
    registers::CoreReadyState::CoreType type;
  } core_types[] = {{"L2 Cache", registers::CoreReadyState::CoreType::kL2},
                    {"Shader", registers::CoreReadyState::CoreType::kShader},
                    {"Tiler", registers::CoreReadyState::CoreType::kTiler}};

  static struct {
    const char* name;
    registers::CoreReadyState::StatusType type;
  } status_types[] = {{"Present", registers::CoreReadyState::StatusType::kPresent},
                      {"Ready", registers::CoreReadyState::StatusType::kReady},
                      {"Transitioning", registers::CoreReadyState::StatusType::kPowerTransitioning},
                      {"Power active", registers::CoreReadyState::StatusType::kPowerActive}};
  for (size_t i = 0; i < std::size(core_types); i++) {
    for (size_t j = 0; j < std::size(status_types); j++) {
      uint64_t bitmask =
          registers::CoreReadyState::ReadBitmask(io, core_types[i].type, status_types[j].type);
      dump_state->power_states.push_back({core_types[i].name, status_types[j].name, bitmask});
    }
  }

  dump_state->gpu_fault_status = registers::GpuFaultStatus::Get().ReadFrom(io).reg_value();
  dump_state->gpu_fault_address = registers::GpuFaultAddress::Get().ReadFrom(io).reg_value();
  dump_state->gpu_status = registers::GpuStatus::Get().ReadFrom(io).reg_value();
  dump_state->cycle_count = registers::CycleCount::Get().ReadFrom(io).reg_value();
  dump_state->timestamp = registers::Timestamp::Get().ReadFrom(io).reg_value();

  dump_state->gpu_irq_rawstat = registers::GpuIrqFlags::GetRawStat().ReadFrom(io).reg_value();
  dump_state->gpu_irq_status = registers::GpuIrqFlags::GetStatus().ReadFrom(io).reg_value();
  dump_state->gpu_irq_mask = registers::GpuIrqFlags::GetIrqMask().ReadFrom(io).reg_value();

  dump_state->job_irq_rawstat = registers::JobIrqFlags::GetRawStat().ReadFrom(io).reg_value();
  dump_state->job_irq_status = registers::JobIrqFlags::GetStatus().ReadFrom(io).reg_value();
  dump_state->job_irq_mask = registers::JobIrqFlags::GetIrqMask().ReadFrom(io).reg_value();
  dump_state->job_irq_js_state = registers::JobJsState::Get().ReadFrom(io).reg_value();

  dump_state->mmu_irq_rawstat = registers::MmuIrqFlags::GetRawStat().ReadFrom(io).reg_value();
  dump_state->mmu_irq_status = registers::MmuIrqFlags::GetStatus().ReadFrom(io).reg_value();
  dump_state->mmu_irq_mask = registers::MmuIrqFlags::GetIrqMask().ReadFrom(io).reg_value();

  for (uint32_t i = 0; i < features.job_slot_count; i++) {
    DumpState::JobSlotStatus status;
    auto js_regs = registers::JobSlotRegisters(i);
    status.status = js_regs.Status().ReadFrom(io).reg_value();
    status.head = js_regs.Head().ReadFrom(io).reg_value();
    status.tail = js_regs.Tail().ReadFrom(io).reg_value();
    status.config = js_regs.Config().ReadFrom(io).reg_value();
    dump_state->job_slot_status.push_back(status);
  }

  for (uint32_t i = 0; i < features.address_space_count; i++) {
    DumpState::AddressSpaceStatus status;
    auto as_regs = registers::AsRegisters(i);
    status.status = as_regs.Status().ReadFrom(io).reg_value();
    status.fault_status = as_regs.FaultStatus().ReadFrom(io).reg_value();
    status.fault_address = as_regs.FaultAddress().ReadFrom(io).reg_value();
    dump_state->address_space_status.push_back(status);
  }
}

void MsdArmDevice::Dump(DumpState* dump_state, bool on_device_thread) {
  DumpRegisters(gpu_features_, register_io_.get(), dump_state);

  // These are atomics, so they can be accessed on any thread.
  dump_state->handling_gpu_interrupt = handling_gpu_interrupt_;
  dump_state->handling_job_interrupt = handling_job_interrupt_;
  dump_state->handling_mmu_interrupt = handling_mmu_interrupt_;
  dump_state->gpu_interrupt_delay = gpu_interrupt_delay_;
  dump_state->job_interrupt_delay = job_interrupt_delay_;
  dump_state->mmu_interrupt_delay = mmu_interrupt_delay_;
  dump_state->gpu_interrupt_time = gpu_interrupt_time_;
  dump_state->job_interrupt_time = job_interrupt_time_;
  dump_state->mmu_interrupt_time = mmu_interrupt_time_;
  dump_state->job_interrupt_time_processed = job_interrupt_time_processed_;

  if (on_device_thread) {
    std::chrono::steady_clock::duration total_time;
    std::chrono::steady_clock::duration active_time;
    power_manager_->GetGpuActiveInfo(&total_time, &active_time);
    dump_state->total_time_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(total_time).count();
    dump_state->active_time_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(active_time).count();
  }
}

void MsdArmDevice::DumpToString(std::vector<std::string>* dump_string, bool on_device_thread) {
  DumpState dump_state = {};
  Dump(&dump_state, on_device_thread);

  FormatDump(dump_state, dump_string);

  {
    std::unique_lock<std::mutex> lock(device_request_mutex_);
    auto current_time = std::chrono::steady_clock::now();
    dump_string->push_back(
        fbl::StringPrintf("Device request queue size: %ld", device_request_list_.size()).c_str());
    for (auto& request : device_request_list_) {
      dump_string->push_back(
          fbl::StringPrintf("Device request queuing delay: %lld ms",
                            std::chrono::duration_cast<std::chrono::milliseconds>(
                                current_time - request->enqueue_time())
                                .count())
              .c_str());
    }
  }
}

static const char* ExceptionTypeToString(uint32_t exception_code) {
  switch (exception_code) {
    case 0xc0:
    case 0xc1:
    case 0xc2:
    case 0xc3:
      return "Translation fault";
    case 0xc8:
      return "Permission fault";
    case 0xd0:
    case 0xd1:
    case 0xd2:
    case 0xd3:
      return "Translation bus fault";
    case 0xd8:
      return "Access flag issue";
    default:
      return "Unknown";
  }
}
static std::string InterpretMmuFaultStatus(uint32_t status) {
  const char* access_type;
  constexpr uint32_t kAccessTypeShift = 8;
  constexpr uint32_t kSourceIdShift = 16;
  constexpr uint32_t kAccessTypeBits = 3;
  constexpr uint32_t kExceptionTypeMask = 0xff;
  switch ((status >> kAccessTypeShift) & kAccessTypeBits) {
    case 1:
      access_type = "execute";
      break;
    case 2:
      access_type = "read";
      break;
    case 3:
      access_type = "write";
      break;
    default:
      access_type = "unknown";
      break;
  }
  uint32_t source_id = status >> kSourceIdShift;
  const char* exception_type = ExceptionTypeToString(status & kExceptionTypeMask);
  return fbl::StringPrintf("  Fault source_id %d, access type \"%s\", exception type: \"%s\"",
                           source_id, access_type, exception_type)
      .c_str();
}

void MsdArmDevice::FormatDump(DumpState& dump_state, std::vector<std::string>* dump_string) {
  dump_string->push_back("Core power states");
  for (auto& state : dump_state.power_states) {
    dump_string->push_back(fbl::StringPrintf("Core type %s state %s bitmap: 0x%lx", state.core_type,
                                             state.status_type, state.bitmask)
                               .c_str());
  }
  dump_string->push_back(fbl::StringPrintf("Total ms %" PRIu64 " Active ms %" PRIu64,
                                           dump_state.total_time_ms, dump_state.active_time_ms)
                             .c_str());
  dump_string->push_back(fbl::StringPrintf("Gpu fault status 0x%x, address 0x%lx",
                                           dump_state.gpu_fault_status,
                                           dump_state.gpu_fault_address)
                             .c_str());
  dump_string->push_back(fbl::StringPrintf("Gpu status 0x%x", dump_state.gpu_status).c_str());
  dump_string->push_back(fbl::StringPrintf("Gpu cycle count %ld, timestamp %ld",
                                           dump_state.cycle_count, dump_state.timestamp)
                             .c_str());

  dump_string->push_back(fbl::StringPrintf("GPU IRQ Rawstat 0x%x Status 0x%x Mask 0x%x",
                                           dump_state.gpu_irq_rawstat, dump_state.gpu_irq_status,
                                           dump_state.gpu_irq_mask)
                             .c_str());
  dump_string->push_back(
      fbl::StringPrintf("JOB IRQ Rawstat 0x%x Status 0x%x Mask 0x%x JsState 0x%x",
                        dump_state.job_irq_rawstat, dump_state.job_irq_status,
                        dump_state.job_irq_mask, dump_state.job_irq_js_state)
          .c_str());
  dump_string->push_back(fbl::StringPrintf("MMU IRQ Rawstat 0x%x Status 0x%x Mask 0x%x",
                                           dump_state.mmu_irq_rawstat, dump_state.mmu_irq_status,
                                           dump_state.mmu_irq_mask)
                             .c_str());
  dump_string->push_back(fbl::StringPrintf("IRQ handlers running - GPU: %d Job: %d Mmu: %d",
                                           dump_state.handling_gpu_interrupt,
                                           dump_state.handling_job_interrupt,
                                           dump_state.handling_mmu_interrupt)
                             .c_str());

  auto now = magma::get_monotonic_ns();
  dump_string->push_back(
      fbl::StringPrintf("Time since last IRQ handler - GPU: %ld us, Job: %ld us, Mmu: %ld us",
                        (now - dump_state.gpu_interrupt_time) / 1000,
                        (now - dump_state.job_interrupt_time) / 1000,
                        (now - dump_state.mmu_interrupt_time) / 1000)
          .c_str());
  dump_string->push_back(fbl::StringPrintf("Last job interrupt time: %ld Processed: %ld",
                                           dump_state.job_interrupt_time,
                                           dump_state.job_interrupt_time_processed)
                             .c_str());

  dump_string->push_back(
      fbl::StringPrintf("Last interrupt delays - GPU: %ld us, Job: %ld us, Mmu: %ld us",
                        dump_state.gpu_interrupt_delay, dump_state.job_interrupt_delay,
                        dump_state.mmu_interrupt_delay)
          .c_str());

  for (size_t i = 0; i < dump_state.job_slot_status.size(); i++) {
    auto* status = &dump_state.job_slot_status[i];
    dump_string->push_back(
        fbl::StringPrintf("Job slot %zu status 0x%x head 0x%lx tail 0x%lx config 0x%x", i,
                          status->status, status->head, status->tail, status->config)
            .c_str());
  }
  for (size_t i = 0; i < dump_state.address_space_status.size(); i++) {
    auto* status = &dump_state.address_space_status[i];
    dump_string->push_back(
        fbl::StringPrintf("AS %zu status 0x%x fault status 0x%x fault address 0x%lx", i,
                          status->status, status->fault_status, status->fault_address)
            .c_str());
    dump_string->push_back(InterpretMmuFaultStatus(status->fault_status));
  }
}

magma::Status MsdArmDevice::ProcessDumpStatusToLog() {
  std::vector<std::string> dump;
  DumpToString(&dump, true);
  MAGMA_LOG(INFO, "Gpu register dump");
  for (auto& str : dump) {
    MAGMA_LOG(INFO, "%s", str.c_str());
  }

  std::vector<std::string> job_information = scheduler_->DumpStatus();
  for (auto& str : job_information) {
    MAGMA_LOG(INFO, "%s", str.c_str());
  }

  return MAGMA_STATUS_OK;
}

magma::Status MsdArmDevice::ProcessScheduleAtoms() {
  std::vector<std::shared_ptr<MsdArmAtom>> atoms_to_schedule;
  {
    std::lock_guard<std::mutex> lock(schedule_mutex_);
    atoms_to_schedule.swap(atoms_to_schedule_);
  }
  for (auto& atom : atoms_to_schedule) {
    scheduler_->EnqueueAtom(std::move(atom));
  }
  scheduler_->TryToSchedule();
  return MAGMA_STATUS_OK;
}

magma::Status MsdArmDevice::ProcessCancelAtoms(std::weak_ptr<MsdArmConnection> connection) {
  // It's fine to cancel with an invalid shared_ptr, as that will clear out
  // atoms for connections that are dead already.
  scheduler_->CancelAtomsForConnection(connection.lock());
  return MAGMA_STATUS_OK;
}

void MsdArmDevice::ExecuteAtomOnDevice(MsdArmAtom* atom, mali::RegisterIo* register_io) {
  TRACE_DURATION("magma", "ExecuteAtomOnDevice", "address", atom->gpu_address(), "slot",
                 atom->slot());
  TRACE_FLOW_STEP("magma", "atom", atom->trace_nonce());

  DASSERT(atom->slot() < 2u);
  bool dependencies_finished;
  atom->UpdateDependencies(&dependencies_finished);
  DASSERT(dependencies_finished);
  DASSERT(atom->gpu_address());

  // Skip atom if address space can't be assigned.
  if (!address_manager_->AssignAddressSpace(atom)) {
    scheduler_->JobCompleted(atom->slot(), kArmMaliResultAtomTerminated, 0u);
    return;
  }
  if (atom->require_cycle_counter()) {
    DASSERT(!atom->using_cycle_counter());
    atom->set_using_cycle_counter(true);

    RefCycleCounter();
  }

  if (atom->is_protected()) {
    DASSERT(IsInProtectedMode());
  } else {
    DASSERT(!IsInProtectedMode());
  }

  auto connection = atom->connection().lock();
  // Should be kept alive because an address space is assigned.
  DASSERT(connection);

  // Ensure the client's writes/cache flushes to the job chain are complete
  // before scheduling. Unlikely to be an issue since several thread and
  // process hops already happened.
  magma::barriers::WriteBarrier();

  registers::JobSlotRegisters slot(atom->slot());
  slot.HeadNext().FromValue(atom->gpu_address()).WriteTo(register_io);
  auto config = slot.ConfigNext().FromValue(0);
  config.set_address_space(atom->address_slot_mapping()->slot_number());
  config.set_start_flush_clean(true);
  config.set_start_flush_invalidate(true);
  // TODO(fxbug.dev/12981): Enable flush reduction optimization.
  config.set_thread_priority(8);
  config.set_end_flush_clean(true);
  config.set_end_flush_invalidate(true);
  // Atoms are in unprotected memory, so don't attempt to write to them when
  // executing in protected mode.
  bool disable_descriptor_write_back = atom->is_protected();
#if defined(ENABLE_PROTECTED_DEBUG_SWAP_MODE)
  // In this case, nonprotected-mode atoms also need to abide by protected mode restrictions.
  disable_descriptor_write_back = true;
#endif
  config.set_disable_descriptor_write_back(disable_descriptor_write_back);
  config.WriteTo(register_io);

  // Execute on every powered-on core.
  slot.AffinityNext().FromValue(UINT64_MAX).WriteTo(register_io);
  slot.CommandNext().FromValue(registers::JobSlotCommand::kCommandStart).WriteTo(register_io);

  // Begin the virtual duration trace event to measure GPU work.
  uint64_t ATTRIBUTE_UNUSED current_ticks = magma::PlatformTrace::GetCurrentTicks();
  TRACE_VTHREAD_DURATION_BEGIN("magma", MsdArmAtom::AtomRunningString(atom->slot()),
                               MsdArmAtom::AtomRunningString(atom->slot()), atom->slot_id(),
                               current_ticks, "client_id", connection->client_id());
  TRACE_VTHREAD_FLOW_STEP("magma", "atom", MsdArmAtom::AtomRunningString(atom->slot()),
                          atom->slot_id(), atom->trace_nonce(), current_ticks);
}

void MsdArmDevice::RunAtom(MsdArmAtom* atom) { ExecuteAtomOnDevice(atom, register_io_.get()); }

void MsdArmDevice::AtomCompleted(MsdArmAtom* atom, ArmMaliResultCode result) {
  TRACE_DURATION("magma", "AtomCompleted", "address", atom->gpu_address());
  TRACE_FLOW_END("magma", "atom", atom->trace_nonce());

  DLOG("Completed job atom: 0x%lx", atom->gpu_address());
  address_manager_->AtomFinished(atom);
  if (atom->using_cycle_counter()) {
    DASSERT(atom->require_cycle_counter());

    DerefCycleCounter();
    atom->set_using_cycle_counter(false);
  }
  // Soft stopped atoms will be retried, so this result shouldn't be reported.
  if (result != kArmMaliResultSoftStopped) {
    atom->set_result_code(result);
    auto connection = atom->connection().lock();
    // Ensure any client writes/reads from memory happen after the mmio access saying memory is
    // read. In practice unlikely to be an issue due to data dependencies and the thread/process
    // hops.
    magma::barriers::Barrier();
    if (connection)
      connection->SendNotificationData(atom);
  }
}

void MsdArmDevice::HardStopAtom(MsdArmAtom* atom) {
  DASSERT(atom->hard_stopped());
  registers::JobSlotRegisters slot(atom->slot());
  DLOG("Hard stopping atom slot %d", atom->slot());
  slot.Command().FromValue(registers::JobSlotCommand::kCommandHardStop).WriteTo(register_io_.get());
}

void MsdArmDevice::SoftStopAtom(MsdArmAtom* atom) {
  registers::JobSlotRegisters slot(atom->slot());
  DLOG("Soft stopping atom slot %d", atom->slot());
  slot.Command().FromValue(registers::JobSlotCommand::kCommandSoftStop).WriteTo(register_io_.get());
}

void MsdArmDevice::ReleaseMappingsForAtom(MsdArmAtom* atom) {
  // The atom should be hung on a fault, so it won't reference memory
  // afterwards.
  address_manager_->AtomFinished(atom);
}

void MsdArmDevice::RefCycleCounter() {
  if (++cycle_counter_refcount_ == 1) {
    register_io_->Write32(registers::GpuCommand::kCmdCycleCountStart,
                          registers::GpuCommand::kOffset);
  }
}

void MsdArmDevice::DerefCycleCounter() {
  DASSERT(cycle_counter_refcount_ != 0);
  if (--cycle_counter_refcount_ == 0) {
    register_io_->Write32(registers::GpuCommand::kCmdCycleCountStop,
                          registers::GpuCommand::kOffset);
  }
}

magma_status_t MsdArmDevice::QueryInfo(uint64_t id, uint64_t* value_out) {
  switch (id) {
    case MAGMA_QUERY_VENDOR_ID:
      *value_out = MAGMA_VENDOR_ID_MALI;
      return MAGMA_STATUS_OK;

    case MAGMA_QUERY_DEVICE_ID:
      *value_out = gpu_features_.gpu_id.reg_value();
      return MAGMA_STATUS_OK;

    case MAGMA_QUERY_IS_TOTAL_TIME_SUPPORTED:
      *value_out = 1;
      return MAGMA_STATUS_OK;

    case kMsdArmVendorQueryL2Present:
      *value_out = gpu_features_.l2_present;
      return MAGMA_STATUS_OK;

    case kMsdArmVendorQueryMaxThreads:
      *value_out = gpu_features_.thread_max_threads;
      return MAGMA_STATUS_OK;

    case kMsdArmVendorQueryThreadMaxBarrierSize:
      *value_out = gpu_features_.thread_max_barrier_size;
      return MAGMA_STATUS_OK;

    case kMsdArmVendorQueryThreadMaxWorkgroupSize:
      *value_out = gpu_features_.thread_max_workgroup_size;
      return MAGMA_STATUS_OK;

    case kMsdArmVendorQueryThreadTlsAlloc:
      *value_out = gpu_features_.thread_tls_alloc;
      return MAGMA_STATUS_OK;

    case kMsdArmVendorQueryShaderPresent:
      *value_out = gpu_features_.shader_present;
      return MAGMA_STATUS_OK;

    case kMsdArmVendorQueryTilerFeatures:
      *value_out = gpu_features_.tiler_features.reg_value();
      return MAGMA_STATUS_OK;

    case kMsdArmVendorQueryThreadFeatures:
      *value_out = gpu_features_.thread_features.reg_value();
      return MAGMA_STATUS_OK;

    case kMsdArmVendorQueryL2Features:
      *value_out = gpu_features_.l2_features.reg_value();
      return MAGMA_STATUS_OK;

    case kMsdArmVendorQueryMemoryFeatures:
      *value_out = gpu_features_.mem_features.reg_value();
      return MAGMA_STATUS_OK;

    case kMsdArmVendorQueryMmuFeatures:
      *value_out = gpu_features_.mmu_features.reg_value();
      return MAGMA_STATUS_OK;

    case kMsdArmVendorQueryCoherencyEnabled:
      *value_out = cache_coherency_status_;
      return MAGMA_STATUS_OK;

    case kMsdArmVendorQuerySupportsProtectedMode:
      *value_out = IsProtectedModeSupported();
      return MAGMA_STATUS_OK;

    default:
      return MAGMA_STATUS_INVALID_ARGS;
  }
}

magma_status_t MsdArmDevice::QueryReturnsBuffer(uint64_t id, uint32_t* buffer_out) {
  switch (id) {
    case MAGMA_QUERY_TOTAL_TIME:
      return power_manager_->GetTotalTime(buffer_out) ? MAGMA_STATUS_OK
                                                      : MAGMA_STATUS_INTERNAL_ERROR;
    case kMsdArmVendorQueryDeviceTimestamp: {
      auto buffer = magma::PlatformBuffer::Create(magma::page_size(), "timestamps");
      if (!buffer)
        return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "failed to create timestamp buffer");

      if (!buffer->duplicate_handle(buffer_out))
        return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "failed to dupe timestamp buffer");

      return QueryTimestamp(std::move(buffer)).get();
    }
    default:
      return MAGMA_STATUS_INVALID_ARGS;
  }
}

// static
void MsdArmDevice::InitializeHardwareQuirks(GpuFeatures* features, mali::RegisterIo* reg) {
  auto shader_config = registers::ShaderConfig::Get().FromValue(0);
  const uint32_t kGpuIdTGOX = 0x7212;
  uint32_t gpu_product_id = features->gpu_id.product_id();
  if (gpu_product_id == kGpuIdTGOX) {
    DLOG("Enabling TLS hashing");
    shader_config.set_tls_hashing_enable(1);
  }

  if (0x750 <= gpu_product_id && gpu_product_id <= 0x880) {
    DLOG("Enabling LS attr types");
    // This seems necessary for geometry shaders to work with non-indexed draws with point and
    // line lists on T8xx and T7xx.
    shader_config.set_ls_allow_attr_types(1);
  }

  shader_config.WriteTo(reg);
}

bool MsdArmDevice::IsProtectedModeSupported() {
  if (!mali_properties_.supports_protected_mode)
    return false;
  uint32_t gpu_product_id = gpu_features_.gpu_id.product_id();
  // TODO(fxbug.dev/13130): Support protected mode when using ACE cache coherency. Apparently
  // the L2 needs to be powered down then switched to ACE Lite in that mode.
  if (cache_coherency_status_ == kArmMaliCacheCoherencyAce)
    return false;
  // All Bifrost should support it. 0x6956 is Mali-t60x MP4 r0p0, so it doesn't count.
  return gpu_product_id != 0x6956 && (gpu_product_id > 0x1000);
}

void MsdArmDevice::EnterProtectedMode() {
  TRACE_DURATION("magma", "MsdArmDevice::EnterProtectedMode");
  // Remove perf counter address mapping.
  perf_counters_->ForceDisable();

  if (!mali_properties_.use_protected_mode_callbacks) {
    // TODO(fxbug.dev/13130): If cache-coherency is enabled, power down L2 and wait for the
    // completion of that.
    register_io_->Write32(registers::GpuCommand::kCmdSetProtectedMode,
                          registers::GpuCommand::kOffset);
    return;
  }
  // |force_expire| is false because nothing should have been using an address
  // space before. Do this before powering down L2 so connections don't try to
  // hit the MMU while that's happening.
  address_manager_->ClearAddressMappings(false);

  if (!PowerDownShaders()) {
    MAGMA_LOG(ERROR, "Powering down shaders timed out");
    // Keep trying to reset the device, or the job scheduler will hang forever.
  }
  // Powering down L2 can fail due to errata 1485982, so flush/invalidate L2
  // instead. We should be able to enter protected mode with L2 enabled.
  if (!FlushL2()) {
    MAGMA_LOG(ERROR, "Flushing L2 timed out");
    // Keep trying to reset the device, or the job scheduler will hang forever.
  }

  zx_status_t status = mali_protocol_client_.EnterProtectedMode();
  if (status != ZX_OK) {
    MAGMA_LOG(ERROR, "Error from EnterProtectedMode: %d", status);
  }

  EnableAllCores();

  if (!power_manager_->WaitForShaderReady(register_io_.get())) {
    MAGMA_LOG(WARNING, "Waiting for shader ready failed");
    return;
  }
}

bool MsdArmDevice::ExitProtectedMode() {
  TRACE_DURATION("magma", "MsdArmDevice::ExitProtectedMode");
  DASSERT(perf_counters_->force_disabled());
  // |force_expire| is false because nothing should have been using an address
  // space before. Do this before powering down L2 so connections don't try to
  // hit the MMU while that's happening.
  address_manager_->ClearAddressMappings(false);

  if (!PowerDownShaders()) {
    MAGMA_LOG(ERROR, "Powering down shaders timed out");
    // Keep trying to reset the device, or the job scheduler will hang forever.
  }
  // Powering down L2 can fail due to errata 1485982, so flush L2 and let the hardware reset deal
  // with it.
  if (!FlushL2()) {
    MAGMA_LOG(ERROR, "Flushing L2 timed out");
    // Keep trying to reset the device, or the job scheduler will hang forever.
  }

  return ResetDevice();
}

bool MsdArmDevice::FlushL2() {
  cache_clean_semaphore_->Reset();
  register_io_->Write32(registers::GpuCommand::kCmdCleanAndInvalidateCaches,
                        registers::GpuCommand::kOffset);
  if (!cache_clean_semaphore_->Wait(1000).ok()) {
    MAGMA_LOG(ERROR, "Waiting for cache clean semaphore failed");
    return false;
  }
  return true;
}

bool MsdArmDevice::ResetDevice() {
  DLOG("Resetting device protected mode");
  // Reset semaphore shouldn't already be signaled.
  DASSERT(!reset_semaphore_->Wait(0));
  registers::GpuIrqFlags::GetIrqMask()
      .ReadFrom(register_io_.get())
      .set_reset_completed(1)
      .WriteTo(register_io_.get());

  if (!mali_properties_.use_protected_mode_callbacks) {
    register_io_->Write32(registers::GpuCommand::kCmdSoftReset, registers::GpuCommand::kOffset);
  } else {
    exiting_protected_mode_flag_ = true;
    zx_status_t status = mali_protocol_client_.StartExitProtectedMode();
    if (status != ZX_OK) {
      MAGMA_LOG(ERROR, "Error from StartExitProtectedMode: %d", status);
      return false;
    }
  }

  if (!assume_reset_happened_ && !reset_semaphore_->Wait(1000)) {
    MAGMA_LOG(WARNING, "Hardware reset timed out");
    return false;
  }
  DASSERT(assume_reset_happened_ || !exiting_protected_mode_flag_);

  if (!InitializeHardware()) {
    MAGMA_LOG(WARNING, "Initialize hardware failed");
    return false;
  }

  if (!assume_reset_happened_ && !power_manager_->WaitForShaderReady(register_io_.get())) {
    MAGMA_LOG(WARNING, "Waiting for shader ready failed");
    return false;
  }

  perf_counters_->RemoveForceDisable();
  // Re-enable the performance counters if a client requested them.
  perf_counters_->Update();

  return true;
}

bool MsdArmDevice::PowerDownL2() {
  power_manager_->DisableL2(register_io_.get());
  return power_manager_->WaitForL2Disable(register_io_.get());
}

bool MsdArmDevice::PowerDownShaders() {
  power_manager_->DisableShaders(register_io_.get());
  return power_manager_->WaitForShaderDisable(register_io_.get());
}

bool MsdArmDevice::IsInProtectedMode() {
  return registers::GpuStatus::Get().ReadFrom(register_io_.get()).protected_mode_active();
}

std::shared_ptr<DeviceRequest::Reply> MsdArmDevice::RunTaskOnDeviceThread(FitCallbackTask task) {
  auto request = std::make_unique<TaskRequest>(std::move(task));
  auto reply = request->GetReply();
  EnqueueDeviceRequest(std::move(request));
  return reply;
}

void MsdArmDevice::SetCurrentThreadToDefaultPriority() {
  magma::PlatformThreadHelper::SetRole(platform_device_->GetDeviceHandle(), "fuchsia.default");
}

MsdArmDevice::InspectEvent::InspectEvent(inspect::Node* parent, std::string type) {
  static std::atomic_uint64_t event_count;
  node = parent->CreateChild(std::to_string(event_count++));
  node.RecordUint("@time", magma::get_monotonic_ns());
  node.RecordString("type", std::move(type));
}

void MsdArmDevice::AppendInspectEvent(InspectEvent event) {
  std::lock_guard lock(inspect_events_mutex_);
  constexpr uint32_t kMaxEventsToStore = 10;
  while (inspect_events_.size() > kMaxEventsToStore)
    inspect_events_.pop_front();
  inspect_events_.push_back(std::move(event));
}

//////////////////////////////////////////////////////////////////////////////////////////////////

msd_connection_t* msd_device_open(msd_device_t* dev, msd_client_id_t client_id) {
  auto connection = MsdArmDevice::cast(dev)->Open(client_id);
  if (!connection)
    return DRETP(nullptr, "MsdArmDevice::Open failed");
  return new MsdArmAbiConnection(std::move(connection));
}

void msd_device_destroy(msd_device_t* dev) { delete MsdArmDevice::cast(dev); }

magma_status_t msd_device_query(msd_device_t* device, uint64_t id,
                                magma_handle_t* result_buffer_out, uint64_t* result_out) {
  magma_status_t status = MsdArmDevice::cast(device)->QueryReturnsBuffer(id, result_buffer_out);

  if (status == MAGMA_STATUS_INVALID_ARGS) {
    status = MsdArmDevice::cast(device)->QueryInfo(id, result_out);

    if (status == MAGMA_STATUS_OK && result_buffer_out)
      *result_buffer_out = magma::PlatformHandle::kInvalidHandle;
  }

  if (status == MAGMA_STATUS_INVALID_ARGS) {
    return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "unhandled id %" PRIu64, id);
  }

  return status;
}

void msd_device_dump_status(msd_device_t* device, uint32_t dump_type) {
  MsdArmDevice::cast(device)->DumpStatusToLog();
}

void msd_device_set_memory_pressure_level(msd_device_t* device, MagmaMemoryPressureLevel level) {
  MsdArmDevice::cast(device)->SetMemoryPressureLevel(level);
}

magma_status_t msd_device_get_icd_list(struct msd_device_t* abi_device, uint64_t count,
                                       msd_icd_info_t* icd_info_out, uint64_t* actual_count_out) {
  const char* kSuffixes[] = {"_test", ""};
  if (icd_info_out && count < std::size(kSuffixes)) {
    return MAGMA_STATUS_INVALID_ARGS;
  }
  *actual_count_out = std::size(kSuffixes);
  if (icd_info_out) {
    auto device = MsdArmDevice::cast(abi_device);
    for (uint32_t i = 0; i < std::size(kSuffixes); i++) {
      strcpy(icd_info_out[i].component_url,
             fbl::StringPrintf("fuchsia-pkg://fuchsia.com/libvulkan_arm_mali_%lx%s#meta/vulkan.cm",
                               device->GpuId(), kSuffixes[i])
                 .c_str());
      icd_info_out[i].support_flags = ICD_SUPPORT_FLAG_VULKAN;
    }
  }
  return MAGMA_STATUS_OK;
}
