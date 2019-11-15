// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_system_connection.h"

#include <vector>

#include "magma_system_device.h"
#include "magma_util/macros.h"

MagmaSystemConnection::MagmaSystemConnection(std::weak_ptr<MagmaSystemDevice> weak_device,
                                             msd_connection_unique_ptr_t msd_connection_t)
    : device_(weak_device), msd_connection_(std::move(msd_connection_t)) {
  DASSERT(msd_connection_);
}

MagmaSystemConnection::~MagmaSystemConnection() {
  for (auto iter = buffer_map_.begin(); iter != buffer_map_.end();) {
    msd_connection_release_buffer(msd_connection(), iter->second.buffer->msd_buf());
    iter = buffer_map_.erase(iter);
  }

  // Reset all MSD objects before calling ConnectionClosed() because the msd device might go away
  // any time after ConnectionClosed() and we don't want any dangling dependencies.
  semaphore_map_.clear();
  context_map_.clear();
  msd_connection_.reset();
  auto device = device_.lock();
  if (device) {
    device->ConnectionClosed(std::this_thread::get_id());
  }
}

uint32_t MagmaSystemConnection::GetDeviceId() {
  auto device = device_.lock();
  return device ? device->GetDeviceId() : 0;
}

bool MagmaSystemConnection::CreateContext(uint32_t context_id) {
  auto iter = context_map_.find(context_id);
  if (iter != context_map_.end())
    return DRETF(false, "Attempting to add context with duplicate id");

  auto msd_ctx = msd_connection_create_context(msd_connection());
  if (!msd_ctx)
    return DRETF(false, "Failed to create msd context");

  auto ctx = std::unique_ptr<MagmaSystemContext>(
      new MagmaSystemContext(this, msd_context_unique_ptr_t(msd_ctx, &msd_context_destroy)));

  context_map_.insert(std::make_pair(context_id, std::move(ctx)));
  return true;
}

bool MagmaSystemConnection::DestroyContext(uint32_t context_id) {
  auto iter = context_map_.find(context_id);
  if (iter == context_map_.end())
    return DRETF(false, "MagmaSystemConnection:Attempting to destroy invalid context id");
  context_map_.erase(iter);
  return true;
}

MagmaSystemContext* MagmaSystemConnection::LookupContext(uint32_t context_id) {
  auto iter = context_map_.find(context_id);
  if (iter == context_map_.end())
    return DRETP(nullptr, "MagmaSystemConnection: Attempting to lookup invalid context id");

  return iter->second.get();
}

magma::Status MagmaSystemConnection::ExecuteCommandBufferWithResources(
    uint32_t context_id, std::unique_ptr<magma_system_command_buffer> command_buffer,
    std::vector<magma_system_exec_resource> resources, std::vector<uint64_t> semaphores) {
  auto context = LookupContext(context_id);
  if (!context)
    return DRET_MSG(MAGMA_STATUS_INVALID_ARGS,
                    "Attempting to execute command buffer on invalid context");

  return context->ExecuteCommandBufferWithResources(std::move(command_buffer), std::move(resources),
                                                    std::move(semaphores));
}

magma::Status MagmaSystemConnection::ExecuteImmediateCommands(uint32_t context_id,
                                                              uint64_t commands_size,
                                                              void* commands,
                                                              uint64_t semaphore_count,
                                                              uint64_t* semaphore_ids) {
  auto context = LookupContext(context_id);
  if (!context)
    return DRET_MSG(MAGMA_STATUS_INVALID_ARGS,
                    "Attempting to execute command buffer on invalid context");

  return context->ExecuteImmediateCommands(commands_size, commands, semaphore_count, semaphore_ids);
}

bool MagmaSystemConnection::ImportBuffer(uint32_t handle, uint64_t* id_out) {
  auto buffer = magma::PlatformBuffer::Import(handle);
  if (!buffer)
    return DRETF(false, "failed to import buffer");

  uint64_t id = buffer->id();

  auto iter = buffer_map_.find(id);
  if (iter != buffer_map_.end()) {
    iter->second.refcount++;
    return true;
  }

  BufferReference ref;
  ref.buffer = MagmaSystemBuffer::Create(std::move(buffer));

  buffer_map_.insert({id, ref});
  *id_out = id;
  return true;
}

bool MagmaSystemConnection::ReleaseBuffer(uint64_t id) {
  auto iter = buffer_map_.find(id);
  if (iter == buffer_map_.end())
    return DRETF(false, "Attempting to free invalid buffer id");

  if (--iter->second.refcount > 0)
    return true;

  msd_connection_release_buffer(msd_connection(), iter->second.buffer->msd_buf());
  buffer_map_.erase(iter);

  return true;
}

