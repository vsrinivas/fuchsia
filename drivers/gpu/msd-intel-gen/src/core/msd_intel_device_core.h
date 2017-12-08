// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MSD_INTEL_DEVICE_CORE_H
#define MSD_INTEL_DEVICE_CORE_H

#include "device_request.h"
#include "gtt.h"
#include "interrupt_manager.h"
#include "magma_util/fps_printer.h"
#include "magma_util/semaphore_port.h"
#include "magma_util/status.h"
#include "platform_pci_device.h"
#include "platform_semaphore.h"
#include <list>
#include <queue>
#include <thread>

// Implements core device functionality;
// May be replaced with a shim to a different driver.
class MsdIntelDeviceCore final : public Gtt::Owner, InterruptManager::Owner {
public:
    using DeviceRequest = DeviceRequest<MsdIntelDeviceCore>;

    magma::PlatformPciDevice* platform_device() override { return platform_device_.get(); }

    ~MsdIntelDeviceCore();

    bool RegisterCallback(InterruptManager::InterruptCallback callback, void* data,
                          uint32_t interrupt_mask)
    {
        if (forwarding_mask_)
            return DRETF(false, "callback already registered");

        DASSERT(callback);
        forwarding_data_ = data;
        forwarding_callback_ = callback;
        forwarding_mask_ = interrupt_mask;

        return true;
    }

    void DeleteInterruptManager() { interrupt_manager_.reset(); }

    Gtt* gtt() { return gtt_.get(); }

    void PresentBuffer(uint32_t buffer_handle, magma_system_image_descriptor* image_desc,
                       std::vector<std::shared_ptr<magma::PlatformSemaphore>> wait_semaphores,
                       std::vector<std::shared_ptr<magma::PlatformSemaphore>> signal_semaphores,
                       present_buffer_callback_t callback);

    static std::unique_ptr<MsdIntelDeviceCore> Create(void* device_handle);

private:
    class FlipRequest;
    class InterruptRequest;

    MsdIntelDeviceCore() {}

    bool Init(void* device_handle);
    void Destroy();

    RegisterIo* register_io_for_interrupt() override { return register_io_.get(); }
    RegisterIo* register_io() { return register_io_.get(); }

    magma::Status
    ProcessFlip(std::shared_ptr<MsdIntelBuffer> buffer,
                const magma_system_image_descriptor& image_desc,
                std::vector<std::shared_ptr<magma::PlatformSemaphore>> signal_semaphores,
                present_buffer_callback_t callback);
    magma::Status ProcessInterrupts(uint64_t interrupt_time_ns, uint32_t master_interrupt_control);

    void ProcessPendingFlip();
    void ProcessPendingFlipSync();
    void ProcessFlipComplete(uint64_t interrupt_time_ns);

    void EnqueueDeviceRequest(std::unique_ptr<DeviceRequest> request, bool enqueue_front = false);

    int DeviceThreadLoop();
    void WaitThreadLoop();
    static void InterruptCallback(void* data, uint32_t master_interrupt_control);

    std::thread device_thread_;
    std::thread wait_thread_;
    std::atomic_bool device_thread_quit_flag_{false};

    std::shared_ptr<Gtt> gtt_;
    std::unique_ptr<magma::PlatformPciDevice> platform_device_;
    std::unique_ptr<RegisterIo> register_io_;
    std::unique_ptr<InterruptManager> interrupt_manager_;

    std::mutex pageflip_request_mutex_;
    std::queue<std::unique_ptr<DeviceRequest>> pageflip_pending_queue_;
    std::queue<std::unique_ptr<DeviceRequest>> pageflip_pending_sync_queue_;

    std::unique_ptr<magma::PlatformSemaphore> device_request_semaphore_;
    std::mutex device_request_mutex_;
    std::list<std::unique_ptr<DeviceRequest>> device_request_list_;
    std::unique_ptr<magma::SemaphorePort> semaphore_port_;

    std::shared_ptr<magma::PlatformSemaphore> flip_ready_semaphore_;
    std::vector<std::shared_ptr<magma::PlatformSemaphore>> signal_semaphores_[2];
    std::shared_ptr<GpuMapping> saved_display_mapping_[2];
    present_buffer_callback_t flip_callback_;

    InterruptManager::InterruptCallback forwarding_callback_;
    void* forwarding_data_;
    std::atomic<uint32_t> forwarding_mask_{0};

    std::unordered_map<uint64_t, std::shared_ptr<GpuMapping>> mappings_;

    magma::FpsPrinter fps_printer_;
};

#endif // MSD_INTEL_DEVICE_CORE_H
