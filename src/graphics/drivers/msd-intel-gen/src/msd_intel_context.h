// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MSD_INTEL_CONTEXT_H
#define MSD_INTEL_CONTEXT_H

#include <map>
#include <memory>
#include <queue>
#include <thread>

#include "command_buffer.h"
#include "magma_util/semaphore_port.h"
#include "magma_util/status.h"
#include "msd.h"
#include "msd_intel_buffer.h"
#include "platform_logger.h"
#include "ppgtt.h"
#include "ringbuffer.h"
#include "types.h"

class MsdIntelConnection;

// Base context, not tied to a connection.
class MsdIntelContext {
 public:
  explicit MsdIntelContext(std::shared_ptr<AddressSpace> address_space)
      : address_space_(std::move(address_space)) {
    DASSERT(address_space_);
  }

  MsdIntelContext(std::shared_ptr<AddressSpace> address_space,
                  std::weak_ptr<MsdIntelConnection> connection)
      : address_space_(std::move(address_space)), connection_(std::move(connection)) {}

  ~MsdIntelContext();

  // The context has a single target command streamer so that mapping release batches and pipeline
  // fence batches are processed by the appropriate command streamer.
  void SetTargetCommandStreamer(EngineCommandStreamerId id) {
    DASSERT(!target_command_streamer_);
    target_command_streamer_ = id;
  }

  std::optional<EngineCommandStreamerId> GetTargetCommandStreamer() {
    return target_command_streamer_;
  }

  void SetEngineState(EngineCommandStreamerId id, std::unique_ptr<MsdIntelBuffer> context_buffer,
                      std::unique_ptr<Ringbuffer> ringbuffer);

  bool Map(std::shared_ptr<AddressSpace> address_space, EngineCommandStreamerId id);
  bool Unmap(EngineCommandStreamerId id);

  std::weak_ptr<MsdIntelConnection> connection() { return connection_; }

  bool killed() const { return killed_; }

  void Kill();

  size_t GetQueueSize() {
    std::lock_guard lock(presubmit_mutex_);
    return presubmit_queue_.size();
  }

  // Gets the gpu address of the context buffer if mapped.
  bool GetGpuAddress(EngineCommandStreamerId id, gpu_addr_t* addr_out);
  bool GetRingbufferGpuAddress(EngineCommandStreamerId id, gpu_addr_t* addr_out);

  MsdIntelBuffer* get_context_buffer(EngineCommandStreamerId id) {
    auto iter = state_map_.find(id);
    return iter == state_map_.end() ? nullptr : iter->second.context_buffer.get();
  }

  void* GetCachedContextBufferCpuAddr(EngineCommandStreamerId id) {
    auto iter = state_map_.find(id);
    if (iter == state_map_.end())
      return nullptr;
    if (!iter->second.context_buffer_cpu_addr) {
      MsdIntelBuffer* context_buffer = iter->second.context_buffer.get();
      if (!context_buffer)
        return nullptr;
      if (!context_buffer->platform_buffer()->MapCpu(&iter->second.context_buffer_cpu_addr))
        return DRETP(nullptr, "Failed to map context buffer");
    }
    return iter->second.context_buffer_cpu_addr;
  }

  Ringbuffer* get_ringbuffer(EngineCommandStreamerId id) {
    auto iter = state_map_.find(id);
    return iter == state_map_.end() ? nullptr : iter->second.ringbuffer.get();
  }

  bool IsInitializedForEngine(EngineCommandStreamerId id) {
    return state_map_.find(id) != state_map_.end();
  }

  std::queue<std::unique_ptr<MappedBatch>>& pending_batch_queue() { return pending_batch_queue_; }

  std::shared_ptr<AddressSpace> exec_address_space() { return address_space_; }

  magma::Status SubmitCommandBuffer(std::unique_ptr<CommandBuffer> cmd_buf);
  magma::Status SubmitBatch(std::unique_ptr<MappedBatch> batch);

  void Shutdown();

 private:
  magma::Status SubmitBatchLocked() MAGMA_REQUIRES(presubmit_mutex_);

  struct PerEngineState {
    std::shared_ptr<MsdIntelBuffer> context_buffer;
    std::unique_ptr<GpuMapping> context_mapping;
    std::unique_ptr<Ringbuffer> ringbuffer;
    gpu_addr_t ringbuffer_gpu_addr = 0;
    void* context_buffer_cpu_addr = nullptr;
  };

  std::optional<EngineCommandStreamerId> target_command_streamer_;
  std::map<EngineCommandStreamerId, PerEngineState> state_map_;
  std::queue<std::unique_ptr<MappedBatch>> pending_batch_queue_;
  std::shared_ptr<AddressSpace> address_space_;

  std::weak_ptr<MsdIntelConnection> connection_;
  std::unique_ptr<magma::SemaphorePort> semaphore_port_;
  std::thread wait_thread_;
  std::mutex presubmit_mutex_;
  std::queue<std::unique_ptr<MappedBatch>> presubmit_queue_ MAGMA_GUARDED(presubmit_mutex_);
  bool killed_ = false;

  friend class TestContext;
};

class MsdIntelAbiContext : public msd_context_t {
 public:
  explicit MsdIntelAbiContext(std::shared_ptr<MsdIntelContext> ptr) : ptr_(std::move(ptr)) {
    magic_ = kMagic;
  }

  static MsdIntelAbiContext* cast(msd_context_t* context) {
    DASSERT(context);
    DASSERT(context->magic_ == kMagic);
    return static_cast<MsdIntelAbiContext*>(context);
  }
  std::shared_ptr<MsdIntelContext> ptr() { return ptr_; }

 private:
  std::shared_ptr<MsdIntelContext> ptr_;
  static const uint32_t kMagic = 0x63747874;  // "ctxt"
};

#endif  // MSD_INTEL_CONTEXT_H
