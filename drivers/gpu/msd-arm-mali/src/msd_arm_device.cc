// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd_arm_device.h"

#include <bitset>
#include <cinttypes>
#include <cstdio>
#include <string>

#include "job_scheduler.h"
#include "lib/fxl/arraysize.h"
#include "lib/fxl/strings/string_printf.h"
#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include "magma_vendor_queries.h"
#include "platform_port.h"
#include "platform_trace.h"

#include "registers.h"

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
    magma::Status Process(MsdArmDevice* device) override
    {
        return device->ProcessDumpStatusToLog();
    }
};

class MsdArmDevice::GpuInterruptRequest : public DeviceRequest {
public:
    GpuInterruptRequest() {}

protected:
    magma::Status Process(MsdArmDevice* device) override { return device->ProcessGpuInterrupt(); }
};

class MsdArmDevice::JobInterruptRequest : public DeviceRequest {
public:
    JobInterruptRequest() {}

protected:
    magma::Status Process(MsdArmDevice* device) override { return device->ProcessJobInterrupt(); }
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
    magma::Status Process(MsdArmDevice* device) override
    {
        return device->ProcessCancelAtoms(connection_);
    }

    std::weak_ptr<MsdArmConnection> connection_;
};

class MsdArmDevice::PerfCounterRequest : public DeviceRequest {
public:
    PerfCounterRequest(uint32_t type) : type_(type) {}

protected:
    magma::Status Process(MsdArmDevice* device) override
    {
        return device->ProcessPerfCounterRequest(type_);
    }

    uint32_t type_;
};

//////////////////////////////////////////////////////////////////////////////////////////////////

std::unique_ptr<MsdArmDevice> MsdArmDevice::Create(void* device_handle, bool start_device_thread)
{
    auto device = std::make_unique<MsdArmDevice>();

    if (!device->Init(device_handle))
        return DRETP(nullptr, "Failed to initialize MsdArmDevice");

    if (start_device_thread)
        device->StartDeviceThread();

    return device;
}

MsdArmDevice::MsdArmDevice() { magic_ = kMagic; }

MsdArmDevice::~MsdArmDevice() { Destroy(); }

