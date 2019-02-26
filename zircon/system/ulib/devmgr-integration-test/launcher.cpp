// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/devmgr-integration-test/fixture.h>

#include <stdint.h>
#include <utility>

#include <fbl/algorithm.h>
#include <lib/devmgr-launcher/launch.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/directory.h>
#include <lib/zx/channel.h>
#include <zircon/processargs.h>
#include <zircon/status.h>

namespace devmgr_integration_test {

const char* IsolatedDevmgr::kSysdevDriver = "/boot/driver/test/sysdev.so";

devmgr_launcher::Args IsolatedDevmgr::DefaultArgs() {
    devmgr_launcher::Args args;
    args.sys_device_driver = kSysdevDriver;
    args.load_drivers.push_back("/boot/driver/test.so");
    args.driver_search_paths.push_back("/boot/driver/test");
    args.use_system_svchost = true;
    return args;
}

IsolatedDevmgr::~IsolatedDevmgr() {
    // Destroy the isolated devmgr
    if (job_.is_valid()) {
        job_.kill();
    }
}

zx_status_t IsolatedDevmgr::Create(devmgr_launcher::Args args,
                                   IsolatedDevmgr* out) {
    IsolatedDevmgr devmgr;

    zx::channel devfs;
    zx_status_t status = devmgr_launcher::Launch(std::move(args), &devmgr.job_, &devfs);
    if (status != ZX_OK) {
        return status;
    }

    int fd = -1;
    status = fdio_fd_create(devfs.release(), &fd);
    if (status != ZX_OK) {
        return status;
    }
    devmgr.devfs_root_.reset(fd);

    *out = std::move(devmgr);
    return ZX_OK;
}

} // namespace devmgr_integration_test
