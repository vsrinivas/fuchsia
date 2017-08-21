// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd_intel_device.h"
#include "device_id.h"
#include "forcewake.h"
#include "global_context.h"
#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include "modeset/displayport.h"
#include "msd_intel_semaphore.h"
#include "platform_trace.h"
#include "registers.h"
#include "registers_pipe.h"
#include <bitset>
#include <cstdio>
#include <string>

constexpr bool kWaitForFlip = MSD_INTEL_WAIT_FOR_FLIP ? true : false;

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

class MsdIntelDevice::FlipRequest : public DeviceRequest {
public:
    FlipRequest(std::shared_ptr<MsdIntelBuffer> buffer, magma_system_image_descriptor* image_desc,
                std::vector<std::shared_ptr<magma::PlatformSemaphore>> wait_semaphores,
                std::vector<std::shared_ptr<magma::PlatformSemaphore>> signal_semaphores)
        : buffer_(std::move(buffer)), image_desc_(*image_desc),
          wait_semaphores_(std::move(wait_semaphores)),
          signal_semaphores_(std::move(signal_semaphores))
    {
    }

    // Takes ownership
    std::vector<std::shared_ptr<magma::PlatformSemaphore>> get_wait_semaphores()
    {
        return std::move(wait_semaphores_);
    }
    void set_wait_semaphore(std::shared_ptr<magma::PlatformSemaphore> semaphore)
    {
        wait_semaphores_.clear();
        wait_semaphores_.push_back(std::move(semaphore));
    }

protected:
    magma::Status Process(MsdIntelDevice* device) override
    {
        return device->ProcessFlip(buffer_, image_desc_, std::move(signal_semaphores_));
    }

private:
    std::shared_ptr<MsdIntelBuffer> buffer_;
    magma_system_image_descriptor image_desc_;
    std::vector<std::shared_ptr<magma::PlatformSemaphore>> wait_semaphores_;
    std::vector<std::shared_ptr<magma::PlatformSemaphore>> signal_semaphores_;
};

class MsdIntelDevice::InterruptRequest : public DeviceRequest {
public:
    InterruptRequest() {}

protected:
    magma::Status Process(MsdIntelDevice* device) override { return device->ProcessInterrupts(); }
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

    if (register_io_)
        registers::MasterInterruptControl::write(register_io_.get(), false);

    interrupt_thread_quit_flag_ = true;

    if (interrupt_)
        interrupt_->Signal();

