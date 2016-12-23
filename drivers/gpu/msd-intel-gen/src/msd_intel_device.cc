// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd_intel_device.h"
#include "device_id.h"
#include "forcewake.h"
#include "global_context.h"
#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include "registers.h"
#include <cstdio>
#include <string>

class MsdIntelDevice::CommandBufferRequest : public DeviceRequest {
public:
    CommandBufferRequest(std::unique_ptr<CommandBuffer> command_buffer)
        : command_buffer_(std::move(command_buffer))
    {
    }

protected:
    void Process(MsdIntelDevice* device) override
    {
        device->ProcessCommandBuffer(std::move(command_buffer_));
    }

private:
    std::unique_ptr<CommandBuffer> command_buffer_;
};

class MsdIntelDevice::FlipRequest : public DeviceRequest {
public:
    FlipRequest(std::shared_ptr<MsdIntelBuffer> buffer, magma_system_pageflip_callback_t callback,
                void* data)
        : buffer_(std::move(buffer)), callback_(callback), data_(data)
    {
    }

protected:
    void Process(MsdIntelDevice* device) override
    {
        device->ProcessFlip(buffer_, callback_, data_);
    }

private:
    std::shared_ptr<MsdIntelBuffer> buffer_;
    magma_system_pageflip_callback_t callback_;
    void* data_;
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
    CHECK_THREAD_NOT_CURRENT(device_thread_id_);

    device_thread_quit_flag_ = true;

    if (monitor_)
        monitor_->Signal();

    if (device_thread_.joinable()) {
        DLOG("joining device thread");
        device_thread_.join();
        DLOG("joined");
    }
}

std::unique_ptr<MsdIntelConnection> MsdIntelDevice::Open(msd_client_id client_id)
{
    return std::unique_ptr<MsdIntelConnection>(new MsdIntelConnection(this));
}

bool MsdIntelDevice::Init(void* device_handle)
{
    DASSERT(!platform_device_);

    DLOG("Init device_handle %p", device_handle);

    platform_device_ = magma::PlatformDevice::Create(device_handle);
    if (!platform_device_)
        return DRETF(false, "failed to create platform device");

    uint16_t pci_dev_id;
    if (!platform_device_->ReadPciConfig16(2, &pci_dev_id))
        return DRETF(false, "ReadPciConfig16 failed");

    device_id_ = pci_dev_id;
    DLOG("device_id 0x%x", device_id_);

    uint16_t gmch_graphics_ctrl;
    if (!platform_device_->ReadPciConfig16(registers::GmchGraphicsControl::kOffset,
                                           &gmch_graphics_ctrl))
        return DRETF(false, "ReadPciConfig16 failed");

    uint32_t gtt_size = registers::GmchGraphicsControl::gtt_size(gmch_graphics_ctrl);

    DLOG("gtt_size: %uMB", gtt_size >> 20);

    std::unique_ptr<magma::PlatformMmio> mmio(
        platform_device_->CpuMapPciMmio(0, magma::PlatformMmio::CACHE_POLICY_UNCACHED_DEVICE));
    if (!mmio)
        return DRETF(false, "failed to map pci bar 0");

    register_io_ = std::unique_ptr<RegisterIo>(new RegisterIo(std::move(mmio)));

    if (DeviceId::is_gen8(device_id_)) {
        ForceWake::reset(register_io_.get(), registers::ForceWake::GEN8);
        ForceWake::request(register_io_.get(), registers::ForceWake::GEN8);
    } else if (DeviceId::is_gen9(device_id_)) {
        ForceWake::reset(register_io_.get(), registers::ForceWake::GEN9_RENDER);
        ForceWake::request(register_io_.get(), registers::ForceWake::GEN9_RENDER);
    } else {
        DASSERT(false);
    }

    // Clear faults
    registers::AllEngineFault::clear(register_io_.get());

    gtt_ = std::unique_ptr<Gtt>(new Gtt(this));

    if (!gtt_->Init(gtt_size, platform_device_.get()))
        return DRETF(false, "failed to Init gtt");

    // Arbitrary
    constexpr uint32_t kFirstSequenceNumber = 0x1000;
    sequencer_ = std::unique_ptr<Sequencer>(new Sequencer(kFirstSequenceNumber));

    render_engine_cs_ = RenderEngineCommandStreamer::Create(this);

    global_context_ = std::shared_ptr<GlobalContext>(new GlobalContext());

    // Creates the context backing store.
    if (!render_engine_cs_->InitContext(global_context_.get()))
        return DRETF(false, "render_engine_cs failed to init global context");

    if (!global_context_->Map(gtt_, render_engine_cs_->id()))
        return DRETF(false, "global context init failed");

    render_engine_cs_->InitHardware();

    auto init_batch = render_engine_cs_->CreateRenderInitBatch(device_id_);
    if (!init_batch)
        return DRETF(false, "failed to create render init batch");

    if (!render_engine_cs_->RenderInit(global_context_, std::move(init_batch), gtt_))
        return DRETF(false, "render_engine_cs failed RenderInit");

    monitor_ = magma::Monitor::CreateShared();

    return true;
}

