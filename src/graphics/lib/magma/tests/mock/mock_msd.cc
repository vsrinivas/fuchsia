// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mock_msd.h"

#include <vector>

#include "msd.h"
#include "platform_semaphore.h"

std::unique_ptr<MsdMockBufferManager> g_bufmgr;

struct msd_driver_t* msd_driver_create(void) {
  return new MsdMockDriver();
}

void msd_driver_configure(struct msd_driver_t* drv, uint32_t flags) {}

void msd_driver_destroy(msd_driver_t* drv) { delete MsdMockDriver::cast(drv); }

msd_device_t* msd_driver_create_device(msd_driver_t* drv, void* device) {
  // If youre passing something meaningful in here youre #doingitwrong
  DASSERT(!device);

  return MsdMockDriver::cast(drv)->CreateDevice();
}

void msd_device_destroy(msd_device_t* dev) {
  // TODO(MA-28) should be
  // MsdMockDriver::cast(drv)->DestroyDevice(MsdMockDevice::cast(dev));
  delete MsdMockDevice::cast(dev);
}

msd_connection_t* msd_device_open(msd_device_t* dev, msd_client_id_t client_id) {
  return MsdMockDevice::cast(dev)->Open(client_id);
}

void msd_connection_close(msd_connection_t* connection) {
  delete MsdMockConnection::cast(connection);
}

magma_status_t msd_device_query(msd_device_t* device, uint64_t id, uint64_t* value_out) {
  switch (id) {
    case MAGMA_QUERY_DEVICE_ID:
      *value_out = MsdMockDevice::cast(device)->GetDeviceId();
      return MAGMA_STATUS_OK;
    default:
      return MAGMA_STATUS_INVALID_ARGS;
  }
}

msd_context_t* msd_connection_create_context(msd_connection_t* dev) {
  return MsdMockConnection::cast(dev)->CreateContext();
}

void msd_context_destroy(msd_context_t* ctx) { delete MsdMockContext::cast(ctx); }

msd_buffer_t* msd_buffer_import(uint32_t handle) {
  if (!g_bufmgr)
    g_bufmgr.reset(new MsdMockBufferManager());
  return g_bufmgr->CreateBuffer(handle);
}

void msd_buffer_destroy(msd_buffer_t* buf) {
  if (!g_bufmgr)
    g_bufmgr.reset(new MsdMockBufferManager());

  if (buf)
    return g_bufmgr->DestroyBuffer(MsdMockBuffer::cast(buf));
}

magma_status_t msd_context_execute_command_buffer_with_resources(
    struct msd_context_t* ctx, struct magma_system_command_buffer* command_buffer,
    struct magma_system_exec_resource* exec_resources, struct msd_buffer_t** buffers,
    struct msd_semaphore_t** wait_semaphores, struct msd_semaphore_t** signal_semaphores) {
  return MsdMockContext::cast(ctx)->ExecuteCommandBufferWithResources(command_buffer, buffers);
}

magma_status_t msd_context_execute_immediate_commands(msd_context_t* ctx, uint64_t commands_size,
                                                      void* commands, uint64_t semaphore_count,
                                                      msd_semaphore_t** semaphores) {
  return MAGMA_STATUS_OK;
}

void MsdMockBufferManager::SetTestBufferManager(std::unique_ptr<MsdMockBufferManager> bufmgr) {
  g_bufmgr = std::move(bufmgr);
}

MsdMockBufferManager* MsdMockBufferManager::ScopedMockBufferManager::get() {
  return g_bufmgr.get();
}

MsdMockContext::~MsdMockContext() { connection_->DestroyContext(this); }

magma_status_t msd_semaphore_import(uint32_t handle, msd_semaphore_t** semaphore_out) {
  *semaphore_out =
      reinterpret_cast<msd_semaphore_t*>(magma::PlatformSemaphore::Import(handle).release());
  DASSERT(*semaphore_out);
  return MAGMA_STATUS_OK;
}

void msd_semaphore_release(msd_semaphore_t* semaphore) {
  delete reinterpret_cast<magma::PlatformSemaphore*>(semaphore);
}

void msd_connection_release_buffer(msd_connection_t* connection, msd_buffer_t* buffer) {}

magma_status_t msd_connection_map_buffer_gpu(struct msd_connection_t* connection,
                                             struct msd_buffer_t* buffer, uint64_t gpu_va,
                                             uint64_t page_offset, uint64_t page_count,
                                             uint64_t flags) {
  return MAGMA_STATUS_OK;
}

magma_status_t msd_connection_unmap_buffer_gpu(struct msd_connection_t* connection,
                                               struct msd_buffer_t* buffer, uint64_t gpu_va) {
  return MAGMA_STATUS_OK;
}

magma_status_t msd_connection_commit_buffer(struct msd_connection_t* connection,
                                            struct msd_buffer_t* buffer, uint64_t page_offset,
                                            uint64_t page_count) {
  return MAGMA_STATUS_OK;
}

void msd_connection_set_notification_callback(struct msd_connection_t* connection,
                                              msd_connection_notification_callback_t callback,
                                              void* token) {}

magma_status_t msd_connection_enable_performance_counters(msd_connection_t* abi_connection,
                                                          const uint64_t* counters,
                                                          uint64_t counter_count) {
  return MAGMA_STATUS_OK;
}

struct MsdMockPool : public msd_perf_count_pool {};

magma_status_t msd_connection_create_performance_counter_buffer_pool(
    struct msd_connection_t* connection, uint64_t pool_id, struct msd_perf_count_pool** pool_out) {
  *pool_out = new MsdMockPool;
  return MAGMA_STATUS_OK;
}

magma_status_t msd_connection_release_performance_counter_buffer_pool(
    struct msd_connection_t* connection, struct msd_perf_count_pool* pool) {
  delete static_cast<MsdMockPool*>(pool);
  return MAGMA_STATUS_OK;
}

magma_status_t msd_connection_dump_performance_counters(struct msd_connection_t* abi_connection,
                                                        struct msd_perf_count_pool* pool,
                                                        uint32_t trigger_id) {
  return MAGMA_STATUS_OK;
}

magma_status_t msd_connection_clear_performance_counters(struct msd_connection_t* connection,
                                                         const uint64_t* counters,
                                                         uint64_t counter_count) {
  return MAGMA_STATUS_UNIMPLEMENTED;
}

magma_status_t msd_connection_add_performance_counter_buffer_offset_to_pool(
    struct msd_connection_t*, struct msd_perf_count_pool* abi_pool, struct msd_buffer_t* abi_buffer,
    uint64_t buffer_id, uint64_t buffer_offset, uint64_t buffer_size) {
  return MAGMA_STATUS_OK;
}

magma_status_t msd_connection_remove_performance_counter_buffer_from_pool(
    struct msd_connection_t*, struct msd_perf_count_pool* pool, struct msd_buffer_t* buffer) {
  return MAGMA_STATUS_OK;
}