    if (interrupt_thread_.joinable()) {
        DLOG("joining interrupt thread");
        interrupt_thread_.join();
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

std::unique_ptr<MsdIntelConnection> MsdIntelDevice::Open(msd_client_id client_id)
{
    return MsdIntelConnection::Create(this, scratch_buffer_);
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
        magma::log(magma::LOG_WARNING, "Unrecognized graphics PCI device id 0x%x", device_id_);
        return false;
    }

    // Clear faults
    registers::AllEngineFault::clear(register_io_.get());

    QuerySliceInfo(&subslice_total_, &eu_total_);
    ReadDisplaySize();

    interrupt_ = platform_device_->RegisterInterrupt();
    if (!interrupt_)
        return DRETF(false, "failed to register interrupt");

    PerProcessGtt::InitPrivatePat(register_io_.get());

#if MSD_INTEL_ENABLE_MAPPING_CACHE
    mapping_cache_ = GpuMappingCache::Create();
#endif

    gtt_ = std::make_shared<Gtt>(mapping_cache_);

    if (!gtt_->Init(gtt_size, platform_device_.get()))
        return DRETF(false, "failed to Init gtt");

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

    if (!RenderEngineInit())
        return DRETF(false, "failed to init render engine");

    device_request_semaphore_ = magma::PlatformSemaphore::Create();

    if (kWaitForFlip) {
        flip_ready_semaphore_ = magma::PlatformSemaphore::Create();
        flip_ready_semaphore_->Signal();
    }

    semaphore_port_ = magma::SemaphorePort::Create();

    scratch_buffer_ =
        std::shared_ptr<magma::PlatformBuffer>(magma::PlatformBuffer::Create(PAGE_SIZE, "scratch"));

    if (!scratch_buffer_->PinPages(0, 1))
        return DRETF(false, "failed to pin pages scratch buffer");

    registers::MasterInterruptControl::write(register_io_.get(), true);

#if MSD_INTEL_ENABLE_MODESETTING
    // The modesetting code is only tested on gen 9 (Skylake).
    if (DeviceId::is_gen9(device_id_))
        DisplayPort::PartiallyBringUpDisplays(register_io_.get());
#endif

    return true;
}

bool MsdIntelDevice::RenderEngineInit()
{
    CHECK_THREAD_IS_CURRENT(device_thread_id_);

    progress_ = std::make_unique<GpuProgress>();

    render_engine_cs_->InitHardware();

    auto init_batch = render_engine_cs_->CreateRenderInitBatch(device_id_);
    if (!init_batch)
        return DRETF(false, "failed to create render init batch");

    if (!render_engine_cs_->RenderInit(global_context_, std::move(init_batch), gtt_))
        return DRETF(false, "render_engine_cs failed RenderInit");

    registers::MasterInterruptControl::write(register_io_.get(), true);

    return true;
}

bool MsdIntelDevice::RenderEngineReset()
{
    magma::log(magma::LOG_WARNING, "resetting render engine");

    render_engine_cs_->ResetCurrentContext();

    registers::AllEngineFault::clear(register_io_.get());

    return RenderEngineInit();
}

void MsdIntelDevice::StartDeviceThread()
{
    DASSERT(!device_thread_.joinable());
    device_thread_ = std::thread([this] { this->DeviceThreadLoop(); });

    // TODO: move interrupt thread processing into device thread.
    // However for now, we need a separate interrupt thread and it
    // requires the device thread.
    DASSERT(!interrupt_thread_.joinable());
    interrupt_thread_ = std::thread([this] { this->InterruptThreadLoop(); });

    DASSERT(!wait_thread_.joinable());
    wait_thread_ = std::thread([this] { this->WaitThreadLoop(); });

    // TODO(MG-594): stop the wait thread like other threads
    wait_thread_.detach();
}

int MsdIntelDevice::InterruptThreadLoop()
{
    magma::PlatformThreadHelper::SetCurrentThreadName("InterruptThread");
    DLOG("Interrupt thread started");

    while (!interrupt_thread_quit_flag_) {
        DLOG("waiting for interrupt");
        interrupt_->Wait();
        DLOG("Returned from interrupt wait!");

        if (interrupt_thread_quit_flag_)
            break;

        auto request = std::make_unique<InterruptRequest>();
        auto reply = request->GetReply();

        EnqueueDeviceRequest(std::move(request), true);

        TRACE_DURATION("magma", "Interrupt Request Wait");
        reply->Wait();
    }

    DLOG("Interrupt thread exited");
    return 0;
}

void MsdIntelDevice::WaitThreadLoop()
{
    magma::PlatformThreadHelper::SetCurrentThreadName("FlipWaitThread");
    DLOG("Wait thread started");

    while (semaphore_port_->WaitOne()) {
    }

    DLOG("Wait thread exited");
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

void MsdIntelDevice::Flip(std::shared_ptr<MsdIntelBuffer> buffer,
                          magma_system_image_descriptor* image_desc,
                          std::vector<std::shared_ptr<magma::PlatformSemaphore>> wait_semaphores,
                          std::vector<std::shared_ptr<magma::PlatformSemaphore>> signal_semaphores)
{
    DLOG("Flip buffer %lu", buffer->platform_buffer()->id());

    CHECK_THREAD_NOT_CURRENT(device_thread_id_);
    DASSERT(buffer);

    TRACE_DURATION("magma", "Flip", "buffer", buffer->platform_buffer()->id());

    auto request = std::make_unique<FlipRequest>(buffer, image_desc, std::move(wait_semaphores),
                                                 std::move(signal_semaphores));

    std::unique_lock<std::mutex> lock(pageflip_request_mutex_);
    pageflip_pending_queue_.push(std::move(request));

    if (pageflip_pending_queue_.size() == 1)
        ProcessPendingFlip();
}

void MsdIntelDevice::ProcessPendingFlip()
{
    auto callback = [this](magma::SemaphorePort::WaitSet* wait_set) {
        std::unique_lock<std::mutex> lock(pageflip_request_mutex_);
        this->ProcessPendingFlip();
    };

    while (pageflip_pending_queue_.size()) {
        DLOG("pageflip_pending_queue_ size %zu", pageflip_pending_queue_.size());

        std::unique_ptr<FlipRequest>& request = pageflip_pending_queue_.front();

        // Takes ownership
        auto semaphores = request->get_wait_semaphores();

        if (semaphores.size() == 0) {
            if (kWaitForFlip)
                request->set_wait_semaphore(flip_ready_semaphore_);

            pageflip_pending_sync_queue_.push(std::move(request));
            pageflip_pending_queue_.pop();

            if (pageflip_pending_sync_queue_.size() == 1)
                ProcessPendingFlipSync();

        } else {
            DLOG("adding waitset with %zu semaphores, first %lu", semaphores.size(),
                 semaphores[0]->id());

            // Invoke the callback when semaphores are satisfied;
            // the next ProcessPendingFlip will see an empty semaphore array for the front request.
            bool result = semaphore_port_->AddWaitSet(
                std::make_unique<magma::SemaphorePort::WaitSet>(callback, std::move(semaphores)));
            if (result) {
                break;
            } else {
                magma::log(magma::LOG_WARNING, "ProcessPendingFlip: failed to add to waitset");
            }
        }
    }
}

void MsdIntelDevice::ProcessPendingFlipSync()
{
    auto callback = [this](magma::SemaphorePort::WaitSet* wait_set) {
        std::unique_lock<std::mutex> lock(pageflip_request_mutex_);
        this->ProcessPendingFlipSync();
    };

    while (pageflip_pending_sync_queue_.size()) {
        DLOG("pageflip_pending_sync_queue_ size %zu", pageflip_pending_sync_queue_.size());

        std::unique_ptr<FlipRequest>& request = pageflip_pending_sync_queue_.front();

        // Takes ownership
        auto semaphores = request->get_wait_semaphores();

        if (semaphores.size() == 0) {
            EnqueueDeviceRequest(std::move(request));
            pageflip_pending_sync_queue_.pop();
        } else {
            DASSERT(semaphores.size() == 1); // flip ready semaphore only
            DLOG("adding waitset with flip ready semaphore");
            bool result = semaphore_port_->AddWaitSet(
                std::make_unique<magma::SemaphorePort::WaitSet>(callback, std::move(semaphores)));
            if (result) {
                break;
            } else {
                magma::log(magma::LOG_WARNING, "ProcessPendingFlipSync: failed to add to waitset");
            }
        }
    }
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

    DLOG("DeviceThreadLoop starting thread 0x%x", device_thread_id_->id());

    constexpr uint32_t kTimeoutMs = 300;

    std::unique_lock<std::mutex> lock(device_request_mutex_, std::defer_lock);

    while (true) {
        if (progress_->work_outstanding()) {
            DLOG("waiting with timeout");
            // When the semaphore wait returns the semaphore will be reset.
            // The reset may race with subsequent enqueue/signals on the semaphore,
            // which is fine because we process everything available in the queue
            // before returning here to wait.
            bool timed_out = !device_request_semaphore_->Wait(kTimeoutMs);
            if (timed_out)
                SuspectedGpuHang();
        } else {
            DLOG("waiting, no timeout");
            device_request_semaphore_->Wait();
        }

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

void MsdIntelDevice::ProcessCompletedCommandBuffers()
{
    CHECK_THREAD_IS_CURRENT(device_thread_id_);
    TRACE_DURATION("magma", "ProcessCompletedCommandBuffers");

    uint32_t sequence_number =
        hardware_status_page(RENDER_COMMAND_STREAMER)->read_sequence_number();
    render_engine_cs_->ProcessCompletedCommandBuffers(sequence_number);

    progress_->Completed(sequence_number);
}

magma::Status MsdIntelDevice::ProcessInterrupts()
{
    uint32_t master_interrupt_control = registers::MasterInterruptControl::read(register_io_.get());
    DLOG("ProcessInterrupts 0x%08x", master_interrupt_control);

    TRACE_DURATION("magma", "ProcessInterrupts");

    registers::MasterInterruptControl::write(register_io_.get(), false);

    if (master_interrupt_control &
        registers::MasterInterruptControl::kRenderInterruptsPendingBitMask) {
        uint32_t val = registers::GtInterruptIdentity0::read(
            register_io(), registers::InterruptRegisterBase::RENDER_ENGINE);
        DLOG("gt IIR0 0x%08x", val);

        if (val & registers::InterruptRegisterBase::kUserInterruptBit) {
            registers::GtInterruptIdentity0::write(
                register_io(), registers::InterruptRegisterBase::RENDER_ENGINE,
                registers::InterruptRegisterBase::USER, registers::InterruptRegisterBase::MASK);

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

        } else {
            DASSERT(false);
        }
    }

    if (master_interrupt_control &
        registers::MasterInterruptControl::kDisplayEnginePipeAInterruptsPendingBit) {
        bool flip_done = false;
        registers::DisplayPipeInterrupt::process_identity_bits(
            register_io(), registers::DisplayPipeInterrupt::PIPE_A,
            registers::DisplayPipeInterrupt::kPlane1FlipDoneBit, &flip_done);
        DASSERT(flip_done);

        ProcessFlipComplete();
    }

    interrupt_->Complete();
    registers::MasterInterruptControl::write(register_io_.get(), true);

    return MAGMA_STATUS_OK;
}

magma::Status MsdIntelDevice::ProcessDumpStatusToLog()
{
    std::string dump;
    DumpToString(dump);
    magma::log(magma::LOG_INFO, "%s", dump.c_str());
    return MAGMA_STATUS_OK;
}

void MsdIntelDevice::SuspectedGpuHang()
{
    std::string s;
    DumpToString(s);
    uint32_t master_interrupt_control = registers::MasterInterruptControl::read(register_io_.get());
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

    auto connection = context->connection().lock();
    if (connection && connection->context_killed())
        return DRET_MSG(MAGMA_STATUS_CONTEXT_KILLED, "Connection context killed");

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

magma::Status MsdIntelDevice::ProcessFlip(
    std::shared_ptr<MsdIntelBuffer> buffer, const magma_system_image_descriptor& image_desc,
    std::vector<std::shared_ptr<magma::PlatformSemaphore>> signal_semaphores)
{
    CHECK_THREAD_IS_CURRENT(device_thread_id_);
    DASSERT(buffer);

#if MSD_INTEL_PRINT_FPS
    fps_printer_.OnNewFrame();
#endif

    TRACE_DURATION("magma", "ProcessFlip");
    DLOG("ProcessFlip buffer %lu", buffer->platform_buffer()->id());

    // Error indicators are passed to the callback
    magma::Status status(MAGMA_STATUS_OK);

    std::shared_ptr<GpuMapping> mapping =
        AddressSpace::GetSharedGpuMapping(gtt_, buffer, PAGE_SIZE);
    if (!mapping)
        return DRET_MSG(MAGMA_STATUS_MEMORY_ERROR, "Couldn't map buffer to gtt");

    uint32_t pipe_number = 0;
    registers::PipeRegs pipe(pipe_number);

    auto surface_size = pipe.PlaneSurfaceSize().ReadFrom(register_io());
    uint32_t width = surface_size.width_minus_1().get() + 1;

    // Controls whether the plane surface update happens immediately or on the next vblank.
    constexpr bool kUpdateOnVblank = true;

    auto plane_control = pipe.PlaneControl().ReadFrom(register_io());
    plane_control.async_address_update_enable().set(!kUpdateOnVblank);

    if (kWaitForFlip) {
        registers::DisplayPipeInterrupt::write_mask(
            register_io(), registers::DisplayPipeInterrupt::PIPE_A,
            registers::DisplayPipeInterrupt::kPlane1FlipDoneBit, true);
        registers::DisplayPipeInterrupt::write_enable(
            register_io(), registers::DisplayPipeInterrupt::PIPE_A,
            registers::DisplayPipeInterrupt::kPlane1FlipDoneBit, true);
    }

    constexpr uint32_t kCacheLineSize = 64;
    constexpr uint32_t kTileSize = 512;

    uint32_t stride;
    if (image_desc.tiling == MAGMA_IMAGE_TILING_OPTIMAL) {
        // Stride must be an integer number of tiles
        stride = magma::round_up(width * sizeof(uint32_t), kTileSize) / kTileSize;
        plane_control.tiled_surface().set(plane_control.TILING_X);
    } else {
        // Stride must be an integer number of cache lines
        stride = magma::round_up(width * sizeof(uint32_t), kCacheLineSize) / kCacheLineSize;
        plane_control.tiled_surface().set(plane_control.TILING_NONE);
    }
    plane_control.WriteTo(register_io());

    auto stride_reg = pipe.PlaneSurfaceStride().FromValue(0);
    stride_reg.stride().set(stride);
    stride_reg.WriteTo(register_io());

    auto addr_reg = pipe.PlaneSurfaceAddress().FromValue(0);
    DASSERT((mapping->gpu_addr() & ((1 << addr_reg.kPageShift) - 1)) == 0);
    addr_reg.surface_base_address().set(mapping->gpu_addr() >> addr_reg.kPageShift);
    addr_reg.WriteTo(register_io());

    saved_display_mapping_[1] = std::move(mapping);
    signal_semaphores_[1] = std::move(signal_semaphores);

    if (!kWaitForFlip)
        ProcessFlipComplete();

    return status;
}

void MsdIntelDevice::ProcessFlipComplete()
{
    TRACE_DURATION("magma", "ProcessFlipComplete");
    DLOG("ProcessFlipComplete");

    for (auto& semaphore : signal_semaphores_[0]) {
        DLOG("signalling flip semaphore 0x%" PRIx64 "\n", semaphore->id());
        semaphore->Signal();
    }
    signal_semaphores_[0] = std::move(signal_semaphores_[1]);
    saved_display_mapping_[0] = std::move(saved_display_mapping_[1]);

    if (kWaitForFlip)
        flip_ready_semaphore_->Signal();
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

void MsdIntelDevice::ReadDisplaySize()
{
    // Read the main display's resolution from the register state, assuming
    // that the display was set up by some previous modesetting code
    // (typically the firmware's boot-time modesetting).
    uint32_t pipe_number = 0;
    registers::PipeRegs pipe(pipe_number);
    auto surface_size = pipe.PlaneSurfaceSize().ReadFrom(register_io());
    display_size_.width = surface_size.width_minus_1().get() + 1;
    display_size_.height = surface_size.height_minus_1().get() + 1;
}

//////////////////////////////////////////////////////////////////////////////////////////////////

msd_connection_t* msd_device_open(msd_device_t* dev, msd_client_id client_id)
{
    auto connection = MsdIntelDevice::cast(dev)->Open(client_id);
    if (!connection)
        return DRETP(nullptr, "MsdIntelDevice::Open failed");
    return new MsdIntelAbiConnection(std::move(connection));
}

void msd_device_destroy(msd_device_t* dev) { delete MsdIntelDevice::cast(dev); }

uint32_t msd_device_get_id(msd_device_t* dev) { return MsdIntelDevice::cast(dev)->device_id(); }

magma_status_t msd_device_query(msd_device_t* device, uint64_t id, uint64_t* value_out)
{
    switch (id) {
        case MAGMA_QUERY_VENDOR_PARAM_0:
            *value_out = MsdIntelDevice::cast(device)->subslice_total();
            *value_out = (*value_out << 32) | MsdIntelDevice::cast(device)->eu_total();
            return MAGMA_STATUS_OK;
    }
    return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "unhandled id %" PRIu64, id);
}

void msd_device_dump_status(msd_device_t* device)
{
    MsdIntelDevice::cast(device)->DumpStatusToLog();
}

magma_status_t msd_device_display_get_size(msd_device_t* dev, magma_display_size* size_out)
{
    *size_out = MsdIntelDevice::cast(dev)->display_size();
    return MAGMA_STATUS_OK;
}

void msd_device_page_flip(msd_device_t* dev, msd_buffer_t* buf,
                          magma_system_image_descriptor* image_desc, uint32_t wait_semaphore_count,
                          uint32_t signal_semaphore_count, msd_semaphore_t** semaphores)
{
    std::vector<std::shared_ptr<magma::PlatformSemaphore>> wait_semaphores(wait_semaphore_count);
    uint32_t index = 0;
    for (uint32_t i = 0; i < wait_semaphore_count; i++) {
        wait_semaphores[i] = MsdIntelAbiSemaphore::cast(semaphores[index++])->ptr();
    }
    std::vector<std::shared_ptr<magma::PlatformSemaphore>> signal_semaphores(
        signal_semaphore_count);
    for (uint32_t i = 0; i < signal_semaphore_count; i++) {
        signal_semaphores[i] = MsdIntelAbiSemaphore::cast(semaphores[index++])->ptr();
    }

    MsdIntelDevice::cast(dev)->Flip(MsdIntelAbiBuffer::cast(buf)->ptr(), image_desc,
                                    std::move(wait_semaphores), std::move(signal_semaphores));
}
