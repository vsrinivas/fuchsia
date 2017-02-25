// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MSD_DEVICE_H
#define MSD_DEVICE_H

#include "device_request.h"
#include "engine_command_streamer.h"
#include "global_context.h"
#include "gpu_progress.h"
#include "gtt.h"
#include "magma_util/macros.h"
#include "magma_util/monitor.h"
#include "magma_util/semaphore_port.h"
#include "magma_util/thread.h"
#include "msd.h"
#include "msd_intel_connection.h"
#include "platform_device.h"
#include "register_io.h"
#include "sequencer.h"
#include <deque>
#include <list>
#include <thread>

class MsdIntelDevice : public msd_device_t,
                       public EngineCommandStreamer::Owner,
                       public MsdIntelConnection::Owner {
public:
    // Creates a device for the given |device_handle| and returns ownership.
    // If |start_device_thread| is false, then StartDeviceThread should be called
    // to enable device request processing.
    static std::unique_ptr<MsdIntelDevice> Create(void* device_handle, bool start_device_thread);

    virtual ~MsdIntelDevice();

    // This takes ownership of the connection so that ownership can be
    // transferred across the MSD ABI by the caller
    std::unique_ptr<MsdIntelConnection> Open(msd_client_id client_id);

    uint32_t device_id() { return device_id_; }

    static MsdIntelDevice* cast(msd_device_t* dev)
    {
        DASSERT(dev);
        DASSERT(dev->magic_ == kMagic);
        return static_cast<MsdIntelDevice*>(dev);
    }

    bool Init(void* device_handle);

    struct DumpState {
        struct RenderCommandStreamer {
            uint32_t sequence_number;
            uint64_t active_head_pointer;
        } render_cs;

        bool fault_present;
        uint8_t fault_engine;
        uint8_t fault_src;
        uint8_t fault_type;
        uint64_t fault_gpu_address;
    };

    void Dump(DumpState* dump_state);
    void DumpToString(std::string& dump_string);

    void Flip(std::shared_ptr<MsdIntelBuffer> buffer, magma_system_image_descriptor* image_desc,
              std::vector<std::shared_ptr<magma::PlatformSemaphore>> wait_semaphores,
              std::vector<std::shared_ptr<magma::PlatformSemaphore>> signal_semaphores);

private:
#define CHECK_THREAD_IS_CURRENT(x)                                                                 \
    if (x)                                                                                         \
    DASSERT(magma::ThreadIdCheck::IsCurrent(*x))

#define CHECK_THREAD_NOT_CURRENT(x)                                                                \
    if (x)                                                                                         \
    DASSERT(!magma::ThreadIdCheck::IsCurrent(*x))

    // EngineCommandStreamer::Owner
    RegisterIo* register_io() override
    {
        CHECK_THREAD_IS_CURRENT(device_thread_id_);
        DASSERT(register_io_);
        return register_io_.get();
    }

    // EngineCommandStreamer::Owner
    Sequencer* sequencer() override
    {
        CHECK_THREAD_IS_CURRENT(device_thread_id_);
        DASSERT(sequencer_);
        return sequencer_.get();
    }

    // EngineCommandStreamer::Owner
    HardwareStatusPage* hardware_status_page(EngineCommandStreamerId id) override;

    void batch_submitted(uint32_t sequence_number) override
    {
        DASSERT(progress_);
        progress_->Submitted(sequence_number);
    }

    // MsdIntelConnection::Owner
    magma::Status SubmitCommandBuffer(std::unique_ptr<CommandBuffer> cmd_buf) override;
    void DestroyContext(std::shared_ptr<ClientContext> client_context) override;
    std::shared_ptr<GpuMappingCache> mapping_cache() override { return mapping_cache_; }

private:
    MsdIntelDevice();

    void StartDeviceThread();

    void Destroy();
    bool RenderEngineInit();
    bool RenderEngineReset();

    void ProcessDeviceRequests(magma::Monitor::Lock* lock);
    void ProcessCompletedCommandBuffers();
    void HangCheck(uint64_t timeout_ms);

    magma::Status ProcessCommandBuffer(std::unique_ptr<CommandBuffer> command_buffer);
    magma::Status ProcessDestroyContext(std::shared_ptr<ClientContext> client_context);
    magma::Status
    ProcessFlip(std::shared_ptr<MsdIntelBuffer> buffer,
                const magma_system_image_descriptor& image_desc,
                std::vector<std::shared_ptr<magma::PlatformSemaphore>> signal_semaphores);
    magma::Status ProcessInterrupts();

    void ProcessPendingFlip();
    void EnqueueDeviceRequest(std::unique_ptr<DeviceRequest> request);

    bool WaitIdle();

    uint32_t GetCurrentFrequency();
    void RequestMaxFreq();

    static void DumpFault(DumpState* dump_out, uint32_t fault);
    static void DumpFaultAddress(DumpState* dump_out, RegisterIo* register_io);

    int DeviceThreadLoop();
    int InterruptThreadLoop();
    void WaitThreadLoop();

    std::shared_ptr<GlobalContext> global_context() { return global_context_; }

    RenderEngineCommandStreamer* render_engine_cs() { return render_engine_cs_.get(); }

    std::shared_ptr<AddressSpace> gtt() { return gtt_; }

    std::shared_ptr<magma::PlatformBuffer> scratch_buffer() { return scratch_buffer_; }

    static const uint32_t kMagic = 0x64657669; //"devi"

    uint32_t device_id_{};

    std::thread device_thread_;
    std::unique_ptr<magma::PlatformThreadId> device_thread_id_;
    bool device_thread_quit_flag_ = false;
    bool interrupt_thread_quit_flag_ = false;
    std::unique_ptr<GpuProgress> progress_;

    std::thread interrupt_thread_;
    std::thread wait_thread_;

    std::unique_ptr<magma::PlatformDevice> platform_device_;
    std::unique_ptr<RegisterIo> register_io_;
    std::shared_ptr<Gtt> gtt_;
    std::unique_ptr<RenderEngineCommandStreamer> render_engine_cs_;
    std::shared_ptr<GlobalContext> global_context_;
    std::unique_ptr<Sequencer> sequencer_;
    std::shared_ptr<magma::PlatformBuffer> scratch_buffer_;
    std::unique_ptr<magma::PlatformInterrupt> interrupt_;
    std::shared_ptr<GpuMappingCache> mapping_cache_;
    std::unique_ptr<magma::SemaphorePort> semaphore_port_;

    // page flipping
    std::deque<std::shared_ptr<GpuMapping>> display_mappings_;
    std::vector<std::shared_ptr<magma::PlatformSemaphore>> signal_semaphores_;

    class CommandBufferRequest;
    class FlipRequest;
    class DestroyContextRequest;
    class InterruptRequest;

    // Thread-shared data members
    std::shared_ptr<magma::Monitor> monitor_;
    std::list<std::unique_ptr<DeviceRequest>> device_request_list_;

    std::mutex pageflip_request_mutex_;
    std::queue<std::unique_ptr<FlipRequest>> pageflip_pending_queue_;

    friend class TestMsdIntelDevice;
    friend class TestCommandBuffer;
};

#endif // MSD_DEVICE_H
