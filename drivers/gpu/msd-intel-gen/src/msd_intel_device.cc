// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd_intel_device.h"
#include "msd_intel_gen_query.h"
#include "device_id.h"
#include "forcewake.h"
#include "global_context.h"
#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include "msd_intel_semaphore.h"
#include "platform_trace.h"
#include "registers.h"
#include <bitset>
#include <cstdio>
#include <string>

inline uint64_t get_current_time_ns()
{
    return std::chrono::time_point_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now())
        .time_since_epoch()
        .count();
}

class MsdIntelDevice::CommandBufferRequest : public DeviceRequest {
public:
    CommandBufferRequest(std::unique_ptr<CommandBuffer> command_buffer)
        : command_buffer_(std::move(command_buffer))
    {
    }

protected:
    magma::Status Process(MsdIntelDevice* device) override
    {
        return device->ProcessCommandBuffer(std::move(command_buffer_));
    }

private:
    std::unique_ptr<CommandBuffer> command_buffer_;
};

class MsdIntelDevice::DestroyContextRequest : public DeviceRequest {
public:
    DestroyContextRequest(std::shared_ptr<ClientContext> client_context)
        : client_context_(std::move(client_context))
    {
    }

protected:
    magma::Status Process(MsdIntelDevice* device) override
    {
        return device->ProcessDestroyContext(std::move(client_context_));
    }

private:
    std::shared_ptr<ClientContext> client_context_;
};

class MsdIntelDevice::ReleaseBufferRequest : public DeviceRequest {
public:
    ReleaseBufferRequest(std::shared_ptr<AddressSpace> address_space,
                         std::shared_ptr<MsdIntelBuffer> buffer)
        : address_space_(std::move(address_space)), buffer_(std::move(buffer))
    {
    }

protected:
    magma::Status Process(MsdIntelDevice* device) override
    {
        return device->ProcessReleaseBuffer(std::move(address_space_), std::move(buffer_));
    }

private:
    std::shared_ptr<AddressSpace> address_space_;
    std::shared_ptr<MsdIntelBuffer> buffer_;
};

class MsdIntelDevice::InterruptRequest : public DeviceRequest {
public:
    InterruptRequest(uint64_t interrupt_time_ns, uint32_t master_interrupt_control,
                     uint32_t render_interrupt_status)
        : interrupt_time_ns_(interrupt_time_ns),
          master_interrupt_control_(master_interrupt_control),
          render_interrupt_status_(render_interrupt_status)
    {
    }

protected:
    magma::Status Process(MsdIntelDevice* device) override
    {
        return device->ProcessInterrupts(interrupt_time_ns_, master_interrupt_control_,
                                         render_interrupt_status_);
    }
    uint64_t interrupt_time_ns_;
    uint32_t master_interrupt_control_;
    uint32_t render_interrupt_status_;
};

class MsdIntelDevice::DumpRequest : public DeviceRequest {
public:
    DumpRequest() {}

protected:
    magma::Status Process(MsdIntelDevice* device) override
    {
        return device->ProcessDumpStatusToLog();
    }
};

//////////////////////////////////////////////////////////////////////////////////////////////////

std::unique_ptr<MsdIntelDevice> MsdIntelDevice::Create(void* device_handle,
                                                       bool start_device_thread)
{
    std::unique_ptr<MsdIntelDevice> device(new MsdIntelDevice());

    if (!device->Init(device_handle))
        return DRETP(nullptr, "Failed to initialize MsdIntelDevice");

    if (start_device_thread)
        device->StartDeviceThread();

    return device;
}

MsdIntelDevice::MsdIntelDevice() { magic_ = kMagic; }

MsdIntelDevice::~MsdIntelDevice() { Destroy(); }

void MsdIntelDevice::Destroy()
{
    DLOG("Destroy");
    CHECK_THREAD_NOT_CURRENT(device_thread_id_);

    interrupt_manager_.reset();

    device_thread_quit_flag_ = true;

    if (device_request_semaphore_)
        device_request_semaphore_->Signal();

    if (freq_monitor_context_)
        freq_monitor_context_->semaphore->Signal();

    if (device_thread_.joinable()) {
        DLOG("joining device thread");
        device_thread_.join();
        DLOG("joined");
    }
    if (freq_monitor_device_thread_.joinable()) {
        DLOG("joining freq monitor thread");
        freq_monitor_device_thread_.join();
        DLOG("joined");
    }
}

