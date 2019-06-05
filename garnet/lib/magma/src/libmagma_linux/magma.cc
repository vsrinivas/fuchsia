// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma.h"
#include "garnet/lib/magma/include/virtio/virtio_magma.h"
#include "garnet/lib/magma/src/libmagma_linux/virtmagma_util.h"

// TODO(MA-623): support an object that is a parent of magma_connection_t
// This class is a temporary workaround to support magma APIs that do not
// pass in generic objects capable of holding file descriptors, e.g.
// magma_duplicate_handle.
std::map<uint32_t, virtmagma_handle_t*>& GlobalHandleTable()
{
    static std::map<uint32_t, virtmagma_handle_t*> ht;
    return ht;
}

magma_status_t magma_wait_semaphores(const magma_semaphore_t* semaphores, uint32_t count,
                                     uint64_t timeout_ms, magma_bool_t wait_all)
{
    return MAGMA_STATUS_OK;
}
