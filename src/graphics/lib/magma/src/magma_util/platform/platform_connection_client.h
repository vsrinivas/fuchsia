// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_MAGMA_SRC_MAGMA_UTIL_PLATFORM_PLATFORM_CONNECTION_CLIENT_H_
#define SRC_GRAPHICS_LIB_MAGMA_SRC_MAGMA_UTIL_PLATFORM_PLATFORM_CONNECTION_CLIENT_H_

#include <memory>

#include "magma/magma.h"
#include "magma_util/macros.h"
#include "magma_util/status.h"
#include "platform_buffer.h"
#include "platform_object.h"
#include "platform_thread.h"

namespace magma {

class PlatformPerfCountPoolClient {
 public:
  virtual ~PlatformPerfCountPoolClient() = default;
  virtual uint64_t pool_id() = 0;
  virtual magma_handle_t handle() = 0;
  virtual magma::Status ReadPerformanceCounterCompletion(uint32_t* trigger_id_out,
                                                         uint64_t* buffer_id_out,
                                                         uint32_t* buffer_offset_out,
                                                         uint64_t* time_out,
                                                         uint32_t* result_flags_out) = 0;
};

// Any implementation of PlatformConnectionClient shall be threadsafe.
class PlatformConnectionClient {
 public:
  virtual ~PlatformConnectionClient() {}

  static std::unique_ptr<PlatformConnectionClient> Create(uint32_t device_handle,
                                                          uint32_t device_notification_handle,
                                                          uint64_t max_inflight_messages,
                                                          uint64_t max_inflight_bytes);
  // Imports an object for use in the system driver
  virtual magma_status_t ImportObject(uint32_t handle, PlatformObject::Type object_type,
                                      uint64_t object_id) = 0;

  // Releases the connection's reference to the given object.
  virtual magma_status_t ReleaseObject(uint64_t object_id, PlatformObject::Type object_type) = 0;

  // Creates a context and returns the context id
  virtual magma_status_t CreateContext(uint32_t* context_id_out) = 0;
  // Destroys a context for the given id
  virtual magma_status_t DestroyContext(uint32_t context_id) = 0;

  virtual magma_status_t GetError() = 0;
  virtual magma_status_t Flush() = 0;

  virtual magma_status_t MapBuffer(uint64_t buffer_id, uint64_t gpu_va, uint64_t offset,
                                   uint64_t length, uint64_t flags) = 0;

  virtual magma_status_t UnmapBuffer(uint64_t buffer_id, uint64_t gpu_va) = 0;

  virtual magma_status_t BufferRangeOp(uint64_t buffer_id, uint32_t options, uint64_t offset,
                                       uint64_t length) = 0;

  virtual uint32_t GetNotificationChannelHandle() = 0;
  virtual magma_status_t ReadNotificationChannel(void* buffer, size_t buffer_size,
                                                 size_t* buffer_size_out,
                                                 magma_bool_t* more_data_out) = 0;
  virtual magma_status_t ExecuteCommand(uint32_t context_id,
                                        magma_command_descriptor* descriptor) = 0;
  virtual magma_status_t ExecuteImmediateCommands(uint32_t context_id, uint64_t command_count,
                                                  magma_inline_command_buffer* command_buffers,
                                                  uint64_t* messages_sent_out) = 0;

  virtual magma_status_t EnablePerformanceCounterAccess(
      std::unique_ptr<magma::PlatformHandle> handle) = 0;
  virtual magma_status_t IsPerformanceCounterAccessAllowed(bool* enabled_out) = 0;
  virtual magma::Status EnablePerformanceCounters(uint64_t* counters, uint64_t counter_count) = 0;
  virtual magma::Status CreatePerformanceCounterBufferPool(
      std::unique_ptr<PlatformPerfCountPoolClient>* pool_out) = 0;
  virtual magma::Status ReleasePerformanceCounterBufferPool(uint64_t pool_id) = 0;
  virtual magma::Status AddPerformanceCounterBufferOffsetsToPool(uint64_t pool_id,
                                                                 const magma_buffer_offset* offsets,
                                                                 uint64_t offsets_count) = 0;
  virtual magma::Status RemovePerformanceCounterBufferFromPool(uint64_t pool_id,
                                                               uint64_t buffer_id) = 0;
  virtual magma::Status DumpPerformanceCounters(uint64_t pool_id, uint32_t trigger_id) = 0;
  virtual magma::Status ClearPerformanceCounters(uint64_t* counters, uint64_t counter_count) = 0;

  // Retrieve the performance counter access token from a channel to a gpu-performance-counters
  // device.
  static std::unique_ptr<magma::PlatformHandle> RetrieveAccessToken(magma::PlatformHandle* channel);

  static PlatformConnectionClient* cast(magma_connection_t connection) {
    DASSERT(connection);
    auto client = reinterpret_cast<PlatformConnectionClient*>(connection);
    DASSERT(client->magic_ == kMagic);
    return client;
  }

  // Returns: inflight messages, inflight memory
  virtual std::pair<uint64_t, uint64_t> GetFlowControlCounts() = 0;

 protected:
  PlatformConnectionClient() {}

 private:
  static const uint32_t kMagic = 0x636f6e6e;  // "conn" (Connection)

  uint32_t magic_ = kMagic;
};

}  // namespace magma

#endif  // SRC_GRAPHICS_LIB_MAGMA_SRC_MAGMA_UTIL_PLATFORM_PLATFORM_CONNECTION_CLIENT_H_