std::unique_ptr<MsdIntelConnection> MsdIntelDevice::Open(msd_client_id_t client_id)
{
    return MsdIntelConnection::Create(this, client_id);
}

bool MsdIntelDevice::Init(void* device_handle)
{
    if (!BaseInit(device_handle))
        return DRETF(false, "BaseInit failed");

    if (!RenderEngineInit(true))
        return DRETF(false, "RenderEngineInit failed");

    return true;
}

bool MsdIntelDevice::BaseInit(void* device_handle)
{
    DASSERT(!platform_device_);

    DLOG("Init device_handle %p", device_handle);

    platform_device_ = MsdIntelPciDevice::CreateShim(device_handle);
    if (!platform_device_)
        return DRETF(false, "failed to create pci device");

    uint16_t pci_dev_id;
    if (!platform_device_->ReadPciConfig16(2, &pci_dev_id))
        return DRETF(false, "ReadPciConfig16 failed");

    uint16_t revision;
    if (!platform_device_->ReadPciConfig16(8, &revision))
        return DRETF(false, "ReadPciConfig16 failed");

    revision_ = revision & 0xFF;

    device_id_ = pci_dev_id;
    DLOG("device_id 0x%x revision 0x%x", device_id_, revision);

    std::unique_ptr<magma::PlatformMmio> mmio(
        platform_device_->CpuMapPciMmio(0, magma::PlatformMmio::CACHE_POLICY_UNCACHED_DEVICE));
    if (!mmio)
        return DRETF(false, "failed to map pci bar 0");

    register_io_ = std::make_unique<magma::RegisterIo>(std::move(mmio));

    if (DeviceId::is_gen9(device_id_)) {
        ForceWake::reset(register_io_.get(), registers::ForceWake::GEN9_RENDER);
        ForceWake::request(register_io_.get(), registers::ForceWake::GEN9_RENDER);
    } else {
        magma::log(magma::LOG_WARNING, "Unrecognized graphics PCI device id 0x%x", device_id_);
        return false;
    }

    bus_mapper_ = magma::PlatformBusMapper::Create(platform_device_->GetBusTransactionInitiator());
    if (!bus_mapper_)
        return DRETF(false, "failed to create bus mapper");

    // Clear faults
    registers::AllEngineFault::clear(register_io_.get());

    QuerySliceInfo(&subslice_total_, &eu_total_);

    interrupt_manager_ = InterruptManager::CreateShim(this);
    if (!interrupt_manager_)
        return DRETF(false, "failed to create interrupt manager");

    PerProcessGtt::InitPrivatePat(register_io_.get());

    gtt_ = std::shared_ptr<Gtt>(Gtt::CreateShim(this));

    // Arbitrary
    constexpr uint32_t kFirstSequenceNumber = 0x1000;
    sequencer_ = std::unique_ptr<Sequencer>(new Sequencer(kFirstSequenceNumber));

    render_engine_cs_ = RenderEngineCommandStreamer::Create(this);

    global_context_ = std::shared_ptr<GlobalContext>(new GlobalContext(gtt_));

    // Creates the context backing store.
    if (!render_engine_cs_->InitContext(global_context_.get()))
        return DRETF(false, "render_engine_cs failed to init global context");

    if (!global_context_->Map(gtt_, render_engine_cs_->id()))
        return DRETF(false, "global context init failed");

    device_request_semaphore_ = magma::PlatformSemaphore::Create();

    return true;
}

bool MsdIntelDevice::RenderEngineInit(bool execute_init_batch)
{
    CHECK_THREAD_IS_CURRENT(device_thread_id_);

    progress_ = std::make_unique<GpuProgress>();

    render_engine_cs_->InitHardware();

    if (execute_init_batch) {
        auto init_batch = render_engine_cs_->CreateRenderInitBatch(device_id_);
        if (!init_batch)
            return DRETF(false, "failed to create render init batch");

        if (!render_engine_cs_->RenderInit(global_context_, std::move(init_batch), gtt_))
            return DRETF(false, "render_engine_cs failed RenderInit");
    }

    return true;
}

