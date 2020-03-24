// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "zircon_platform_connection.h"

#include <lib/fidl-async/cpp/bind.h>

namespace magma {

bool ZirconPlatformConnection::Bind(zx::channel server_endpoint) {
  fidl::OnChannelClosedFn<llcpp::fuchsia::gpu::magma::Primary::Interface> channel_closed_callback =
      [](llcpp::fuchsia::gpu::magma::Primary::Interface* interface) {
        static_cast<ZirconPlatformConnection*>(interface)->async_loop()->Quit();
      };

  llcpp::fuchsia::gpu::magma::Primary::Interface* interface = this;
  zx_status_t status = fidl::Bind(async_loop()->dispatcher(), std::move(server_endpoint), interface,
                                  std::move(channel_closed_callback));

  if (status != ZX_OK)
    return DRETF(false, "fidl::Bind failed: %d", status);

  return true;
}

bool ZirconPlatformConnection::HandleRequest() {
  zx_status_t status = async_loop_.Run(zx::time::infinite(), true /* once */);
  if (status != ZX_OK)
    return false;
  return true;
}

bool ZirconPlatformConnection::BeginShutdownWait() {
  zx_status_t status = async_begin_wait(async_loop()->dispatcher(), &async_wait_shutdown_);
  if (status != ZX_OK)
    return DRETF(false, "Couldn't begin wait on shutdown: %s", zx_status_get_string(status));
  return true;
}

void ZirconPlatformConnection::AsyncWaitHandler(async_dispatcher_t* dispatcher, AsyncWait* wait,
                                                zx_status_t status,
                                                const zx_packet_signal_t* signal) {
  if (status != ZX_OK)
    return;

  bool quit = false;
  if (wait == &async_wait_shutdown_) {
    DASSERT(signal->observed == ZX_EVENT_SIGNALED);
    quit = true;
    DLOG("got shutdown event");
  } else {
    DASSERT(false);
  }

  if (quit) {
    async_loop()->Quit();
  }
}

bool ZirconPlatformConnection::AsyncTaskHandler(async_dispatcher_t* dispatcher, AsyncTask* task,
                                                zx_status_t status) {
  switch (static_cast<MSD_CONNECTION_NOTIFICATION_TYPE>(task->notification.type)) {
    case MSD_CONNECTION_NOTIFICATION_CHANNEL_SEND: {
      zx_status_t status = zx_channel_write(server_notification_endpoint_.get(), 0,
                                            task->notification.u.channel_send.data,
                                            task->notification.u.channel_send.size, nullptr, 0);
      if (status != ZX_OK)
        return DRETF(false, "Failed writing to channel: %s", zx_status_get_string(status));
      return true;
    }
    case MSD_CONNECTION_NOTIFICATION_CONTEXT_KILLED:
      // Kill the connection.
      ShutdownEvent()->Signal();
      return true;
  }
  return DRETF(false, "Unhandled notification type: %d", task->notification.type);
}

void ZirconPlatformConnection::ImportBuffer(::zx::vmo buffer,
                                            ImportBufferCompleter::Sync _completer) {
  DLOG("ZirconPlatformConnection - ImportBuffer");
  uint64_t buffer_id;
  if (!delegate_->ImportBuffer(buffer.release(), &buffer_id)) {
    SetError(MAGMA_STATUS_INVALID_ARGS);
  }
}

void ZirconPlatformConnection::ReleaseBuffer(uint64_t buffer_id,
                                             ReleaseBufferCompleter::Sync _completer) {
  DLOG("ZirconPlatformConnection: ReleaseBuffer");
  if (!delegate_->ReleaseBuffer(buffer_id))
    SetError(MAGMA_STATUS_INVALID_ARGS);
}

void ZirconPlatformConnection::ImportObject(zx::handle handle, uint32_t object_type,
                                            ImportObjectCompleter::Sync _completer) {
  DLOG("ZirconPlatformConnection: ImportObject");
  if (!delegate_->ImportObject(handle.release(), static_cast<PlatformObject::Type>(object_type)))
    SetError(MAGMA_STATUS_INVALID_ARGS);
}

void ZirconPlatformConnection::ReleaseObject(uint64_t object_id, uint32_t object_type,
                                             ReleaseObjectCompleter::Sync _completer) {
  DLOG("ZirconPlatformConnection: ReleaseObject");
  if (!delegate_->ReleaseObject(object_id, static_cast<PlatformObject::Type>(object_type)))
    SetError(MAGMA_STATUS_INVALID_ARGS);
}

void ZirconPlatformConnection::CreateContext(uint32_t context_id,
                                             CreateContextCompleter::Sync _completer) {
  DLOG("ZirconPlatformConnection: CreateContext");
  if (!delegate_->CreateContext(context_id))
    SetError(MAGMA_STATUS_INTERNAL_ERROR);
}

void ZirconPlatformConnection::DestroyContext(uint32_t context_id,
                                              DestroyContextCompleter::Sync _completer) {
  DLOG("ZirconPlatformConnection: DestroyContext");
  if (!delegate_->DestroyContext(context_id))
    SetError(MAGMA_STATUS_INTERNAL_ERROR);
}

void ZirconPlatformConnection::ExecuteCommandBufferWithResources(
    uint32_t context_id, llcpp::fuchsia::gpu::magma::CommandBuffer fidl_command_buffer,
    ::fidl::VectorView<llcpp::fuchsia::gpu::magma::Resource> fidl_resources,
    ::fidl::VectorView<uint64_t> wait_semaphores, ::fidl::VectorView<uint64_t> signal_semaphores,
    ExecuteCommandBufferWithResourcesCompleter::Sync _completer) {
  auto command_buffer = std::make_unique<magma_system_command_buffer>();
  *command_buffer = {
      .batch_buffer_resource_index = fidl_command_buffer.batch_buffer_resource_index,
      .batch_start_offset = fidl_command_buffer.batch_start_offset,
      .num_resources = static_cast<uint32_t>(fidl_resources.count()),
      .wait_semaphore_count = static_cast<uint32_t>(wait_semaphores.count()),
      .signal_semaphore_count = static_cast<uint32_t>(signal_semaphores.count()),
  };

  std::vector<magma_system_exec_resource> resources;
  resources.reserve(fidl_resources.count());

  for (auto& resource : fidl_resources) {
    resources.push_back({
        resource.buffer,
        resource.offset,
        resource.length,
    });
  }

  // Merge semaphores into one vector
  std::vector<uint64_t> semaphores;
  semaphores.reserve(wait_semaphores.count() + signal_semaphores.count());

  for (uint64_t semaphore_id : wait_semaphores) {
    semaphores.push_back(semaphore_id);
  }
  for (uint64_t semaphore_id : signal_semaphores) {
    semaphores.push_back(semaphore_id);
  }

  magma::Status status = delegate_->ExecuteCommandBufferWithResources(
      context_id, std::move(command_buffer), std::move(resources), std::move(semaphores));

  if (!status)
    SetError(status.get());
}

void ZirconPlatformConnection::ExecuteImmediateCommands(
    uint32_t context_id, ::fidl::VectorView<uint8_t> command_data_vec,
    ::fidl::VectorView<uint64_t> semaphore_vec,
    ExecuteImmediateCommandsCompleter::Sync _completer) {
  DLOG("ZirconPlatformConnection: ExecuteImmediateCommands");
  magma::Status status = delegate_->ExecuteImmediateCommands(
      context_id, command_data_vec.count(), command_data_vec.mutable_data(), semaphore_vec.count(),
      semaphore_vec.mutable_data());
  if (!status)
    SetError(status.get());
}

void ZirconPlatformConnection::GetError(GetErrorCompleter::Sync _completer) {
  DLOG("ZirconPlatformConnection: GetError");
  magma_status_t result = error_;
  error_ = 0;
  _completer.Reply(result);
}

void ZirconPlatformConnection::MapBufferGpu(uint64_t buffer_id, uint64_t gpu_va,
                                            uint64_t page_offset, uint64_t page_count,
                                            uint64_t flags,
                                            MapBufferGpuCompleter::Sync _completer) {
  DLOG("ZirconPlatformConnection: MapBufferGpuFIDL");
  if (!delegate_->MapBufferGpu(buffer_id, gpu_va, page_offset, page_count, flags))
    SetError(MAGMA_STATUS_INVALID_ARGS);
}

void ZirconPlatformConnection::UnmapBufferGpu(uint64_t buffer_id, uint64_t gpu_va,
                                              UnmapBufferGpuCompleter::Sync _completer) {
  DLOG("ZirconPlatformConnection: UnmapBufferGpuFIDL");
  if (!delegate_->UnmapBufferGpu(buffer_id, gpu_va))
    SetError(MAGMA_STATUS_INVALID_ARGS);
}

void ZirconPlatformConnection::CommitBuffer(uint64_t buffer_id, uint64_t page_offset,
                                            uint64_t page_count,
                                            CommitBufferCompleter::Sync _completer) {
  DLOG("ZirconPlatformConnection: CommitBufferFIDL");
  if (!delegate_->CommitBuffer(buffer_id, page_offset, page_count))
    SetError(MAGMA_STATUS_INVALID_ARGS);
}

std::shared_ptr<PlatformConnection> PlatformConnection::Create(
    std::unique_ptr<PlatformConnection::Delegate> delegate, msd_client_id_t client_id,
    std::unique_ptr<magma::PlatformHandle> thread_profile) {
  if (!delegate)
    return DRETP(nullptr, "attempting to create PlatformConnection with null delegate");

  zx::channel server_endpoint;
  zx::channel client_endpoint;
  zx_status_t status = zx::channel::create(0, &server_endpoint, &client_endpoint);
  if (status != ZX_OK)
    return DRETP(nullptr, "zx::channel::create failed");

  zx::channel server_notification_endpoint;
  zx::channel client_notification_endpoint;
  status = zx::channel::create(0, &server_notification_endpoint, &client_notification_endpoint);
  if (status != ZX_OK)
    return DRETP(nullptr, "zx::channel::create failed");

  auto shutdown_event = magma::PlatformEvent::Create();
  if (!shutdown_event)
    return DRETP(nullptr, "Failed to create shutdown event");

  auto connection = std::make_shared<ZirconPlatformConnection>(
      std::move(delegate), client_id, std::move(client_endpoint),
      std::move(server_notification_endpoint), std::move(client_notification_endpoint),
      std::shared_ptr<magma::PlatformEvent>(std::move(shutdown_event)), std::move(thread_profile));

  if (!connection->Bind(std::move(server_endpoint)))
    return DRETP(nullptr, "fidl::Bind failed: %d", status);

  if (!connection->BeginShutdownWait())
    return DRETP(nullptr, "Failed to begin shutdown wait");

  return connection;
}

}  // namespace magma
