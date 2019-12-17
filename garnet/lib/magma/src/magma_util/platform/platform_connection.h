// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MAGMA_SRC_MAGMA_UTIL_PLATFORM_PLATFORM_CONNECTION_H_
#define GARNET_LIB_MAGMA_SRC_MAGMA_UTIL_PLATFORM_PLATFORM_CONNECTION_H_

#include <memory>
#include <vector>

#include "magma.h"
#include "magma_util/macros.h"
#include "magma_util/status.h"
#include "msd_defs.h"
#include "platform_buffer.h"
#include "platform_event.h"
#include "platform_object.h"
#include "platform_semaphore.h"
#include "platform_thread.h"

namespace magma {

class PlatformConnection {
 public:
  class Delegate {
   public:
    virtual ~Delegate() {}
    virtual bool ImportBuffer(uint32_t handle, uint64_t* buffer_id_out) = 0;
    virtual bool ReleaseBuffer(uint64_t buffer_id) = 0;

    virtual bool ImportObject(uint32_t handle, PlatformObject::Type object_type) = 0;
    virtual bool ReleaseObject(uint64_t object_id, PlatformObject::Type object_type) = 0;

    virtual bool CreateContext(uint32_t context_id) = 0;
    virtual bool DestroyContext(uint32_t context_id) = 0;

    virtual magma::Status ExecuteCommandBufferWithResources(
        uint32_t context_id, std::unique_ptr<magma_system_command_buffer> command_buffer,
        std::vector<magma_system_exec_resource> resources, std::vector<uint64_t> semaphores) = 0;
    virtual bool MapBufferGpu(uint64_t buffer_id, uint64_t gpu_va, uint64_t page_offset,
                              uint64_t page_count, uint64_t flags) = 0;
    virtual bool UnmapBufferGpu(uint64_t buffer_id, uint64_t gpu_va) = 0;
    virtual bool CommitBuffer(uint64_t buffer_id, uint64_t page_offset, uint64_t page_count) = 0;
    virtual void SetNotificationCallback(msd_connection_notification_callback_t callback,
                                         void* token) = 0;
    virtual magma::Status ExecuteImmediateCommands(uint32_t context_id, uint64_t commands_size,
                                                   void* commands, uint64_t semaphore_count,
                                                   uint64_t* semaphore_ids) = 0;
  };

  PlatformConnection(std::shared_ptr<magma::PlatformEvent> shutdown_event,
                     msd_client_id_t client_id)
      : client_id_(client_id), shutdown_event_(std::move(shutdown_event)) {}

  virtual ~PlatformConnection() {}

  static std::shared_ptr<PlatformConnection> Create(std::unique_ptr<Delegate> Delegate,
                                                    msd_client_id_t client_id);
  virtual uint32_t GetClientEndpoint() = 0;
  // This handle is used to asynchronously return information to the client.
  virtual uint32_t GetClientNotificationEndpoint() = 0;

  // handles a single request, returns false if anything has put it into an illegal state
  // or if the remote has closed
  virtual bool HandleRequest() = 0;

  std::shared_ptr<magma::PlatformEvent> ShutdownEvent() { return shutdown_event_; }

  static void RunLoop(std::shared_ptr<magma::PlatformConnection> connection) {
    magma::PlatformThreadHelper::SetCurrentThreadName("ConnectionThread " +
                                                      std::to_string(connection->client_id_));
    while (connection->HandleRequest())
      ;
    // the runloop terminates when the remote closes, or an error is experienced
    // so this is the appropriate time to let the connection go out of scope and be destroyed
  }

 private:
  msd_client_id_t client_id_;
  std::shared_ptr<magma::PlatformEvent> shutdown_event_;
};

}  // namespace magma

#endif  // GARNET_LIB_MAGMA_SRC_MAGMA_UTIL_PLATFORM_PLATFORM_CONNECTION_H_