void MsdIntelDevice::StartDeviceThread()
{
    DASSERT(!device_thread_.joinable());
    device_thread_ = std::thread(DeviceThreadEntry, this);
}

bool MsdIntelDevice::ReadGttSize(unsigned int* gtt_size)
{
    DASSERT(platform_device_);

    uint16_t reg;
    if (!platform_device_->ReadPciConfig16(registers::GmchGraphicsControl::kOffset, &reg))
        return DRETF(false, "ReadPciConfig16 failed");

    unsigned int size = (reg >> 6) & 0x3;
    *gtt_size = (size == 0) ? 0 : (1 << size) * 1024 * 1024;

    return true;
}

void MsdIntelDevice::Dump(DumpState* dump_out)
{
    dump_out->render_cs.sequence_number =
        global_context_->hardware_status_page(render_engine_cs_->id())->read_sequence_number();
    dump_out->render_cs.active_head_pointer = render_engine_cs_->GetActiveHeadPointer();

    DumpFault(dump_out, registers::AllEngineFault::read(register_io_.get()));

    dump_out->fault_gpu_address = kInvalidGpuAddr;
    if (dump_out->fault_present)
        DumpFaultAddress(dump_out, register_io_.get());
}

void MsdIntelDevice::DumpFault(DumpState* dump_out, uint32_t fault)
{
    dump_out->fault_present = registers::AllEngineFault::valid(fault);
    dump_out->fault_engine = registers::AllEngineFault::engine(fault);
    dump_out->fault_src = registers::AllEngineFault::src(fault);
    dump_out->fault_type = registers::AllEngineFault::type(fault);
}

void MsdIntelDevice::DumpFaultAddress(DumpState* dump_out, RegisterIo* register_io)
{
    dump_out->fault_gpu_address = registers::FaultTlbReadData::addr(register_io);
}

void MsdIntelDevice::DumpToString(std::string& dump_out)
{
    DumpState dump_state;
    Dump(&dump_state);

    const char* fmt = "Device id: 0x%x\n"
                      "RENDER_COMMAND_STREAMER\n"
                      "sequence_number 0x%x\n"
                      "active head pointer: 0x%llx\n";
    int size = std::snprintf(nullptr, 0, fmt, device_id(), dump_state.render_cs.sequence_number,
                             dump_state.render_cs.active_head_pointer);
    std::vector<char> buf(size + 1);
    std::snprintf(&buf[0], buf.size(), fmt, device_id(), dump_state.render_cs.sequence_number,
                  dump_state.render_cs.active_head_pointer);
    dump_out.append(&buf[0]);

    if (dump_state.fault_present) {
        fmt = "ENGINE FAULT DETECTED\n"
              "engine 0x%x src 0x%x type 0x%x gpu_address 0x%llx\n";
        size = std::snprintf(nullptr, 0, fmt, dump_state.fault_engine, dump_state.fault_src,
                             dump_state.fault_type, dump_state.fault_gpu_address);
        std::vector<char> buf(size + 1);
        std::snprintf(&buf[0], buf.size(), fmt, dump_state.fault_engine, dump_state.fault_src,
                      dump_state.fault_type);
        dump_out.append(&buf[0]);
    } else {
        dump_out.append("No engine faults detected.");
    }
}

bool MsdIntelDevice::SubmitCommandBuffer(std::unique_ptr<CommandBuffer> command_buffer)
{
    DLOG("SubmitCommandBuffer");
    CHECK_THREAD_NOT_CURRENT(device_thread_id_);

    auto request = std::make_unique<CommandBufferRequest>(std::move(command_buffer));
    auto reply = request->GetReply();

    EnqueueDeviceRequest(std::move(request));

    reply->Wait();

    DLOG("SubmitCommandBuffer returning");

    return true;
}

void MsdIntelDevice::Flip(std::shared_ptr<MsdIntelBuffer> buffer,
                          magma_system_pageflip_callback_t callback, void* data)
{
    DLOG("Flip");
    CHECK_THREAD_NOT_CURRENT(device_thread_id_);
    DASSERT(buffer);

    buffer->WaitRendering();

    auto request = std::make_unique<FlipRequest>(buffer, callback, data);
    auto reply = request->GetReply();

    EnqueueDeviceRequest(std::move(request));

    reply->Wait();
}