bool MagmaSystemConnection::MapBufferGpu(uint64_t id, uint64_t gpu_va, uint64_t page_offset,
                                         uint64_t page_count, uint64_t flags) {
  auto iter = buffer_map_.find(id);
  if (iter == buffer_map_.end())
    return DRETF(false, "Attempting to gpu map invalid buffer id");
  if (msd_connection_map_buffer_gpu(msd_connection(), iter->second.buffer->msd_buf(), gpu_va,
                                    page_offset, page_count, flags) != MAGMA_STATUS_OK)
    return DRETF(false, "msd_connection_map_buffer_gpu failed");

  return true;
}

bool MagmaSystemConnection::UnmapBufferGpu(uint64_t id, uint64_t gpu_va) {
  auto iter = buffer_map_.find(id);
  if (iter == buffer_map_.end())
    return DRETF(false, "Attempting to gpu unmap invalid buffer id");
  if (msd_connection_unmap_buffer_gpu(msd_connection(), iter->second.buffer->msd_buf(), gpu_va) !=
      MAGMA_STATUS_OK)
    return DRETF(false, "msd_connection_unmap_buffer_gpu failed");

  return true;
}

bool MagmaSystemConnection::CommitBuffer(uint64_t id, uint64_t page_offset, uint64_t page_count) {
  auto iter = buffer_map_.find(id);
  if (iter == buffer_map_.end())
    return DRETF(false, "Attempting to commit invalid buffer id");
  if (page_count + page_offset < page_count) {
    return DRETF(false, "Offset overflows");
  }
  if (page_count + page_offset > iter->second.buffer->size() / PAGE_SIZE) {
    return DRETF(false, "Page offset too large for buffer");
  }
  if (msd_connection_commit_buffer(msd_connection(), iter->second.buffer->msd_buf(), page_offset,
                                   page_count) != MAGMA_STATUS_OK)
    return DRETF(false, "msd_connection_commit_buffer failed");

  return true;
}

void MagmaSystemConnection::SetNotificationCallback(msd_connection_notification_callback_t callback,
                                                    void* token) {
  msd_connection_set_notification_callback(msd_connection(), callback, token);
}

bool MagmaSystemConnection::ImportObject(uint32_t handle, magma::PlatformObject::Type object_type) {
  auto device = device_.lock();
  if (!device)
    return DRETF(false, "failed to lock device");

  switch (object_type) {
    case magma::PlatformObject::SEMAPHORE: {
      uint64_t id;
      if (!magma::PlatformObject::IdFromHandle(handle, &id))
        return DRETF(false, "failed to get semaphore id for handle");

      // Always import the handle to to ensure it gets closed
      auto platform_sem = magma::PlatformSemaphore::Import(handle);

      auto iter = semaphore_map_.find(id);
      if (iter != semaphore_map_.end()) {
        iter->second.refcount++;
        return true;
      }

      auto semaphore = MagmaSystemSemaphore::Create(std::move(platform_sem));
      if (!semaphore)
        return DRETF(false, "failed to import platform semaphore");

      SemaphoreReference ref;
      ref.semaphore = std::move(semaphore);
      semaphore_map_.insert(std::make_pair(id, ref));
    } break;
  }

  return true;
}

bool MagmaSystemConnection::ReleaseObject(uint64_t object_id,
                                          magma::PlatformObject::Type object_type) {
  switch (object_type) {
    case magma::PlatformObject::SEMAPHORE: {
      auto iter = semaphore_map_.find(object_id);
      if (iter == semaphore_map_.end())
        return DRETF(false, "Attempting to free invalid semaphore id 0x%" PRIx64, object_id);

      if (--iter->second.refcount > 0)
        return true;

      semaphore_map_.erase(iter);
    } break;
  }
  return true;
}

std::shared_ptr<MagmaSystemBuffer> MagmaSystemConnection::LookupBuffer(uint64_t id) {
  auto iter = buffer_map_.find(id);
  if (iter == buffer_map_.end())
    return DRETP(nullptr, "Attempting to lookup invalid buffer id");

  return iter->second.buffer;
}

std::shared_ptr<MagmaSystemSemaphore> MagmaSystemConnection::LookupSemaphore(uint64_t id) {
  auto iter = semaphore_map_.find(id);
  if (iter == semaphore_map_.end())
    return nullptr;
  return iter->second.semaphore;
}