void MsdArmDevice::Destroy()
{
    DLOG("Destroy");
    CHECK_THREAD_NOT_CURRENT(device_thread_id_);

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

bool MsdArmDevice::Init(void* device_handle)
{
    DLOG("Init");
    platform_device_ = magma::PlatformDevice::Create(device_handle);
    if (!platform_device_)
        return DRETF(false, "Failed to initialize device");

    std::unique_ptr<magma::PlatformMmio> mmio = platform_device_->CpuMapMmio(
        kMmioIndexRegisters, magma::PlatformMmio::CACHE_POLICY_UNCACHED_DEVICE);
    if (!mmio)
        return DRETF(false, "failed to map registers");

    register_io_ = std::make_unique<magma::RegisterIo>(std::move(mmio));

    gpu_features_.ReadFrom(register_io_.get());
    magma::log(magma::LOG_INFO, "ARM mali ID %x", gpu_features_.gpu_id.reg_value());

#if defined(MSD_ARM_ENABLE_CACHE_COHERENCY)
    if (gpu_features_.coherency_features.ace().get()) {
        cache_coherency_status_ = kArmMaliCacheCoherencyAce;
    } else {
        magma::log(magma::LOG_INFO, "Cache coherency unsupported");
    }
#endif

    device_request_semaphore_ = magma::PlatformSemaphore::Create();
    device_port_ = magma::PlatformPort::Create();

    power_manager_ = std::make_unique<PowerManager>(register_io_.get());

    scheduler_ = std::make_unique<JobScheduler>(this, 3);
    address_manager_ = std::make_unique<AddressManager>(this, gpu_features_.address_space_count);

    bus_mapper_ = magma::PlatformBusMapper::Create(platform_device_->GetBusTransactionInitiator());
    if (!bus_mapper_)
        return DRETF(false, "Failed to create bus mapper");

    if (!InitializeInterrupts())
        return false;

    EnableInterrupts();

    uint64_t enabled_cores = 1;
#if defined(MSD_ARM_ENABLE_ALL_CORES)
    enabled_cores = gpu_features_.shader_present;
#endif
    power_manager_->EnableCores(register_io_.get(), enabled_cores);
    perf_counters_ = std::make_unique<PerformanceCounters>(this);

    return true;
}

std::shared_ptr<MsdArmConnection> MsdArmDevice::Open(msd_client_id_t client_id)
{
    return MsdArmConnection::Create(client_id, this);
}

void MsdArmDevice::DumpStatusToLog() { EnqueueDeviceRequest(std::make_unique<DumpRequest>()); }

void MsdArmDevice::SuspectedGpuHang()
{
    magma::log(magma::LOG_WARNING, "Possible GPU hang\n");
    ProcessDumpStatusToLog();
    scheduler_->KillTimedOutAtoms();
}

int MsdArmDevice::DeviceThreadLoop()
{
    magma::PlatformThreadHelper::SetCurrentThreadName("DeviceThread");

    device_thread_id_ = std::make_unique<magma::PlatformThreadId>();
    CHECK_THREAD_IS_CURRENT(device_thread_id_);

    DLOG("DeviceThreadLoop starting thread 0x%lx", device_thread_id_->id());

    std::unique_lock<std::mutex> lock(device_request_mutex_, std::defer_lock);
    device_request_semaphore_->WaitAsync(device_port_.get());

    while (!device_thread_quit_flag_) {
        auto timeout_duration = scheduler_->GetCurrentTimeoutDuration();
        if (timeout_duration <= JobScheduler::Clock::duration::zero()) {
            SuspectedGpuHang();
            continue;
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
            if (key == device_request_semaphore_->id()) {
                device_request_semaphore_->Reset();
                device_request_semaphore_->WaitAsync(device_port_.get());
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

int MsdArmDevice::GpuInterruptThreadLoop()
{
    magma::PlatformThreadHelper::SetCurrentThreadName("Gpu InterruptThread");
    DLOG("GPU Interrupt thread started");

    while (!interrupt_thread_quit_flag_) {
        DLOG("GPU waiting for interrupt");
        gpu_interrupt_->Wait();
        DLOG("GPU Returned from interrupt wait!");

        if (interrupt_thread_quit_flag_)
            break;

        auto request = std::make_unique<GpuInterruptRequest>();
        auto reply = request->GetReply();

        EnqueueDeviceRequest(std::move(request), true);
        reply->Wait();
    }

    DLOG("GPU Interrupt thread exited");
    return 0;
}

magma::Status MsdArmDevice::ProcessGpuInterrupt()
{
    auto irq_status = registers::GpuIrqFlags::GetStatus().ReadFrom(register_io_.get());
    auto clear_flags = registers::GpuIrqFlags::GetIrqClear().FromValue(irq_status.reg_value());
    clear_flags.WriteTo(register_io_.get());

    DLOG("Got GPU interrupt status 0x%x\n", irq_status.reg_value());
    if (!irq_status.reg_value())
        magma::log(magma::LOG_WARNING, "Got unexpected GPU IRQ with no flags set\n");

    if (irq_status.power_changed_single().get() || irq_status.power_changed_all().get()) {
        irq_status.power_changed_single().set(0);
        irq_status.power_changed_all().set(0);
        power_manager_->ReceivedPowerInterrupt(register_io_.get());
        if (power_manager_->l2_ready_status() &&
            (cache_coherency_status_ == kArmMaliCacheCoherencyAce)) {
            auto enable_reg = registers::CoherencyFeatures::GetEnable().FromValue(0);
            enable_reg.ace().set(true);
            enable_reg.WriteTo(register_io_.get());
        }
    }
    if (irq_status.performance_counter_sample_completed().get()) {
        uint64_t duration_ms = 0;
        std::vector<uint32_t> perf_result = perf_counters_->ReadCompleted(&duration_ms);

        magma::log(magma::LOG_INFO, "Performance counter read complete, duration %lu ms:\n",
                   duration_ms);
        for (uint32_t i = 0; i < perf_result.size(); ++i) {
            magma::log(magma::LOG_INFO, "Performance counter %d: %u\n", i, perf_result[i]);
        }
        irq_status.performance_counter_sample_completed().set(0);
    }

    if (irq_status.reg_value()) {
        magma::log(magma::LOG_WARNING, "Got unexpected GPU IRQ %d\n", irq_status.reg_value());
        ProcessDumpStatusToLog();
    }
    gpu_interrupt_->Complete();
    return MAGMA_STATUS_OK;
}

int MsdArmDevice::JobInterruptThreadLoop()
{
    magma::PlatformThreadHelper::SetCurrentThreadName("Job InterruptThread");
    DLOG("Job Interrupt thread started");

    while (!interrupt_thread_quit_flag_) {
        DLOG("Job waiting for interrupt");
        job_interrupt_->Wait();
        DLOG("Job Returned from interrupt wait!");

        if (interrupt_thread_quit_flag_)
            break;
        auto request = std::make_unique<JobInterruptRequest>();
        auto reply = request->GetReply();
        EnqueueDeviceRequest(std::move(request), true);
        reply->Wait();
    }

    DLOG("Job Interrupt thread exited");
    return 0;
}

static bool IsHardwareResultCode(uint32_t result)
{
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

magma::Status MsdArmDevice::ProcessJobInterrupt()
{
    TRACE_DURATION("magma", "MsdArmDevice::ProcessJobInterrupt");
    while (true) {
        auto irq_status = registers::JobIrqFlags::GetRawStat().ReadFrom(register_io_.get());
        if (!irq_status.reg_value())
            break;
        auto clear_flags = registers::JobIrqFlags::GetIrqClear().FromValue(irq_status.reg_value());
        clear_flags.WriteTo(register_io_.get());
        DLOG("Processing job interrupt status %x", irq_status.reg_value());

        bool dumped_on_failure = false;
        uint32_t failed = irq_status.failed_slots().get();
        while (failed) {
            uint32_t slot = __builtin_ffs(failed) - 1;
            registers::JobSlotRegisters regs(slot);
            uint32_t result = regs.Status().ReadFrom(register_io_.get()).reg_value();

            if (!IsHardwareResultCode(result))
                result = kArmMaliResultUnknownFault;

            // Soft stopping isn't counted as an actual failure.
            if (result != kArmMaliResultSoftStopped && !dumped_on_failure) {
                magma::log(magma::LOG_WARNING, "Got unexpected failed slots %x\n",
                           irq_status.failed_slots().get());
                ProcessDumpStatusToLog();
                dumped_on_failure = true;
            }

            uint64_t job_tail = regs.Tail().ReadFrom(register_io_.get()).reg_value();

            scheduler_->JobCompleted(slot, static_cast<ArmMaliResultCode>(result), job_tail);
            failed &= ~(1 << slot);
        }

        uint32_t finished = irq_status.finished_slots().get();
        while (finished) {
            uint32_t slot = __builtin_ffs(finished) - 1;
            scheduler_->JobCompleted(slot, kArmMaliResultSuccess, 0u);
            finished &= ~(1 << slot);
        }
    }
    job_interrupt_->Complete();
    return MAGMA_STATUS_OK;
}

magma::Status MsdArmDevice::ProcessMmuInterrupt()
{
    auto irq_status = registers::MmuIrqFlags::GetStatus().ReadFrom(register_io_.get());
    DLOG("Received MMU IRQ status 0x%x\n", irq_status.reg_value());

    uint32_t faulted_slots = irq_status.pf_flags().get() | irq_status.bf_flags().get();
    while (faulted_slots) {
        uint32_t slot = ffs(faulted_slots) - 1;

        // Clear all flags before attempting to page in memory, as otherwise
        // if the atom continues executing the next interrupt may be lost.
        auto clear_flags = registers::MmuIrqFlags::GetIrqClear().FromValue(0);
        clear_flags.pf_flags().set(1 << slot);
        clear_flags.bf_flags().set(1 << slot);
        clear_flags.WriteTo(register_io_.get());

        std::shared_ptr<MsdArmConnection> connection;
        {
            auto mapping = address_manager_->GetMappingForSlot(slot);
            if (!mapping) {
                magma::log(magma::LOG_WARNING, "Fault on idle slot %d\n", slot);
            } else {
                connection = mapping->connection();
            }
        }
        if (connection) {
            uint64_t address = registers::AsRegisters(slot)
                                   .FaultAddress()
                                   .ReadFrom(register_io_.get())
                                   .reg_value();
            bool kill_context = true;
            if (irq_status.bf_flags().get() & (1 << slot)) {
                magma::log(magma::LOG_WARNING, "Bus fault at address 0x%lx on slot %d\n", address,
                           slot);
            } else {
                if (connection->PageInMemory(address)) {
                    DLOG("Paged in address %lx\n", address);
                    kill_context = false;
                } else {
                    magma::log(magma::LOG_WARNING, "Failed to page in address 0x%lx on slot %d\n",
                               address, slot);
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

int MsdArmDevice::MmuInterruptThreadLoop()
{
    magma::PlatformThreadHelper::SetCurrentThreadName("MMU InterruptThread");
    DLOG("MMU Interrupt thread started");

    while (!interrupt_thread_quit_flag_) {
        DLOG("MMU waiting for interrupt");
        mmu_interrupt_->Wait();
        DLOG("MMU Returned from interrupt wait!");

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

void MsdArmDevice::StartDeviceThread()
{
    DASSERT(!device_thread_.joinable());
    device_thread_ = std::thread([this] { this->DeviceThreadLoop(); });

    gpu_interrupt_thread_ = std::thread([this] { this->GpuInterruptThreadLoop(); });
    job_interrupt_thread_ = std::thread([this] { this->JobInterruptThreadLoop(); });
    mmu_interrupt_thread_ = std::thread([this] { this->MmuInterruptThreadLoop(); });
}

bool MsdArmDevice::InitializeInterrupts()
{
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

void MsdArmDevice::EnableInterrupts()
{
    auto gpu_flags = registers::GpuIrqFlags::GetIrqMask().FromValue(0xffffffff);
    gpu_flags.WriteTo(register_io_.get());

    auto mmu_flags = registers::MmuIrqFlags::GetIrqMask().FromValue(0xffffffff);
    mmu_flags.WriteTo(register_io_.get());

    auto job_flags = registers::JobIrqFlags::GetIrqMask().FromValue(0xffffffff);
    job_flags.WriteTo(register_io_.get());
}

void MsdArmDevice::DisableInterrupts()
{
    if (!register_io_)
        return;
    auto gpu_flags = registers::GpuIrqFlags::GetIrqMask().FromValue(0);
    gpu_flags.WriteTo(register_io_.get());

    auto mmu_flags = registers::MmuIrqFlags::GetIrqMask().FromValue(0);
    mmu_flags.WriteTo(register_io_.get());

    auto job_flags = registers::JobIrqFlags::GetIrqMask().FromValue(0);
    job_flags.WriteTo(register_io_.get());
}

void MsdArmDevice::EnqueueDeviceRequest(std::unique_ptr<DeviceRequest> request, bool enqueue_front)
{
    std::unique_lock<std::mutex> lock(device_request_mutex_);
    if (enqueue_front) {
        device_request_list_.emplace_front(std::move(request));
    } else {
        device_request_list_.emplace_back(std::move(request));
    }
    device_request_semaphore_->Signal();
}

void MsdArmDevice::ScheduleAtom(std::shared_ptr<MsdArmAtom> atom)
{
    bool need_schedule;
    {
        std::lock_guard<std::mutex> lock(schedule_mutex_);
        need_schedule = atoms_to_schedule_.empty();
        atoms_to_schedule_.push_back(std::move(atom));
    }
    if (need_schedule)
        EnqueueDeviceRequest(std::make_unique<ScheduleAtomRequest>());
}

void MsdArmDevice::CancelAtoms(std::shared_ptr<MsdArmConnection> connection)
{
    EnqueueDeviceRequest(std::make_unique<CancelAtomsRequest>(connection));
}

magma::PlatformPort* MsdArmDevice::GetPlatformPort() { return device_port_.get(); }

void MsdArmDevice::UpdateGpuActive(bool active) { power_manager_->UpdateGpuActive(active); }

void MsdArmDevice::DumpRegisters(const GpuFeatures& features, magma::RegisterIo* io,
                                 DumpState* dump_state)
{
    static struct {
        const char* name;
        registers::CoreReadyState::CoreType type;
    } core_types[] = {{"L2 Cache", registers::CoreReadyState::CoreType::kL2},
                      {"Shader", registers::CoreReadyState::CoreType::kShader},
                      {"Tiler", registers::CoreReadyState::CoreType::kTiler}};

    static struct {
        const char* name;
        registers::CoreReadyState::StatusType type;
    } status_types[] = {
        {"Present", registers::CoreReadyState::StatusType::kPresent},
        {"Ready", registers::CoreReadyState::StatusType::kReady},
        {"Transitioning", registers::CoreReadyState::StatusType::kPowerTransitioning},
        {"Power active", registers::CoreReadyState::StatusType::kPowerActive}};
    for (size_t i = 0; i < arraysize(core_types); i++) {
        for (size_t j = 0; j < arraysize(status_types); j++) {
            uint64_t bitmask = registers::CoreReadyState::ReadBitmask(io, core_types[i].type,
                                                                      status_types[j].type);
            dump_state->power_states.push_back({core_types[i].name, status_types[j].name, bitmask});
        }
    }

    dump_state->gpu_fault_status = registers::GpuFaultStatus::Get().ReadFrom(io).reg_value();
    dump_state->gpu_fault_address = registers::GpuFaultAddress::Get().ReadFrom(io).reg_value();
    dump_state->gpu_status = registers::GpuStatus::Get().ReadFrom(io).reg_value();
    dump_state->cycle_count = registers::CycleCount::Get().ReadFrom(io).reg_value();
    dump_state->timestamp = registers::Timestamp::Get().ReadFrom(io).reg_value();

    for (size_t i = 0; i < features.job_slot_count; i++) {
        DumpState::JobSlotStatus status;
        auto js_regs = registers::JobSlotRegisters(i);
        status.status = js_regs.Status().ReadFrom(io).reg_value();
        status.head = js_regs.Head().ReadFrom(io).reg_value();
        status.tail = js_regs.Tail().ReadFrom(io).reg_value();
        status.config = js_regs.Config().ReadFrom(io).reg_value();
        dump_state->job_slot_status.push_back(status);
    }

    for (size_t i = 0; i < features.address_space_count; i++) {
        DumpState::AddressSpaceStatus status;
        auto as_regs = registers::AsRegisters(i);
        status.status = as_regs.Status().ReadFrom(io).reg_value();
        status.fault_status = as_regs.FaultStatus().ReadFrom(io).reg_value();
        status.fault_address = as_regs.FaultAddress().ReadFrom(io).reg_value();
        dump_state->address_space_status.push_back(status);
    }
}

void MsdArmDevice::Dump(DumpState* dump_state)
{
    DumpRegisters(gpu_features_, register_io_.get(), dump_state);

    std::chrono::steady_clock::duration total_time;
    std::chrono::steady_clock::duration active_time;
    power_manager_->GetGpuActiveInfo(&total_time, &active_time);
    dump_state->total_time_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(total_time).count();
    dump_state->active_time_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(active_time).count();
}

void MsdArmDevice::DumpToString(std::string& dump_string)
{
    DumpState dump_state;
    Dump(&dump_state);

    FormatDump(dump_state, dump_string);
}

void MsdArmDevice::FormatDump(DumpState& dump_state, std::string& dump_string)
{
    dump_string.append("Core power states\n");
    for (auto& state : dump_state.power_states) {
        fxl::StringAppendf(&dump_string, "Core type %s state %s bitmap: 0x%lx\n", state.core_type,
                           state.status_type, state.bitmask);
    }
    fxl::StringAppendf(&dump_string, "Total ms %" PRIu64 " Active ms %" PRIu64 "\n",
                       dump_state.total_time_ms, dump_state.active_time_ms);
    fxl::StringAppendf(&dump_string, "Gpu fault status 0x%x, address 0x%lx\n",
                       dump_state.gpu_fault_status, dump_state.gpu_fault_address);
    fxl::StringAppendf(&dump_string, "Gpu status 0x%x\n", dump_state.gpu_status);
    fxl::StringAppendf(&dump_string, "Gpu cycle count %ld, timestamp %ld\n", dump_state.cycle_count,
                       dump_state.timestamp);
    for (size_t i = 0; i < dump_state.job_slot_status.size(); i++) {
        auto* status = &dump_state.job_slot_status[i];
        fxl::StringAppendf(&dump_string,
                           "Job slot %zu status 0x%x head 0x%lx tail 0x%lx config 0x%x\n", i,
                           status->status, status->head, status->tail, status->config);
    }
    for (size_t i = 0; i < dump_state.address_space_status.size(); i++) {
        auto* status = &dump_state.address_space_status[i];
        fxl::StringAppendf(&dump_string,
                           "AS %zu status 0x%x fault status 0x%x fault address 0x%lx\n", i,
                           status->status, status->fault_status, status->fault_address);
    }
}

magma::Status MsdArmDevice::ProcessDumpStatusToLog()
{
    std::string dump;
    DumpToString(dump);
    magma::log(magma::LOG_INFO, "%s", dump.c_str());
    return MAGMA_STATUS_OK;
}

magma::Status MsdArmDevice::ProcessScheduleAtoms()
{
    TRACE_DURATION("magma", "MsdArmDevice::ProcessScheduleAtoms");
    std::vector<std::shared_ptr<MsdArmAtom>> atoms_to_schedule;
    {
        std::lock_guard<std::mutex> lock(schedule_mutex_);
        atoms_to_schedule.swap(atoms_to_schedule_);
    }
    for (auto& atom : atoms_to_schedule)
        scheduler_->EnqueueAtom(std::move(atom));
    scheduler_->TryToSchedule();
    return MAGMA_STATUS_OK;
}

magma::Status MsdArmDevice::ProcessCancelAtoms(std::weak_ptr<MsdArmConnection> connection)
{
    // It's fine to cancel with an invalid shared_ptr, as that will clear out
    // atoms for connections that are dead already.
    scheduler_->CancelAtomsForConnection(connection.lock());
    return MAGMA_STATUS_OK;
}

void MsdArmDevice::ExecuteAtomOnDevice(MsdArmAtom* atom, magma::RegisterIo* register_io)
{
    TRACE_DURATION("magma", "ExecuteAtomOnDevice", "address", atom->gpu_address(), "slot",
                   atom->slot());
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

        if (++cycle_counter_refcount_ == 1) {
            register_io_->Write32(registers::GpuCommand::kOffset,
                                  registers::GpuCommand::kCmdCycleCountStart);
        }
    }

    registers::JobSlotRegisters slot(atom->slot());
    slot.HeadNext().FromValue(atom->gpu_address()).WriteTo(register_io);
    auto config = slot.ConfigNext().FromValue(0);
    config.address_space().set(atom->address_slot_mapping()->slot_number());
    config.start_flush_clean().set(true);
    config.start_flush_invalidate().set(true);
    // TODO(MA-367): Enable flush reduction optimization.
    config.thread_priority().set(8);
    config.end_flush_clean().set(true);
    config.end_flush_invalidate().set(true);
    config.WriteTo(register_io);

    // Execute on every powered-on core.
    slot.AffinityNext().FromValue(power_manager_->shader_ready_status()).WriteTo(register_io);
    slot.CommandNext().FromValue(registers::JobSlotCommand::kCommandStart).WriteTo(register_io);
}

void MsdArmDevice::RunAtom(MsdArmAtom* atom) { ExecuteAtomOnDevice(atom, register_io_.get()); }

void MsdArmDevice::AtomCompleted(MsdArmAtom* atom, ArmMaliResultCode result)
{
    TRACE_DURATION("magma", "AtomCompleted", "address", atom->gpu_address());
    DLOG("Completed job atom: 0x%lx\n", atom->gpu_address());
    address_manager_->AtomFinished(atom);
    if (atom->using_cycle_counter()) {
        DASSERT(atom->require_cycle_counter());

        if (--cycle_counter_refcount_ == 0) {
            register_io_->Write32(registers::GpuCommand::kOffset,
                                  registers::GpuCommand::kCmdCycleCountStop);
        }
        atom->set_using_cycle_counter(false);
    }
    // Soft stopped atoms will be retried, so this result shouldn't be reported.
    if (result != kArmMaliResultSoftStopped) {
        atom->set_result_code(result);
        auto connection = atom->connection().lock();
        if (connection)
            connection->SendNotificationData(atom, result);
    }
}

void MsdArmDevice::HardStopAtom(MsdArmAtom* atom)
{
    DASSERT(atom->hard_stopped());
    registers::JobSlotRegisters slot(atom->slot());
    DLOG("Hard stopping atom slot %d\n", atom->slot());
    slot.Command()
        .FromValue(registers::JobSlotCommand::kCommandHardStop)
        .WriteTo(register_io_.get());
}

void MsdArmDevice::SoftStopAtom(MsdArmAtom* atom)
{
    registers::JobSlotRegisters slot(atom->slot());
    DLOG("Soft stopping atom slot %d\n", atom->slot());
    slot.Command()
        .FromValue(registers::JobSlotCommand::kCommandSoftStop)
        .WriteTo(register_io_.get());
}

void MsdArmDevice::ReleaseMappingsForAtom(MsdArmAtom* atom)
{
    // The atom should be hung on a fault, so it won't reference memory
    // afterwards.
    address_manager_->AtomFinished(atom);
}

magma_status_t MsdArmDevice::QueryInfo(uint64_t id, uint64_t* value_out)
{
    switch (id) {
        case MAGMA_QUERY_DEVICE_ID:
            *value_out = gpu_features_.gpu_id.reg_value();
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

        default:
            return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "unhandled id %" PRIu64, id);
    }
}

void MsdArmDevice::RequestPerfCounterOperation(uint32_t type)
{
    EnqueueDeviceRequest(std::make_unique<PerfCounterRequest>(type));
}

magma::Status MsdArmDevice::ProcessPerfCounterRequest(uint32_t type)
{
    if (type == (MAGMA_DUMP_TYPE_PERF_COUNTER_ENABLE | MAGMA_DUMP_TYPE_PERF_COUNTERS)) {
        if (!perf_counters_->TriggerRead(true))
            return MAGMA_STATUS_INVALID_ARGS;
    } else if (type == MAGMA_DUMP_TYPE_PERF_COUNTERS) {
        if (!perf_counters_->TriggerRead(false))
            return MAGMA_STATUS_INVALID_ARGS;
    } else if (type == MAGMA_DUMP_TYPE_PERF_COUNTER_ENABLE) {
        if (!perf_counters_->Enable())
            return MAGMA_STATUS_INVALID_ARGS;
    } else {
        DASSERT(false);
        return MAGMA_STATUS_INVALID_ARGS;
    }
    return MAGMA_STATUS_OK;
}

//////////////////////////////////////////////////////////////////////////////////////////////////

msd_connection_t* msd_device_open(msd_device_t* dev, msd_client_id_t client_id)
{
    auto connection = MsdArmDevice::cast(dev)->Open(client_id);
    if (!connection)
        return DRETP(nullptr, "MsdArmDevice::Open failed");
    return new MsdArmAbiConnection(std::move(connection));
}

void msd_device_destroy(msd_device_t* dev) { delete MsdArmDevice::cast(dev); }

magma_status_t msd_device_query(msd_device_t* device, uint64_t id, uint64_t* value_out)
{
    return MsdArmDevice::cast(device)->QueryInfo(id, value_out);
}

void msd_device_dump_status(msd_device_t* device, uint32_t dump_type)
{
    uint32_t perf_dump_type =
        dump_type & (MAGMA_DUMP_TYPE_PERF_COUNTER_ENABLE | MAGMA_DUMP_TYPE_PERF_COUNTERS);
    if (perf_dump_type) {
        MsdArmDevice::cast(device)->RequestPerfCounterOperation(perf_dump_type);
    }
    if (!dump_type || (dump_type & MAGMA_DUMP_TYPE_NORMAL)) {
        MsdArmDevice::cast(device)->DumpStatusToLog();
    }
}