//////////////////////////////////////////////////////////////////////////////////////////////////

void MsdIntelDevice::EnqueueDeviceRequest(std::unique_ptr<DeviceRequest> request)
{
    DASSERT(monitor_);
    magma::Monitor::Lock lock(monitor_);
    lock.Acquire();
    device_request_list_.emplace_back(std::move(request));
    lock.Release();
    monitor_->Signal();
}

int MsdIntelDevice::DeviceThreadLoop()
{
    device_thread_id_ = std::make_unique<magma::PlatformThreadId>();
    CHECK_THREAD_IS_CURRENT(device_thread_id_);

    DLOG("DeviceThreadLoop starting thread 0x%x", device_thread_id_->id());

    DASSERT(monitor_);
    magma::Monitor::Lock lock(monitor_);
    lock.Acquire();

    // Reduced timeout to 1ms because to allow for prompt context scheduling
    // given the lack of interrupt telling us when the execlist submit port is ready.
    constexpr uint32_t kTimeoutMs = 1;
    std::chrono::high_resolution_clock::time_point time_point =
        std::chrono::high_resolution_clock::now() + std::chrono::milliseconds(kTimeoutMs);

    while (true) {
        bool timed_out = false;
        monitor_->WaitUntil(&lock, time_point, &timed_out);

        if (device_thread_quit_flag_)
            break;

        ProcessAllRequests(&lock);

        HangCheck();

        // TODO(US-86): only reset the time_point when timed_out (currently unreliable).
        time_point =
            std::chrono::high_resolution_clock::now() + std::chrono::milliseconds(kTimeoutMs);
    }

    lock.Release();

    // Ensure gpu is idle
    render_engine_cs_->Reset();

    DLOG("DeviceThreadLoop exit");
    return 0;
}

// If |lock| is non null then it should be in acquired state.
// The lock will be released and reacquired while processing device requests.
void MsdIntelDevice::ProcessAllRequests(magma::Monitor::Lock* lock)
{
    ProcessCompletedCommandBuffers(lock);
    ProcessDeviceRequests(lock);
}

// If |lock| is non null then it should be in acquired state.
void MsdIntelDevice::ProcessCompletedCommandBuffers(magma::Monitor::Lock* lock)
{
    CHECK_THREAD_IS_CURRENT(device_thread_id_);

    if (lock)
        DASSERT(lock->acquired(monitor_.get()));

    uint32_t last_completed_sequence_number;
    render_engine_cs_->ProcessCompletedCommandBuffers(&last_completed_sequence_number);

    progress_.Completed(last_completed_sequence_number);
}

void MsdIntelDevice::ProcessDeviceRequests(magma::Monitor::Lock* lock)
{
    CHECK_THREAD_IS_CURRENT(device_thread_id_);

    if (lock)
        DASSERT(lock->acquired(monitor_.get()));

    while (device_request_list_.size()) {
        DLOG("device_request_list_.size() %zu", device_request_list_.size());

        auto request = std::move(device_request_list_.front());
        device_request_list_.pop_front();

        if (lock)
            lock->Release();

        DASSERT(request);
        request->ProcessAndReply(this);

        if (lock)
            lock->Acquire();
    }
}

void MsdIntelDevice::HangCheck()
{
    if (progress_.work_outstanding()) {
        std::chrono::duration<double, std::milli> elapsed =
            std::chrono::high_resolution_clock::now() - progress_.hangcheck_time_start();
        constexpr uint32_t kHangCheckTimeoutMs = 100;
        if (elapsed.count() > kHangCheckTimeoutMs) {
            std::string s;
            DumpToString(s);
            printf("Suspected GPU hang: last submitted sequence number 0x%x\n\n%s\n",
                   progress_.last_submitted_sequence_number(), s.c_str());
            DASSERT(false);
        }
    }
}

void MsdIntelDevice::ProcessCommandBuffer(std::unique_ptr<CommandBuffer> command_buffer)
{
    CHECK_THREAD_IS_CURRENT(device_thread_id_);

    RequestMaxFreq();

    uint32_t sequence_number;
    if (!render_engine_cs_->ExecuteCommandBuffer(std::move(command_buffer), gtt(),
                                                 &sequence_number)) {
        DLOG("[WARNING] Failed to execute command buffer");
        return;
    }

    progress_.Submitted(sequence_number);
}

