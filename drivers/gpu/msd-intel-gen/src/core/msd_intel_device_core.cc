// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd_intel_device_core.h"
#include "platform_thread.h"
#include "platform_trace.h"
#include "registers.h"
#include "registers_pipe.h"

constexpr bool kWaitForFlip = MSD_INTEL_WAIT_FOR_FLIP ? true : false;

inline uint64_t get_current_time_ns()
{
    return std::chrono::time_point_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now())
        .time_since_epoch()
        .count();
}

class MsdIntelDeviceCore::FlipRequest : public DeviceRequest {
public:
    FlipRequest(std::shared_ptr<MsdIntelBuffer> buffer, magma_system_image_descriptor* image_desc,
                std::vector<std::shared_ptr<magma::PlatformSemaphore>> wait_semaphores,
                std::vector<std::shared_ptr<magma::PlatformSemaphore>> signal_semaphores,
                present_buffer_callback_t callback)
        : buffer_(std::move(buffer)), image_desc_(*image_desc),
          wait_semaphores_(std::move(wait_semaphores)),
          signal_semaphores_(std::move(signal_semaphores)), callback_(callback)
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
    magma::Status Process(MsdIntelDeviceCore* device) override
    {
        return device->ProcessFlip(buffer_, image_desc_, std::move(signal_semaphores_),
                                   std::move(callback_));
    }

private:
    std::shared_ptr<MsdIntelBuffer> buffer_;
    magma_system_image_descriptor image_desc_;
    std::vector<std::shared_ptr<magma::PlatformSemaphore>> wait_semaphores_;
    std::vector<std::shared_ptr<magma::PlatformSemaphore>> signal_semaphores_;
    present_buffer_callback_t callback_;
};

class MsdIntelDeviceCore::InterruptRequest : public DeviceRequest {
public:
    InterruptRequest(uint64_t interrupt_time_ns, uint32_t master_interrupt_control)
        : interrupt_time_ns_(interrupt_time_ns), master_interrupt_control_(master_interrupt_control)
    {
    }

protected:
    magma::Status Process(MsdIntelDeviceCore* device) override
    {
        return device->ProcessInterrupts(interrupt_time_ns_, master_interrupt_control_);
    }
    uint64_t interrupt_time_ns_;
    uint32_t master_interrupt_control_;
};

MsdIntelDeviceCore::~MsdIntelDeviceCore() { Destroy(); }

std::unique_ptr<MsdIntelDeviceCore> MsdIntelDeviceCore::Create(void* device_handle)
{
    auto device = std::unique_ptr<MsdIntelDeviceCore>(new MsdIntelDeviceCore);
    if (!device->Init(device_handle))
        return DRETP(nullptr, "couldn't init device");

    return device;
}

bool MsdIntelDeviceCore::Init(void* device_handle)
{
    DASSERT(!platform_device_);
    DLOG("Init device_handle %p", device_handle);

    platform_device_ = magma::PlatformPciDevice::Create(device_handle);
    if (!platform_device_)
        return DRETF(false, "failed to create platform device");

    std::unique_ptr<magma::PlatformMmio> mmio(
        platform_device_->CpuMapPciMmio(0, magma::PlatformMmio::CACHE_POLICY_UNCACHED_DEVICE));
    if (!mmio)
        return DRETF(false, "failed to map pci bar 0");

    register_io_ = std::unique_ptr<RegisterIo>(new RegisterIo(std::move(mmio)));

    gtt_ = Gtt::CreateCore(this);

    interrupt_manager_ = InterruptManager::CreateCore(this);
    if (!interrupt_manager_)
        return DRETF(false, "failed to create interrupt manager");

    // Register for all interrupts
    if (!interrupt_manager_->RegisterCallback(InterruptCallback, this, ~0))
        return DRETF(false, "couldn't register interrupt callback");

    device_request_semaphore_ = magma::PlatformSemaphore::Create();

    semaphore_port_ = magma::SemaphorePort::Create();

    if (kWaitForFlip) {
        flip_ready_semaphore_ = magma::PlatformSemaphore::Create();
        flip_ready_semaphore_->Signal();
    }

    device_thread_ = std::thread([this] { this->DeviceThreadLoop(); });
    wait_thread_ = std::thread([this] { this->WaitThreadLoop(); });

    return true;
}

