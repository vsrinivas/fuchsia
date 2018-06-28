// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MSD_INTEL_DEVICE_H
#define MSD_INTEL_DEVICE_H

#include <list>
#include <mutex>
#include <thread>

#include "device_request.h"
#include "engine_command_streamer.h"
#include "global_context.h"
#include "gpu_progress.h"
#include "gtt.h"
#include "interrupt_manager.h"
#include "magma_util/macros.h"
#include "magma_util/register_io.h"
#include "magma_util/thread.h"
#include "msd.h"
#include "msd_intel_connection.h"
#include "msd_intel_pci_device.h"
#include "platform_semaphore.h"
#include "sequencer.h"

class MsdIntelDevice : public msd_device_t,
                       public EngineCommandStreamer::Owner,
                       public Gtt::Owner,
                       public InterruptManager::Owner,
                       public MsdIntelConnection::Owner {
public:
    using DeviceRequest = DeviceRequest<MsdIntelDevice>;

    // Creates a device for the given |device_handle| and returns ownership.
    // If |start_device_thread| is false, then StartDeviceThread should be called
    // to enable device request processing.
    static std::unique_ptr<MsdIntelDevice> Create(void* device_handle, bool start_device_thread);

    virtual ~MsdIntelDevice();

    // This takes ownership of the connection so that ownership can be
    // transferred across the MSD ABI by the caller
    std::unique_ptr<MsdIntelConnection> Open(msd_client_id_t client_id);

    uint32_t device_id() { return device_id_; }
    uint32_t revision() { return revision_; }
    uint32_t subslice_total() { return subslice_total_; }
    uint32_t eu_total() { return eu_total_; }

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
            std::vector<MappedBatch*> inflight_batches;
        } render_cs;

        bool fault_present;
        uint8_t fault_engine;
        uint8_t fault_src;
        uint8_t fault_type;
        uint64_t fault_gpu_address;
        bool global;
    };

    void Dump(DumpState* dump_state);
    void DumpToString(std::string& dump_string);
    void DumpStatusToLog();

private:
    MsdIntelDevice();

#define CHECK_THREAD_IS_CURRENT(x)                                                                 \
    if (x)                                                                                         \
    DASSERT(magma::ThreadIdCheck::IsCurrent(*x))

#define CHECK_THREAD_NOT_CURRENT(x)                                                                \
    if (x)                                                                                         \
    DASSERT(!magma::ThreadIdCheck::IsCurrent(*x))

    // EngineCommandStreamer::Owner
    magma::RegisterIo* register_io() override
    {
        CHECK_THREAD_IS_CURRENT(device_thread_id_);
        DASSERT(register_io_);
        return register_io_.get();
    }

    // InterruptManager::Owner
    magma::RegisterIo* register_io_for_interrupt() override
    {
        DASSERT(register_io_);
        return register_io_.get();
    }
    magma::PlatformPciDevice* platform_device() override
    {
        DASSERT(platform_device_);
        return platform_device_.get();
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
        progress_->Submitted(sequence_number, std::chrono::steady_clock::now());
    }

    // MsdIntelConnection::Owner
    magma::Status SubmitCommandBuffer(std::unique_ptr<CommandBuffer> cmd_buf) override;
    void DestroyContext(std::shared_ptr<ClientContext> client_context) override;
    void ReleaseBuffer(std::shared_ptr<AddressSpace> address_space,
                       std::shared_ptr<MsdIntelBuffer> buffer) override;
    magma::PlatformBusMapper* GetBusMapper() override { return bus_mapper_.get(); }

    void StartDeviceThread();

    void Destroy();

    bool BaseInit(void* device_handle);
    bool RenderEngineInit(bool exec_init_batch);
    bool RenderEngineReset();

    void ProcessCompletedCommandBuffers();
    void HangCheckTimeout();

    magma::Status ProcessCommandBuffer(std::unique_ptr<CommandBuffer> command_buffer);
    magma::Status ProcessDestroyContext(std::shared_ptr<ClientContext> client_context);
    magma::Status ProcessReleaseBuffer(std::shared_ptr<AddressSpace> address_space,
                                       std::shared_ptr<MsdIntelBuffer> buffer);
    magma::Status ProcessInterrupts(uint64_t interrupt_time_ns, uint32_t master_interrupt_control,
                                    uint32_t render_interrupt_status);
    magma::Status ProcessDumpStatusToLog();

    void EnqueueDeviceRequest(std::unique_ptr<DeviceRequest> request, bool enqueue_front = false);

    bool WaitIdle();

    uint32_t GetCurrentFrequency();
    void RequestMaxFreq();

    static void DumpFault(DumpState* dump_out, uint32_t fault);
    static void DumpFaultAddress(DumpState* dump_out, magma::RegisterIo* register_io);
    void FormatDump(DumpState& dump_state, std::string& dump_string);

    int DeviceThreadLoop();
    void FrequencyMonitorDeviceThreadLoop();
    static void InterruptCallback(void* data, uint32_t master_interrupt_control);

    void QuerySliceInfo(uint32_t* subslice_total_out, uint32_t* eu_total_out);

    std::shared_ptr<GlobalContext> global_context() { return global_context_; }

    RenderEngineCommandStreamer* render_engine_cs() { return render_engine_cs_.get(); }

    std::shared_ptr<AddressSpace> gtt() { return gtt_; }

    std::shared_ptr<magma::PlatformBuffer> scratch_buffer() { return scratch_buffer_; }

    static const uint32_t kMagic = 0x64657669; //"devi"

    uint32_t device_id_{};
    uint32_t revision_{};
    uint32_t subslice_total_{};
    uint32_t eu_total_{};

    std::thread device_thread_;
    std::thread freq_monitor_device_thread_;
    std::unique_ptr<magma::PlatformThreadId> device_thread_id_;
    std::atomic_bool device_thread_quit_flag_{false};
    std::unique_ptr<GpuProgress> progress_;

    std::unique_ptr<MsdIntelPciDevice> platform_device_;
    std::unique_ptr<magma::RegisterIo> register_io_;
    std::shared_ptr<Gtt> gtt_;
    std::unique_ptr<RenderEngineCommandStreamer> render_engine_cs_;
    std::shared_ptr<GlobalContext> global_context_;
    std::unique_ptr<Sequencer> sequencer_;
    std::shared_ptr<magma::PlatformBuffer> scratch_buffer_;
    std::unique_ptr<InterruptManager> interrupt_manager_;
    std::unique_ptr<magma::PlatformBusMapper> bus_mapper_;

    class CommandBufferRequest;
    class DestroyContextRequest;
    class ReleaseBufferRequest;
    class InterruptRequest;
    class DumpRequest;

    // Thread-shared data members
    std::unique_ptr<magma::PlatformSemaphore> device_request_semaphore_;
    std::mutex device_request_mutex_;
    std::list<std::unique_ptr<DeviceRequest>> device_request_list_;

    struct FreqMonitorContext {
        std::unique_ptr<magma::PlatformSemaphore> semaphore{magma::PlatformSemaphore::Create()};
        std::atomic_bool tracing_enabled{false};
    };
    std::shared_ptr<FreqMonitorContext> freq_monitor_context_;

    friend class TestMsdIntelDevice;
    friend class TestCommandBuffer;
};

#endif // MSD_INTEL_DEVICE_H
