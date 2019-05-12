// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/vector.h>
#include <fs/pseudo-dir.h>
#include <fs/service.h>
#include <fs/synchronous-vfs.h>
#include <lib/zx/channel.h>

#include "util.h"

namespace bootsvc {

// A VFS used to provide services to the next process in the boot sequence.
class SvcfsService : public fbl::RefCounted<SvcfsService> {
public:
    // Create a SvcfsService using the given |dispatcher|.
    static fbl::RefPtr<SvcfsService> Create(async_dispatcher_t* dispatcher);

    // Add a |service| named |service_name| to the VFS.
    void AddService(const char* service_name, fbl::RefPtr<fs::Service> service);

    // Create a connection to the root of the VFS.
    zx_status_t CreateRootConnection(zx::channel* out);

private:
    explicit SvcfsService(async_dispatcher_t* dispatcher);

    SvcfsService(const SvcfsService&) = delete;
    SvcfsService(SvcfsService&&) = delete;
    SvcfsService& operator=(const SvcfsService&) = delete;
    SvcfsService& operator=(SvcfsService&&) = delete;

    fs::SynchronousVfs vfs_;
    // Root node for |vfs_|.
    fbl::RefPtr<fs::PseudoDir> root_;
};

// Create a service to retrieve boot arguments.
fbl::RefPtr<fs::Service> CreateArgumentsService(async_dispatcher_t* dispatcher, zx::vmo vmo,
                                                uint64_t size);

// Create a service to retrieve ZBI items.
fbl::RefPtr<fs::Service> CreateItemsService(async_dispatcher_t* dispatcher, zx::vmo vmo,
                                            ItemMap map);

// Create a service to provide the root job.
fbl::RefPtr<fs::Service> CreateRootJobService(async_dispatcher_t* dispatcher);

// Create a service to provide the root resource.
fbl::RefPtr<fs::Service> CreateRootResourceService(async_dispatcher_t* dispatcher);

} // namespace bootsvc