void MsdIntelDeviceCore::Destroy()
{
    device_thread_quit_flag_ = true;

    if (device_request_semaphore_)
        device_request_semaphore_->Signal();

    if (semaphore_port_)
        semaphore_port_->Close();

    if (device_thread_.joinable()) {
        DLOG("joining device thread");
        device_thread_.join();
        DLOG("joined");
    }
    if (wait_thread_.joinable()) {
        DLOG("joining wait thread");
        wait_thread_.join();
        DLOG("joined");
    }
}

void MsdIntelDeviceCore::PresentBuffer(
    uint32_t buffer_handle, magma_system_image_descriptor* image_desc,
    std::vector<std::shared_ptr<magma::PlatformSemaphore>> wait_semaphores,
    std::vector<std::shared_ptr<magma::PlatformSemaphore>> signal_semaphores,
    present_buffer_callback_t callback)
{
    auto buffer = std::shared_ptr<MsdIntelBuffer>(MsdIntelBuffer::Import(buffer_handle));
    if (!buffer) {
        magma::log(magma::LOG_WARNING, "Couldn't import buffer_handle; can't present this buffer");
        return;
    }

    DLOG("Present buffer %lu", buffer->platform_buffer()->id());
    DASSERT(buffer);

    TRACE_DURATION("magma", "Flip", "buffer", buffer->platform_buffer()->id());

    auto request = std::make_unique<FlipRequest>(buffer, image_desc, std::move(wait_semaphores),
                                                 std::move(signal_semaphores), std::move(callback));

    std::unique_lock<std::mutex> lock(pageflip_request_mutex_);
    pageflip_pending_queue_.push(std::move(request));

    if (pageflip_pending_queue_.size() == 1)
        ProcessPendingFlip();
}