bool MsdIntelDevice::RenderEngineReset()
{
    magma::log(magma::LOG_WARNING, "resetting render engine");

    render_engine_cs_->ResetCurrentContext();

    registers::AllEngineFault::clear(register_io_.get());

    return RenderEngineInit(true);
}

void MsdIntelDevice::StartDeviceThread()
{
    DASSERT(!device_thread_.joinable());
    device_thread_ = std::thread([this] { this->DeviceThreadLoop(); });

    if (MAGMA_ENABLE_TRACING) {
        freq_monitor_context_ = std::make_shared<FreqMonitorContext>();
        freq_monitor_device_thread_ =
            std::thread([this] { this->FrequencyMonitorDeviceThreadLoop(); });

        magma::PlatformTrace::Get()->SetObserver([weak_context = std::weak_ptr<FreqMonitorContext>(
                                                      freq_monitor_context_)](bool is_enabled) {
            auto context = weak_context.lock();
            if (context && context->tracing_enabled != is_enabled) {
                context->tracing_enabled = is_enabled;
                if (context->tracing_enabled)
                    context->semaphore->Signal();
            }
        });
    }

    // Don't start interrupt processing until the device thread is running.
    interrupt_manager_->RegisterCallback(
        InterruptCallback, this,
        registers::MasterInterruptControl::kRenderInterruptsPendingBitMask);
}

void MsdIntelDevice::InterruptCallback(void* data, uint32_t master_interrupt_control)
{
    DASSERT(data);
    auto device = reinterpret_cast<MsdIntelDevice*>(data);

    magma::RegisterIo* register_io = device->register_io_for_interrupt();
    uint64_t now = get_current_time_ns();
    uint32_t render_interrupt_status = 0;

    if (master_interrupt_control &
        registers::MasterInterruptControl::kRenderInterruptsPendingBitMask) {

        render_interrupt_status = registers::GtInterruptIdentity0::read(
            register_io, registers::InterruptRegisterBase::RENDER_ENGINE);
        DLOG("gt IIR0 0x%08x", render_interrupt_status);

        if (render_interrupt_status & registers::InterruptRegisterBase::kUserInterruptBit) {
            registers::GtInterruptIdentity0::clear(register_io,
                                                   registers::InterruptRegisterBase::RENDER_ENGINE,
                                                   registers::InterruptRegisterBase::USER);
        }
        if (render_interrupt_status & registers::InterruptRegisterBase::kContextSwitchBit) {
            registers::GtInterruptIdentity0::clear(
                register_io, registers::InterruptRegisterBase::RENDER_ENGINE,
                registers::InterruptRegisterBase::CONTEXT_SWITCH);
        }

        device->EnqueueDeviceRequest(std::make_unique<InterruptRequest>(
            now, master_interrupt_control, render_interrupt_status));
    }
}

void MsdIntelDevice::DumpStatusToLog() { EnqueueDeviceRequest(std::make_unique<DumpRequest>()); }

magma::Status MsdIntelDevice::SubmitCommandBuffer(std::unique_ptr<CommandBuffer> command_buffer)
{
    DLOG("SubmitCommandBuffer");
    CHECK_THREAD_NOT_CURRENT(device_thread_id_);

    EnqueueDeviceRequest(std::make_unique<CommandBufferRequest>(std::move(command_buffer)));
    return MAGMA_STATUS_OK;
}

void MsdIntelDevice::DestroyContext(std::shared_ptr<ClientContext> client_context)
{
    DLOG("DestroyContext");
    CHECK_THREAD_NOT_CURRENT(device_thread_id_);

    EnqueueDeviceRequest(std::make_unique<DestroyContextRequest>(std::move(client_context)));
}

void MsdIntelDevice::ReleaseBuffer(std::shared_ptr<AddressSpace> address_space,
                                   std::shared_ptr<MsdIntelBuffer> buffer)
{
    DLOG("ReleaseBuffer");
    CHECK_THREAD_NOT_CURRENT(device_thread_id_);

    EnqueueDeviceRequest(
        std::make_unique<ReleaseBufferRequest>(std::move(address_space), std::move(buffer)));
}

