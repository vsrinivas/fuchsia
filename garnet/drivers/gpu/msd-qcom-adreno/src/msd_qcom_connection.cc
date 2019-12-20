// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd_qcom_connection.h"

#include <msd.h>

#include <magma_util/macros.h>

#include "msd_qcom_context.h"

void msd_connection_close(msd_connection_t* connection) {
  DMESSAGE("msd_connection_close not implemented");
}

msd_context_t* msd_connection_create_context(msd_connection_t* abi_connection) {
  return new MsdQcomAbiContext(std::make_shared<MsdQcomContext>());
}

magma_status_t msd_connection_map_buffer_gpu(msd_connection_t* abi_connection,
                                             msd_buffer_t* abi_buffer, uint64_t gpu_va,
                                             uint64_t page_offset, uint64_t page_count,
                                             uint64_t flags) {
  return DRET(MAGMA_STATUS_UNIMPLEMENTED);
}

magma_status_t msd_connection_unmap_buffer_gpu(msd_connection_t* abi_connection,
                                               msd_buffer_t* abi_buffer, uint64_t gpu_va) {
  return DRET(MAGMA_STATUS_UNIMPLEMENTED);
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
