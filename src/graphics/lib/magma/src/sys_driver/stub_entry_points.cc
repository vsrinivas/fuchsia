// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd.h"

// These entrypoints are used when the driver doesn't implement an extension to the interface yet.

magma_status_t __attribute__((weak))
msd_connection_enable_performance_counters(msd_connection_t* abi_connection,
                                           const uint64_t* counters, uint64_t counter_count) {
  return MAGMA_STATUS_UNIMPLEMENTED;
}

magma_status_t __attribute__((weak))
msd_connection_create_performance_counter_buffer_pool(struct msd_connection_t* connection,
                                                      uint64_t pool_id,
                                                      struct msd_perf_count_pool** pool_out) {
  return MAGMA_STATUS_UNIMPLEMENTED;
}

magma_status_t __attribute__((weak))
msd_connection_release_performance_counter_buffer_pool(struct msd_connection_t* connection,
                                                       struct msd_perf_count_pool* pool) {
  return MAGMA_STATUS_UNIMPLEMENTED;
}

magma_status_t __attribute__((weak))
msd_connection_dump_performance_counters(struct msd_connection_t* abi_connection,
                                         struct msd_perf_count_pool* pool, uint32_t trigger_id) {
  return MAGMA_STATUS_UNIMPLEMENTED;
}

magma_status_t __attribute__((weak))
msd_connection_clear_performance_counters(struct msd_connection_t* connection,
                                          const uint64_t* counters, uint64_t counter_count) {
  return MAGMA_STATUS_UNIMPLEMENTED;
}

magma_status_t __attribute__((weak)) msd_connection_add_performance_counter_buffer_offset_to_pool(
    struct msd_connection_t*, struct msd_perf_count_pool* abi_pool, struct msd_buffer_t* abi_buffer,
    uint64_t buffer_id, uint64_t buffer_offset, uint64_t buffer_size) {
  return MAGMA_STATUS_UNIMPLEMENTED;
}

magma_status_t __attribute__((weak))
msd_connection_remove_performance_counter_buffer_from_pool(struct msd_connection_t*,
                                                           struct msd_perf_count_pool* pool,
                                                           struct msd_buffer_t* buffer) {
  return MAGMA_STATUS_UNIMPLEMENTED;
}