//////////////////////////////////////////////////////////////////////////////////////////////////

void MsdIntelDevice::EnqueueDeviceRequest(std::unique_ptr<DeviceRequest> request,
                                          bool enqueue_front)
{
    TRACE_DURATION("magma", "EnqueueDeviceRequest");
    std::unique_lock<std::mutex> lock(device_request_mutex_);
    if (enqueue_front) {
        device_request_list_.emplace_front(std::move(request));
    } else {
        device_request_list_.emplace_back(std::move(request));
    }
    device_request_semaphore_->Signal();
}

int MsdIntelDevice::DeviceThreadLoop()
{
    magma::PlatformThreadHelper::SetCurrentThreadName("DeviceThread");

    device_thread_id_ = std::make_unique<magma::PlatformThreadId>();
    CHECK_THREAD_IS_CURRENT(device_thread_id_);

    DLOG("DeviceThreadLoop starting thread 0x%lx", device_thread_id_->id());

    constexpr uint32_t kTimeoutMs = 1000;

    std::unique_lock<std::mutex> lock(device_request_mutex_, std::defer_lock);

    while (true) {
        auto timeout = std::chrono::duration_cast<std::chrono::milliseconds>(
            progress_->GetHangcheckTimeout(kTimeoutMs, std::chrono::steady_clock::now()));
        // When the semaphore wait returns the semaphore will be reset.
        // The reset may race with subsequent enqueue/signals on the semaphore,
        // which is fine because we process everything available in the queue
        // before returning here to wait.
        bool timed_out = !device_request_semaphore_->Wait(timeout.count());
        if (timed_out)
            HangCheckTimeout();

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

    // Ensure gpu is idle
    render_engine_cs_->Reset();

    DLOG("DeviceThreadLoop exit");
    return 0;
}

void MsdIntelDevice::FrequencyMonitorDeviceThreadLoop()
{
    magma::PlatformThreadHelper::SetCurrentThreadName("FrequencyMonitorDeviceThread");

    while (!device_thread_quit_flag_ && freq_monitor_context_->semaphore->Wait()) {
        DLOG("begin frequency monitoring");

        while (!device_thread_quit_flag_ && freq_monitor_context_->tracing_enabled) {
            auto registers = register_io_.get(); // bypass thread id check
            uint32_t actual_mhz =
                registers::RenderPerformanceStatus::read_current_frequency_gen9(registers);
            uint32_t requested_mhz =
                registers::RenderPerformanceNormalFrequencyRequest::read(registers);
            TRACE_COUNTER("magma", "gpu freq", 0, "request_mhz", requested_mhz, "actual_mhz",
                          actual_mhz);
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }

        DLOG("stop frequency monitoring");
    }

    DLOG("FrequencyMonitorDeviceThreadLoop exit");
}

void MsdIntelDevice::ProcessCompletedCommandBuffers()
{
    CHECK_THREAD_IS_CURRENT(device_thread_id_);
    TRACE_DURATION("magma", "ProcessCompletedCommandBuffers");

    uint32_t sequence_number =
        hardware_status_page(RENDER_COMMAND_STREAMER)->read_sequence_number();
    render_engine_cs_->ProcessCompletedCommandBuffers(sequence_number);

    progress_->Completed(sequence_number, std::chrono::steady_clock::now());
}

magma::Status MsdIntelDevice::ProcessInterrupts(uint64_t interrupt_time_ns,
                                                uint32_t master_interrupt_control,
                                                uint32_t render_interrupt_status)
{
    DLOG("ProcessInterrupts 0x%08x", master_interrupt_control);

    TRACE_DURATION("magma", "ProcessInterrupts");

    if (master_interrupt_control &
        registers::MasterInterruptControl::kRenderInterruptsPendingBitMask) {

        if (render_interrupt_status & registers::InterruptRegisterBase::kUserInterruptBit) {
            bool fault = registers::AllEngineFault::read(register_io_.get()) &
                         registers::AllEngineFault::kValid;
            if (fault) {
                std::string s;
                DumpToString(s);
                magma::log(magma::LOG_WARNING, "GPU fault detected\n%s", s.c_str());
                RenderEngineReset();
            } else {
                ProcessCompletedCommandBuffers();
            }
        }
        if (render_interrupt_status & registers::InterruptRegisterBase::kContextSwitchBit) {
            render_engine_cs_->ContextSwitched();
        }
    }

    return MAGMA_STATUS_OK;
}

magma::Status MsdIntelDevice::ProcessDumpStatusToLog()
{
    std::string dump;
    DumpToString(dump);
    magma::log(magma::LOG_INFO, "%s", dump.c_str());
    return MAGMA_STATUS_OK;
}

void MsdIntelDevice::HangCheckTimeout()
{
    std::string s;
    DumpToString(s);
    uint32_t master_interrupt_control = registers::MasterInterruptControl::read(register_io_.get());
    if (master_interrupt_control &
        registers::MasterInterruptControl::kRenderInterruptsPendingBitMask) {
        magma::log(magma::LOG_WARNING,
                   "Hang check timeout while pending render interrupt; slow interrupt handler?\n"
                   "last submitted sequence number 0x%x master_interrupt_control 0x%08x\n%s",
                   progress_->last_submitted_sequence_number(), master_interrupt_control,
                   s.c_str());
        return;
    }
    magma::log(magma::LOG_WARNING,
               "Suspected GPU hang: last submitted sequence number "
               "0x%x master_interrupt_control 0x%08x\n%s",
               progress_->last_submitted_sequence_number(), master_interrupt_control, s.c_str());
    RenderEngineReset();
}

magma::Status MsdIntelDevice::ProcessCommandBuffer(std::unique_ptr<CommandBuffer> command_buffer)
{
    CHECK_THREAD_IS_CURRENT(device_thread_id_);
    TRACE_DURATION("magma", "ProcessCommandBuffer");

    DLOG("preparing command buffer for execution");

    auto context = command_buffer->GetContext().lock();
    DASSERT(context);

    if (context->killed())
        return DRET_MSG(MAGMA_STATUS_CONTEXT_KILLED, "Context killed");

    TRACE_DURATION_BEGIN("magma", "PrepareForExecution",
                         "id", command_buffer->GetBatchBufferId());
    if (!command_buffer->PrepareForExecution(render_engine_cs_.get(), gtt()))
        return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR,
                        "Failed to prepare command buffer for execution");
    TRACE_DURATION_END("magma", "PrepareForExecution");

    TRACE_DURATION_BEGIN("magma", "SubmitCommandBuffer");
    render_engine_cs_->SubmitCommandBuffer(std::move(command_buffer));
    TRACE_DURATION_END("magma", "SubmitCommandBuffer");

    RequestMaxFreq();

    return MAGMA_STATUS_OK;
}