void MsdIntelDevice::ProcessFlip(std::shared_ptr<MsdIntelBuffer> buffer,
                                 magma_system_pageflip_callback_t callback, void* data)
{
    CHECK_THREAD_IS_CURRENT(device_thread_id_);
    DASSERT(buffer);

    std::shared_ptr<GpuMapping> mapping;

    for (auto iter = display_mappings_.begin(); iter != display_mappings_.end(); iter++) {
        if ((*iter)->buffer() == buffer.get()) {
            mapping = *iter;
            break;
        }
    }

    if (!mapping) {
        mapping = AddressSpace::GetSharedGpuMapping(gtt_, buffer, PAGE_SIZE);
        if (!mapping) {
            if (callback)
                (*callback)(DRET_MSG(MAGMA_STATUS_MEMORY_ERROR, "Couldn't map buffer to gtt"), data);
            return;
        }
        display_mappings_.push_front(mapping);
    }

    // Controls whether the plane surface update happens immediately or on the next vblank.
    constexpr bool kUpdateOnVblank = true;

    // Controls whether we wait for the flip to complete.
    // Waiting for flip completion seems to imply waiting for the vsync/vblank as well.
    // Note, if not waiting for flip complete you need to be careful of mapping lifetime.
    // For simplicity we just maintain all display buffer mappings forever but we should
    // have the upper layers import/release display buffers.
    constexpr bool kWaitForFlip = true;

    registers::DisplayPlaneControl::enable_update_on_vblank(
        register_io(), registers::DisplayPlaneControl::PIPE_A_PLANE_1, kUpdateOnVblank);

    registers::DisplayPlaneSurfaceAddress::write(
        register_io(), registers::DisplayPlaneSurfaceAddress::PIPE_A_PLANE_1, mapping->gpu_addr());

    if (kWaitForFlip) {
        registers::DisplayPipeInterrupt::update_mask_bits(
            register_io(), registers::DisplayPipeInterrupt::PIPE_A,
            registers::DisplayPipeInterrupt::kPlane1FlipDoneBit, true);

        constexpr uint32_t kRetryMsMax = 100;

        auto start = std::chrono::high_resolution_clock::now();

        while (true) {
            bool flip_done = false;

            registers::DisplayPipeInterrupt::process_identity_bits(
                register_io(), registers::DisplayPipeInterrupt::PIPE_A,
                registers::DisplayPipeInterrupt::kPlane1FlipDoneBit, &flip_done);
            if (flip_done)
                break;
            auto end = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double, std::milli> elapsed = end - start;
            if (elapsed.count() > kRetryMsMax) {
                DLOG("Timeout waiting for page flip event");
                if (callback)
                    (*callback)(MAGMA_STATUS_INTERNAL_ERROR, data);
                return;
            }
            std::this_thread::yield();
        }

        registers::DisplayPipeInterrupt::update_mask_bits(
            register_io(), registers::DisplayPipeInterrupt::PIPE_A,
            registers::DisplayPipeInterrupt::kPlane1FlipDoneBit, false);
    }

    if (flip_callback_) {
        DLOG("making flip callback now");
        (*flip_callback_)(0, flip_data_);
    }

    flip_callback_ = callback;
    flip_data_ = data;
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

    uint32_t max_freq =
        registers::RenderPerformanceStateCapability::read_rp0_frequency(register_io());
    registers::RenderPerformanceNormalFrequencyRequest::write_frequency_request(register_io(),
                                                                                max_freq);
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

//////////////////////////////////////////////////////////////////////////////////////////////////

msd_connection* msd_device_open(msd_device* dev, msd_client_id client_id)
{
    auto connection = MsdIntelDevice::cast(dev)->Open(client_id);
    if (!connection)
        return DRETP(nullptr, "MsdIntelDevice::Open failed");
    return new MsdIntelAbiConnection(std::move(connection));
}

void msd_device_destroy(msd_device* dev) { delete MsdIntelDevice::cast(dev); }

uint32_t msd_device_get_id(msd_device* dev) { return MsdIntelDevice::cast(dev)->device_id(); }

void msd_device_dump_status(struct msd_device* dev)
{
    std::string dump;
    MsdIntelDevice::cast(dev)->DumpToString(dump);
    printf("--------------------\n%s\n--------------------\n", dump.c_str());
}

void msd_device_page_flip(msd_device* dev, msd_buffer* buf,
                          magma_system_pageflip_callback_t callback, void* data)
{
    MsdIntelDevice::cast(dev)->Flip(MsdIntelAbiBuffer::cast(buf)->ptr(), callback, data);
}
