// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd_qcom_context.h"

#include <msd.h>

#include <magma_util/macros.h>

void msd_context_destroy(msd_context_t* abi_context) {
  delete MsdQcomAbiContext::cast(abi_context);
}

magma_status_t msd_context_execute_immediate_commands(msd_context_t* ctx, uint64_t commands_size,
                                                      void* commands, uint64_t semaphore_count,
                                                      msd_semaphore_t** msd_semaphores) {
  return DRET(MAGMA_STATUS_UNIMPLEMENTED);
}

magma_status_t msd_context_execute_command_buffer_with_resources(
    struct msd_context_t* ctx, struct magma_system_command_buffer* command_buffer,
    struct magma_system_exec_resource* exec_resources, struct msd_buffer_t** buffers,
    struct msd_semaphore_t** wait_semaphores, struct msd_semaphore_t** signal_semaphores) {
  return DRET(MAGMA_STATUS_UNIMPLEMENTED);
}