magma::Status MsdIntelDevice::ProcessDestroyContext(std::shared_ptr<ClientContext> client_context)
{
    DLOG("ProcessDestroyContext");
    TRACE_DURATION("magma", "ProcessDestroyContext");

    CHECK_THREAD_IS_CURRENT(device_thread_id_);
    // Just let it go out of scope

    return MAGMA_STATUS_OK;
}

magma::Status MsdIntelDevice::ProcessReleaseBuffer(std::shared_ptr<AddressSpace> address_space,
                                                   std::shared_ptr<MsdIntelBuffer> buffer)
{
    DLOG("ProcessReleaseBuffer");
    TRACE_DURATION("magma", "ProcessReleaseBuffer");

    CHECK_THREAD_IS_CURRENT(device_thread_id_);
    address_space->RemoveCachedMappings(buffer.get());

    return MAGMA_STATUS_OK;
}

bool MsdIntelDevice::WaitIdle()
{
    CHECK_THREAD_IS_CURRENT(device_thread_id_);

    if (!render_engine_cs_->WaitIdle()) {
        std::string s;
        DumpToString(s);
        printf("WaitRendering timed out!\n\n%s\n", s.c_str());
        return false;
    }
    return true;
}

void MsdIntelDevice::RequestMaxFreq()
{
    CHECK_THREAD_IS_CURRENT(device_thread_id_);

    uint32_t mhz = registers::RenderPerformanceStateCapability::read_rp0_frequency(register_io());
    registers::RenderPerformanceNormalFrequencyRequest::write_frequency_request_gen9(register_io(),
                                                                                     mhz);
}

