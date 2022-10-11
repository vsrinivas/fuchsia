// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MSD_INTEL_DEVICE_H
#define MSD_INTEL_DEVICE_H

#include <list>
#include <mutex>
#include <thread>
#include <vector>

#include "device_request.h"
#include "gtt.h"
#include "interrupt_manager.h"
#include "magma_util/macros.h"
#include "magma_util/register_io.h"
#include "magma_util/thread.h"
#include "msd.h"
#include "msd_intel_connection.h"
#include "msd_intel_context.h"
#include "msd_intel_pci_device.h"
#include "platform_semaphore.h"
#include "platform_trace.h"
#include "render_command_streamer.h"
#include "sequencer.h"
#include "video_command_streamer.h"

struct magma_intel_gen_topology;

class MsdIntelDevice : public msd_device_t,
                       public EngineCommandStreamer::Owner,
                       public Gtt::Owner,
                       public InterruptManager::Owner,
                       public MsdIntelConnection::Owner {
 public:
  using DeviceRequest = DeviceRequest<MsdIntelDevice>;

  class BatchRequest;
  class DestroyContextRequest;
  class ReleaseBufferRequest;
  class InterruptRequest;
  class DumpRequest;
  class TimestampRequest;
  class Topology;

  // Creates a device for the given |device_handle| and returns ownership.
  // If |start_device_thread| is false, then StartDeviceThread should be called
  // to enable device request processing.
  static std::unique_ptr<MsdIntelDevice> Create(void* device_handle, bool start_device_thread);

  virtual ~MsdIntelDevice();

  // This takes ownership of the connection so that ownership can be
  // transferred across the MSD ABI by the caller
  std::unique_ptr<MsdIntelConnection> Open(msd_client_id_t client_id);

  uint32_t device_id() override { return device_id_; }  // provided for EngineCommandStreamer
  uint32_t revision() { return revision_; }
  uint32_t subslice_total() { return subslice_total_; }
  uint32_t eu_total() { return eu_total_; }
  std::pair<magma_intel_gen_topology*, uint8_t*> GetTopology();
  bool engines_have_context_isolation() { return engines_have_context_isolation_; }

  static MsdIntelDevice* cast(msd_device_t* dev) {
    DASSERT(dev);
    DASSERT(dev->magic_ == kMagic);
    return static_cast<MsdIntelDevice*>(dev);
  }

  // Initialize the device using the given platform |device_handle|.
  bool Init(void* device_handle);

  struct DumpState {
    struct RenderCommandStreamer {
      uint32_t sequence_number;
      uint64_t active_head_pointer;
      std::vector<MappedBatch*> inflight_batches;
    } render_cs;
    struct VideoCommandStreamer {
      uint32_t sequence_number;
      uint64_t active_head_pointer;
    } video_cs;

    bool fault_present;
    uint8_t fault_engine;
    uint8_t fault_src;
    uint8_t fault_type;
    uint64_t fault_gpu_address;
    bool global;
  };

  void Dump(DumpState* dump_state);
  void DumpToString(std::vector<std::string>& dump_out);
  void DumpStatusToLog();

  // Sends a timestamp request to the device thread and waits for completion.
  magma::Status QueryTimestamp(std::unique_ptr<magma::PlatformBuffer> buffer);

 private:
  MsdIntelDevice();

#define CHECK_THREAD_IS_CURRENT(x) \
  if (x)                           \
  DASSERT(magma::ThreadIdCheck::IsCurrent(*x))

#define CHECK_THREAD_NOT_CURRENT(x) \
  if (x)                            \
  DASSERT(!magma::ThreadIdCheck::IsCurrent(*x))

  // EngineCommandStreamer::Owner
  magma::RegisterIo* register_io() override {
    CHECK_THREAD_IS_CURRENT(device_thread_id_);
    DASSERT(register_io_);
    return register_io_.get();
  }

  // InterruptManager::Owner
  magma::RegisterIo* register_io_for_interrupt() override {
    DASSERT(register_io_);
    return register_io_.get();
  }
  magma::PlatformPciDevice* platform_device() override {
    DASSERT(platform_device_);
    return platform_device_.get();
  }

  // EngineCommandStreamer::Owner
  Sequencer* sequencer() override {
    CHECK_THREAD_IS_CURRENT(device_thread_id_);
    DASSERT(sequencer_);
    return sequencer_.get();
  }

  std::vector<EngineCommandStreamer*> engine_command_streamers() {
    std::vector<EngineCommandStreamer*> engines{render_engine_cs()};

    if (video_command_streamer()) {
      engines.push_back(video_command_streamer());
    }

    return engines;
  }

  // MsdIntelConnection::Owner
  void SubmitBatch(std::unique_ptr<MappedBatch> batch) override;
  void DestroyContext(std::shared_ptr<MsdIntelContext> client_context) override;
  magma::PlatformBusMapper* GetBusMapper() override { return bus_mapper_.get(); }

  bool StartDeviceThread();

  void Destroy();

  bool BaseInit(void* device_handle);
  bool HardwarePreinit();
  bool CreateEngineCommandStreamers();
  void CheckEngines();
  void InitEngine(EngineCommandStreamer* engine);
  void EnableInterrupts(EngineCommandStreamer* engine, bool enable);
  bool RenderInitBatch();
  bool EngineReset(EngineCommandStreamer* engine);

  bool InitContextForEngine(MsdIntelContext* context, EngineCommandStreamer* command_streamer);

  void ProcessCompletedCommandBuffers(EngineCommandStreamerId id);
  void HangCheckTimeout(uint64_t timeout_ms, EngineCommandStreamerId id);

  magma::Status ProcessBatch(std::unique_ptr<MappedBatch> batch);
  magma::Status ProcessDestroyContext(std::shared_ptr<MsdIntelContext> client_context);
  magma::Status ProcessReleaseBuffer(std::shared_ptr<AddressSpace> address_space,
                                     std::shared_ptr<MsdIntelBuffer> buffer);
  magma::Status ProcessInterrupts(uint64_t interrupt_time_ns, uint32_t render_interrupt_status,
                                  uint32_t video_interrupt_status);
  magma::Status ProcessDumpStatusToLog();
  magma::Status ProcessTimestampRequest(std::shared_ptr<magma::PlatformBuffer> buffer);

  void EnqueueDeviceRequest(std::unique_ptr<DeviceRequest> request, bool enqueue_front = false);

  std::chrono::milliseconds GetDeviceRequestTimeoutMs(
      const std::vector<EngineCommandStreamer*>& engines);
  void DeviceRequestTimedOut(const std::vector<EngineCommandStreamer*>& engines);

  bool WaitIdleForTest(uint32_t timeout_ms = 100);

  uint32_t GetCurrentFrequency();
  void RequestMaxFreq();
  // May update the last_freq_poll_time
  void TraceFreq(std::chrono::steady_clock::time_point& last_freq_poll_time);

  static void DumpFault(DumpState* dump_out, uint32_t fault);
  static void DumpFaultAddress(DumpState* dump_out, magma::RegisterIo* register_io);
  void FormatDump(DumpState& dump_state, std::vector<std::string>& dump_out);

  int DeviceThreadLoop();
  static void InterruptCallback(void* data, uint32_t master_interrupt_control, uint64_t timestamp);

  void QuerySliceInfoGen9(uint32_t* subslice_total_out, uint32_t* eu_total_out,
                          Topology* topology_out);
  void QuerySliceInfoGen12(uint32_t* subslice_total_out, uint32_t* eu_total_out,
                           Topology* topology_out);

  std::shared_ptr<MsdIntelContext> global_context() { return global_context_; }

  RenderEngineCommandStreamer* render_engine_cs() { return render_engine_cs_.get(); }

  VideoCommandStreamer* video_command_streamer() { return video_command_streamer_.get(); }

  std::shared_ptr<AddressSpace> gtt() { return gtt_; }

  std::shared_ptr<magma::PlatformBuffer> scratch_buffer() { return scratch_buffer_; }

  static const uint32_t kMagic = 0x64657669;  //"devi"

  uint32_t device_id_{};
  uint32_t revision_{};
  uint32_t subslice_total_{};
  uint32_t eu_total_{};
  std::unique_ptr<Topology> topology_;
  bool engines_have_context_isolation_ = false;

  std::thread device_thread_;
  std::unique_ptr<magma::PlatformThreadId> device_thread_id_;
  std::atomic_bool device_thread_quit_flag_{false};
  std::unique_ptr<GpuProgress> progress_;
  std::atomic_uint64_t suspected_gpu_hang_count_{};
  std::atomic_uint64_t last_interrupt_callback_timestamp_{};
  std::atomic_uint64_t last_interrupt_timestamp_{};

  std::unique_ptr<MsdIntelPciDevice> platform_device_;
  std::unique_ptr<magma::RegisterIo> register_io_;
  std::shared_ptr<Gtt> gtt_;
  std::unique_ptr<RenderEngineCommandStreamer> render_engine_cs_;
  std::unique_ptr<VideoCommandStreamer> video_command_streamer_;
  std::shared_ptr<MsdIntelContext> global_context_;
  std::shared_ptr<IndirectContextBatch> indirect_context_batch_;
  std::unique_ptr<Sequencer> sequencer_;
  std::shared_ptr<magma::PlatformBuffer> scratch_buffer_;
  std::unique_ptr<InterruptManager> interrupt_manager_;
  std::unique_ptr<magma::PlatformBusMapper> bus_mapper_;

  // Thread-shared data members
  std::unique_ptr<magma::PlatformSemaphore> device_request_semaphore_;
  std::mutex device_request_mutex_;
  std::list<std::unique_ptr<DeviceRequest>> device_request_list_;

  friend class TestMsdIntelDevice;
  friend class TestHwCommandBuffer;
  friend class TestExec;
};

#endif  // MSD_INTEL_DEVICE_H
