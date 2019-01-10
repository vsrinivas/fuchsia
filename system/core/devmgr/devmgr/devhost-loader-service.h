// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/unique_fd.h>
#include <lib/fdio/namespace.h>
#include <lib/zx/channel.h>
#include <loader-service/loader-service.h>

namespace devmgr {

// A loader service for devhosts that restricts access to dynamic libraries.
class DevhostLoaderService {
public:
    zx_status_t Init();
    ~DevhostLoaderService();

    // Connect to the loader service.
    zx_status_t Connect(zx::channel* out);

    const fbl::unique_fd& root() const { return root_; }

private:
    fbl::unique_fd root_;
    fdio_ns_t* ns_ = nullptr;
    loader_service_t* svc_ = nullptr;
};

} // namespace devmgr