uint32_t MsdIntelDevice::GetCurrentFrequency()
{
    CHECK_THREAD_IS_CURRENT(device_thread_id_);

    if (DeviceId::is_gen9(device_id_))
        return registers::RenderPerformanceStatus::read_current_frequency_gen9(register_io());

    DLOG("GetCurrentGraphicsFrequency not implemented");
    return 0;
}

HardwareStatusPage* MsdIntelDevice::hardware_status_page(EngineCommandStreamerId id)
{
    CHECK_THREAD_IS_CURRENT(device_thread_id_);
    DASSERT(global_context_);
    return global_context_->hardware_status_page(id);
}

void MsdIntelDevice::QuerySliceInfo(uint32_t* subslice_total_out, uint32_t* eu_total_out)
{
    uint32_t slice_enable_mask;
    uint32_t subslice_enable_mask;

    registers::Fuse2ControlDwordMirror::read(register_io_.get(), &slice_enable_mask,
                                             &subslice_enable_mask);

    DLOG("slice_enable_mask 0x%x subslice_enable_mask 0x%x", slice_enable_mask,
         subslice_enable_mask);

    std::bitset<registers::MirrorEuDisable::kMaxSliceCount> slice_bitset(slice_enable_mask);
    std::bitset<registers::MirrorEuDisable::kMaxSubsliceCount> subslice_bitset(
        subslice_enable_mask);

    *subslice_total_out = slice_bitset.count() * subslice_bitset.count();
    *eu_total_out = 0;

    for (uint32_t slice = 0; slice < registers::MirrorEuDisable::kMaxSliceCount; slice++) {
        if ((slice_enable_mask & (1 << slice)) == 0)
            continue; // skip disabled slice

        std::vector<uint32_t> eu_disable_mask;
        registers::MirrorEuDisable::read(register_io_.get(), slice, eu_disable_mask);

        for (uint32_t subslice = 0; subslice < eu_disable_mask.size(); subslice++) {
            if ((subslice_enable_mask & (1 << subslice)) == 0)
                continue; // skip disabled subslice

            DLOG("subslice %u eu_disable_mask 0x%x", subslice, eu_disable_mask[subslice]);

            uint32_t eu_disable_count =
                std::bitset<registers::MirrorEuDisable::kEuPerSubslice>(eu_disable_mask[subslice])
                    .count();
            *eu_total_out += registers::MirrorEuDisable::kEuPerSubslice - eu_disable_count;
        }
    }
}

//////////////////////////////////////////////////////////////////////////////////////////////////

msd_connection_t* msd_device_open(msd_device_t* dev, msd_client_id_t client_id)
{
    auto connection = MsdIntelDevice::cast(dev)->Open(client_id);
    if (!connection)
        return DRETP(nullptr, "MsdIntelDevice::Open failed");
    return new MsdIntelAbiConnection(std::move(connection));
}

void msd_device_destroy(msd_device_t* dev) { delete MsdIntelDevice::cast(dev); }

magma_status_t msd_device_query(msd_device_t* device, uint64_t id, uint64_t* value_out)
{
    switch (id) {
        case MAGMA_QUERY_DEVICE_ID:
            *value_out = MsdIntelDevice::cast(device)->device_id();
            return MAGMA_STATUS_OK;

        case kMsdIntelGenQuerySubsliceAndEuTotal:
            *value_out = MsdIntelDevice::cast(device)->subslice_total();
            *value_out = (*value_out << 32) | MsdIntelDevice::cast(device)->eu_total();
            return MAGMA_STATUS_OK;

        case kMsdIntelGenQueryGttSize:
            *value_out = 1ul << 48;
            return MAGMA_STATUS_OK;
    }
    return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "unhandled id %" PRIu64, id);
}

void msd_device_dump_status(msd_device_t* device)
{
    MsdIntelDevice::cast(device)->DumpStatusToLog();
}
