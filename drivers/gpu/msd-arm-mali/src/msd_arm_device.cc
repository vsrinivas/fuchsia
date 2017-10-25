// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd_arm_device.h"
#include "lib/fxl/arraysize.h"
#include "lib/fxl/strings/string_printf.h"
#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include "magma_vendor_queries.h"
#include <bitset>
#include <cstdio>
#include <ddk/debug.h>
#include <string>

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

    register_io_ = std::make_unique<RegisterIo>(std::move(mmio));

    gpu_features_.ReadFrom(register_io_.get());
    magma::log(magma::LOG_INFO, "ARM mali ID %x", gpu_features_.gpu_id.reg_value());

    device_request_semaphore_ = magma::PlatformSemaphore::Create();

    power_manager_ = std::make_unique<PowerManager>();

    if (!InitializeInterrupts())
        return false;

    EnableInterrupts();

    power_manager_->EnableCores(register_io_.get());
    return true;
}

std::unique_ptr<MsdArmConnection> MsdArmDevice::Open(msd_client_id_t client_id)
{
    return MsdArmConnection::Create(client_id);
}

void MsdArmDevice::DumpStatusToLog() { EnqueueDeviceRequest(std::make_unique<DumpRequest>()); }

int MsdArmDevice::DeviceThreadLoop()
{
    magma::PlatformThreadHelper::SetCurrentThreadName("DeviceThread");

    device_thread_id_ = std::make_unique<magma::PlatformThreadId>();
    CHECK_THREAD_IS_CURRENT(device_thread_id_);

    DLOG("DeviceThreadLoop starting thread 0x%lx", device_thread_id_->id());

    std::unique_lock<std::mutex> lock(device_request_mutex_, std::defer_lock);

    while (true) {
        device_request_semaphore_->Wait();

        while (true) {
            lock.lock();
            if (!device_request_list_.size())
                break;
            auto request = std::move(device_request_list_.front());
            device_request_list_.pop_front();
            lock.unlock();
            request->ProcessAndReply(this);
        }
        lock.unlock();

        if (device_thread_quit_flag_)
            break;
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
    }

    if (irq_status.reg_value())
        magma::log(magma::LOG_WARNING, "Got unexpected GPU IRQ %d\n", irq_status.reg_value());
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

        auto irq_status = registers::JobIrqFlags::GetStatus().ReadFrom(register_io_.get());
        auto clear_flags = registers::JobIrqFlags::GetIrqClear().FromValue(irq_status.reg_value());
        clear_flags.WriteTo(register_io_.get());

        magma::log(magma::LOG_WARNING, "Got unexpected Job IRQ %d\n", irq_status.reg_value());
        job_interrupt_->Complete();
    }

    DLOG("Job Interrupt thread exited");
    return 0;
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

        auto irq_status = registers::MmuIrqFlags::GetStatus().ReadFrom(register_io_.get());
        auto clear_flags = registers::MmuIrqFlags::GetIrqClear().FromValue(irq_status.reg_value());
        clear_flags.WriteTo(register_io_.get());

        magma::log(magma::LOG_WARNING, "Got unexpected MMU IRQ %d\n", irq_status.reg_value());

        mmu_interrupt_->Complete();
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

void MsdArmDevice::DumpRegisters(RegisterIo* io, DumpState* dump_state)
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

    for (size_t i = 0; i < arraysize(dump_state->job_slot_status); i++) {
        dump_state->job_slot_status[i] =
            registers::JobSlotRegisters(i).Status().ReadFrom(io).reg_value();
    }

    for (size_t i = 0; i < arraysize(dump_state->address_space_status); i++) {
        auto* status = &dump_state->address_space_status[i];
        auto as_regs = registers::AsRegisters(i);
        status->status = as_regs.Status().ReadFrom(io).reg_value();
        status->fault_status = as_regs.FaultStatus().ReadFrom(io).reg_value();
        status->fault_address = as_regs.FaultAddress().ReadFrom(io).reg_value();
    }
}

void MsdArmDevice::Dump(DumpState* dump_state) { DumpRegisters(register_io_.get(), dump_state); }

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
    fxl::StringAppendf(&dump_string, "Gpu fault status 0x%x, address 0x%lx\n",
                       dump_state.gpu_fault_status, dump_state.gpu_fault_address);
    for (size_t i = 0; i < arraysize(dump_state.job_slot_status); i++) {
        fxl::StringAppendf(&dump_string, "Job slot %zu status %x\n", i,
                           dump_state.job_slot_status[i]);
    }
    for (size_t i = 0; i < arraysize(dump_state.address_space_status); i++) {
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

magma_status_t MsdArmDevice::QueryInfo(uint64_t id, uint64_t* value_out)
{
    switch (id) {
        case MAGMA_QUERY_DEVICE_ID:
            *value_out = gpu_features_.gpu_id.reg_value();
            return MAGMA_STATUS_OK;

        case kMsdArmVendorQueryL2Present:
            *value_out = gpu_features_.l2_present;
            return MAGMA_STATUS_OK;

        default:
            return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "unhandled id %" PRIu64, id);
    }
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

void msd_device_dump_status(msd_device_t* device) { MsdArmDevice::cast(device)->DumpStatusToLog(); }

magma_status_t msd_device_display_get_size(msd_device_t* dev, magma_display_size* size_out)
{
    return MAGMA_STATUS_OK;
}
