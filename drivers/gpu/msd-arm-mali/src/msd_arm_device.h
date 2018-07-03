// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MSD_ARM_DEVICE_H
#define MSD_ARM_DEVICE_H

#include <deque>
#include <list>
#include <mutex>
#include <thread>
#include <vector>

#include "address_manager.h"
#include "device_request.h"
#include "gpu_features.h"
#include "job_scheduler.h"
#include "lib/fxl/synchronization/thread_annotations.h"
#include "magma_util/macros.h"
#include "magma_util/register_io.h"
#include "magma_util/thread.h"
#include "msd.h"
#include "msd_arm_connection.h"
#include "performance_counters.h"
#include "platform_device.h"
#include "platform_interrupt.h"
#include "platform_semaphore.h"
#include "power_manager.h"

class MsdArmDevice : public msd_device_t,
                     public JobScheduler::Owner,
                     public MsdArmConnection::Owner,
                     public AddressManager::Owner,
                     public PerformanceCounters::Owner {
public:
    // Creates a device for the given |device_handle| and returns ownership.
    // If |start_device_thread| is false, then StartDeviceThread should be called
    // to enable device request processing.
    static std::unique_ptr<MsdArmDevice> Create(void* device_handle, bool start_device_thread);

    MsdArmDevice();

    virtual ~MsdArmDevice();

    static MsdArmDevice* cast(msd_device_t* dev)
    {
        DASSERT(dev);
        DASSERT(dev->magic_ == kMagic);
        return static_cast<MsdArmDevice*>(dev);
    }

    bool Init(void* device_handle);
    std::shared_ptr<MsdArmConnection> Open(msd_client_id_t client_id);

    struct DumpState {
        struct CorePowerState {
            const char* core_type;
            const char* status_type;
            uint64_t bitmask;
        };
        std::vector<CorePowerState> power_states;
        // Only accounts for recent past.
        uint64_t total_time_ms;
        uint64_t active_time_ms;

        uint32_t gpu_fault_status;
        uint64_t gpu_fault_address;
        uint32_t gpu_status;
        uint64_t cycle_count;
        uint64_t timestamp;

        struct JobSlotStatus {
            uint32_t status;
            uint64_t head;
            uint64_t tail;
            uint32_t config;
        };

        std::vector<JobSlotStatus> job_slot_status;
        struct AddressSpaceStatus {
            uint32_t status;
            uint32_t fault_status;
            uint64_t fault_address;
        };
        std::vector<AddressSpaceStatus> address_space_status;
    };
    static void DumpRegisters(const GpuFeatures& features, magma::RegisterIo* io,
                              DumpState* dump_state);
    void Dump(DumpState* dump_state);
    void DumpToString(std::string& dump_string);
    void FormatDump(DumpState& dump_state, std::string& dump_string);
    void DumpStatusToLog();

    // MsdArmConnection::Owner implementation.
    void ScheduleAtom(std::shared_ptr<MsdArmAtom> atom) override;
    void CancelAtoms(std::shared_ptr<MsdArmConnection> connection) override;
    AddressSpaceObserver* GetAddressSpaceObserver() override { return address_manager_.get(); }
    ArmMaliCacheCoherencyStatus cache_coherency_status() override
    {
        return cache_coherency_status_;
    }
    magma::PlatformBusMapper* GetBusMapper() override { return bus_mapper_.get(); }

    magma_status_t QueryInfo(uint64_t id, uint64_t* value_out);

    void RequestPerfCounterOperation(uint32_t type);

    // PerformanceCounters::Owner implementation.
    AddressManager* address_manager() override { return address_manager_.get(); }
    MsdArmConnection::Owner* connection_owner() override { return this; }

private:
#define CHECK_THREAD_IS_CURRENT(x)                                                                 \
    if (x)                                                                                         \
    DASSERT(magma::ThreadIdCheck::IsCurrent(*x))

#define CHECK_THREAD_NOT_CURRENT(x)                                                                \
    if (x)                                                                                         \
    DASSERT(!magma::ThreadIdCheck::IsCurrent(*x))

    friend class TestMsdArmDevice;

    class DumpRequest;
    class GpuInterruptRequest;
    class JobInterruptRequest;
    class MmuInterruptRequest;
    class ScheduleAtomRequest;
    class CancelAtomsRequest;
    class PerfCounterRequest;

    magma::RegisterIo* register_io() override
    {
        DASSERT(register_io_);
        return register_io_.get();
    }

    void set_register_io(std::unique_ptr<magma::RegisterIo> register_io)
    {
        register_io_ = std::move(register_io);
    }

    void Destroy();
    void StartDeviceThread();
    int DeviceThreadLoop();
    int GpuInterruptThreadLoop();
    int JobInterruptThreadLoop();
    int MmuInterruptThreadLoop();
    bool InitializeInterrupts();
    void EnableInterrupts();
    void DisableInterrupts();
    void EnqueueDeviceRequest(std::unique_ptr<DeviceRequest> request, bool enqueue_front = false);
    void SuspectedGpuHang();
    magma::Status ProcessDumpStatusToLog();
    magma::Status ProcessGpuInterrupt();
    magma::Status ProcessJobInterrupt();
    magma::Status ProcessMmuInterrupt();
    magma::Status ProcessScheduleAtoms();
    magma::Status ProcessCancelAtoms(std::weak_ptr<MsdArmConnection> connection);
    magma::Status ProcessPerfCounterRequest(uint32_t type);

    void ExecuteAtomOnDevice(MsdArmAtom* atom, magma::RegisterIo* registers);

    // JobScheduler::Owner implementation.
    void RunAtom(MsdArmAtom* atom) override;
    void AtomCompleted(MsdArmAtom* atom, ArmMaliResultCode result) override;
    void HardStopAtom(MsdArmAtom* atom) override;
    void SoftStopAtom(MsdArmAtom* atom) override;
    void ReleaseMappingsForAtom(MsdArmAtom* atom) override;
    magma::PlatformPort* GetPlatformPort() override;
    void UpdateGpuActive(bool active) override;

    static const uint32_t kMagic = 0x64657669; //"devi"

    std::thread device_thread_;
    std::unique_ptr<magma::PlatformThreadId> device_thread_id_;
    std::atomic_bool device_thread_quit_flag_{false};

    std::atomic_bool interrupt_thread_quit_flag_{false};
    std::thread gpu_interrupt_thread_;
    std::thread job_interrupt_thread_;
    std::thread mmu_interrupt_thread_;

    std::unique_ptr<magma::PlatformSemaphore> device_request_semaphore_;
    std::unique_ptr<magma::PlatformPort> device_port_;
    std::mutex device_request_mutex_;
    std::list<std::unique_ptr<DeviceRequest>> device_request_list_;

    std::mutex schedule_mutex_;
    FXL_GUARDED_BY(schedule_mutex_) std::vector<std::shared_ptr<MsdArmAtom>> atoms_to_schedule_;

    std::unique_ptr<magma::PlatformDevice> platform_device_;
    std::unique_ptr<magma::RegisterIo> register_io_;
    std::unique_ptr<magma::PlatformInterrupt> gpu_interrupt_;
    std::unique_ptr<magma::PlatformInterrupt> job_interrupt_;
    std::unique_ptr<magma::PlatformInterrupt> mmu_interrupt_;

    GpuFeatures gpu_features_;
    ArmMaliCacheCoherencyStatus cache_coherency_status_ = kArmMaliCacheCoherencyNone;

    std::unique_ptr<PowerManager> power_manager_;
    std::unique_ptr<AddressManager> address_manager_;
    std::unique_ptr<JobScheduler> scheduler_;
    std::unique_ptr<magma::PlatformBusMapper> bus_mapper_;
    uint64_t cycle_counter_refcount_ = 0;

    std::unique_ptr<PerformanceCounters> perf_counters_;
};

#endif // MSD_ARM_DEVICE_H