void MsdIntelDeviceCore::ProcessPendingFlip()
{
    auto callback = [this](magma::SemaphorePort::WaitSet* wait_set) {
        std::unique_lock<std::mutex> lock(pageflip_request_mutex_);
        this->ProcessPendingFlip();
    };

    while (pageflip_pending_queue_.size()) {
        DLOG("pageflip_pending_queue_ size %zu", pageflip_pending_queue_.size());

        std::unique_ptr<DeviceRequest>& device_request = pageflip_pending_queue_.front();
        auto request = static_cast<FlipRequest*>(device_request.get());

        // Takes ownership
        auto semaphores = request->get_wait_semaphores();

        if (semaphores.size() == 0) {
            if (kWaitForFlip)
                request->set_wait_semaphore(flip_ready_semaphore_);

            pageflip_pending_sync_queue_.push(std::move(device_request));
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

void MsdIntelDeviceCore::ProcessPendingFlipSync()
{
    auto callback = [this](magma::SemaphorePort::WaitSet* wait_set) {
        std::unique_lock<std::mutex> lock(pageflip_request_mutex_);
        this->ProcessPendingFlipSync();
    };

    while (pageflip_pending_sync_queue_.size()) {
        DLOG("pageflip_pending_sync_queue_ size %zu", pageflip_pending_sync_queue_.size());

        std::unique_ptr<DeviceRequest>& device_request = pageflip_pending_sync_queue_.front();

        // Takes ownership
        auto semaphores = static_cast<FlipRequest*>(device_request.get())->get_wait_semaphores();

        if (semaphores.size() == 0) {
            EnqueueDeviceRequest(std::move(device_request));
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

void MsdIntelDeviceCore::EnqueueDeviceRequest(std::unique_ptr<DeviceRequest> request,
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

int MsdIntelDeviceCore::DeviceThreadLoop()
{
    magma::PlatformThreadHelper::SetCurrentThreadName("CoreDeviceThread");

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

    DLOG("CoreDeviceThreadLoop exit");
    return 0;
}

void MsdIntelDeviceCore::WaitThreadLoop()
{
    magma::PlatformThreadHelper::SetCurrentThreadName("CoreFlipWaitThread");
    DLOG("Core Wait thread started");

    while (semaphore_port_->WaitOne()) {
    }

    DLOG("Core Wait thread exited");
}

void MsdIntelDeviceCore::InterruptCallback(void* data, uint32_t master_interrupt_control)
{
    DASSERT(data);
    auto device = reinterpret_cast<MsdIntelDeviceCore*>(data);

    uint32_t status = device->forwarding_mask_ & master_interrupt_control;
    if (status)
        device->forwarding_callback_(device->forwarding_data_, status);

    if (master_interrupt_control &
        registers::MasterInterruptControl::kDisplayEnginePipeAInterruptsPendingBit) {
        auto request =
            std::make_unique<InterruptRequest>(get_current_time_ns(), master_interrupt_control);
        auto reply = request->GetReply();

        device->EnqueueDeviceRequest(std::move(request), true);

        TRACE_DURATION("magma", "Core Interrupt Request Wait");
        reply->Wait();
    }
}

magma::Status MsdIntelDeviceCore::ProcessFlip(
    std::shared_ptr<MsdIntelBuffer> buffer, const magma_system_image_descriptor& image_desc,
    std::vector<std::shared_ptr<magma::PlatformSemaphore>> signal_semaphores,
    present_buffer_callback_t callback)
{
    DASSERT(buffer);

#if MSD_INTEL_PRINT_FPS
    fps_printer_.OnNewFrame();
#endif

    TRACE_DURATION("magma", "ProcessFlip");
    DLOG("ProcessFlip buffer %lu", buffer->platform_buffer()->id());

    // Error indicators are passed to the callback
    magma::Status status(MAGMA_STATUS_OK);

    auto iter = mappings_.find(buffer->platform_buffer()->id());
    if (iter == mappings_.end()) {
        std::unique_ptr<GpuMapping> mapping = AddressSpace::MapBufferGpu(
            gtt_, buffer, 0, buffer->platform_buffer()->size(), PAGE_SIZE);
        if (!mapping) {
            if (callback)
                callback(MAGMA_STATUS_MEMORY_ERROR, 0);
            return DRET_MSG(MAGMA_STATUS_MEMORY_ERROR, "Couldn't map buffer to gtt");
        }
        mappings_[buffer->platform_buffer()->id()] = std::move(mapping);
        iter = mappings_.find(buffer->platform_buffer()->id());
        DASSERT(iter != mappings_.end());
    }

    std::shared_ptr<GpuMapping> mapping = iter->second;
    DASSERT(mapping);

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

    flip_callback_ = std::move(callback);

    if (!kWaitForFlip)
        ProcessFlipComplete(get_current_time_ns());

    return status;
}

void MsdIntelDeviceCore::ProcessFlipComplete(uint64_t interrupt_time_ns)
{
    TRACE_DURATION("magma", "ProcessFlipComplete");
    DLOG("ProcessFlipComplete");

    if (flip_callback_)
        flip_callback_(MAGMA_STATUS_OK, interrupt_time_ns);

    for (auto& semaphore : signal_semaphores_[0]) {
        DLOG("signalling flip semaphore 0x%" PRIx64 "\n", semaphore->id());
        semaphore->Signal();
    }
    signal_semaphores_[0] = std::move(signal_semaphores_[1]);
    saved_display_mapping_[0] = std::move(saved_display_mapping_[1]);

    if (kWaitForFlip)
        flip_ready_semaphore_->Signal();
}

magma::Status MsdIntelDeviceCore::ProcessInterrupts(uint64_t interrupt_time_ns,
                                                    uint32_t master_interrupt_control)
{
    DLOG("ProcessInterrupts 0x%08x", master_interrupt_control);

    TRACE_DURATION("magma", "CoreProcessInterrupts");

    if (master_interrupt_control &
        registers::MasterInterruptControl::kDisplayEnginePipeAInterruptsPendingBit) {
        bool flip_done = false;
        registers::DisplayPipeInterrupt::process_identity_bits(
            register_io(), registers::DisplayPipeInterrupt::PIPE_A,
            registers::DisplayPipeInterrupt::kPlane1FlipDoneBit, &flip_done);
        DASSERT(flip_done);

        ProcessFlipComplete(interrupt_time_ns);
    }

    return MAGMA_STATUS_OK;
}
