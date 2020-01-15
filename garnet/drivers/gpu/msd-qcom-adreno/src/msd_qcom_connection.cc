// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd_qcom_connection.h"

#include <msd.h>

#include <magma_util/macros.h>

#include "msd_qcom_buffer.h"
#include "msd_qcom_context.h"

void msd_connection_close(msd_connection_t* connection) {
  delete MsdQcomAbiConnection::cast(connection);
}

msd_context_t* msd_connection_create_context(msd_connection_t* abi_connection) {
  return new MsdQcomAbiContext(std::make_shared<MsdQcomContext>());
}

magma_status_t msd_connection_map_buffer_gpu(msd_connection_t* abi_connection,
                                             msd_buffer_t* abi_buffer, uint64_t gpu_addr,
                                             uint64_t page_offset, uint64_t page_count,
                                             uint64_t flags) {
  auto connection = MsdQcomAbiConnection::cast(abi_connection)->ptr();
  auto buffer = MsdQcomAbiBuffer::cast(abi_buffer)->ptr();

  std::shared_ptr<GpuMapping> mapping;
  magma::Status status = AddressSpace::MapBufferGpu(connection->address_space(), buffer, gpu_addr,
                                                    page_offset, page_count, &mapping);
  if (!status.ok())
    return DRET_MSG(status.get(), "MapBufferGpu failed");

  if (!connection->address_space()->AddMapping(mapping))
    return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "failed to add mapping");

  DLOG("MapBufferGpu %lu addr 0x%lx", mapping->BufferId(), gpu_addr);

  return MAGMA_STATUS_OK;
}

magma_status_t msd_connection_unmap_buffer_gpu(msd_connection_t* abi_connection,
                                               msd_buffer_t* abi_buffer, uint64_t gpu_va) {
  return MAGMA_STATUS_UNIMPLEMENTED;
}

void msd_connection_release_buffer(msd_connection_t* abi_connection, msd_buffer_t* abi_buffer) {
  DMESSAGE("msd_connection_release_buffer not implemented");
}

magma_status_t msd_connection_commit_buffer(msd_connection_t* abi_connection,
                                            msd_buffer_t* abi_buffer, uint64_t page_offset,
                                            uint64_t page_count) {
  return DRET(MAGMA_STATUS_UNIMPLEMENTED);
}

void msd_connection_set_notification_callback(struct msd_connection_t* connection,
                                              msd_connection_notification_callback_t callback,
                                              void* token) {
  DMESSAGE("msd_connection_set_notification_callback not implemented");
}
