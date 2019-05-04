// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>

#include <fuchsia/paver/c/fidl.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/directory.h>
#include <lib/zx/channel.h>
#include <lib/zx/vmo.h>
#include <zxtest/zxtest.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

namespace {

TEST(MiscSvcTest, PaverSvccEnumeratesSuccessfully) {
    zx::channel svc_local, svc_remote;
    ASSERT_OK(zx::channel::create(0, &svc_local, &svc_remote));
    ASSERT_OK(fdio_service_connect("/svc", svc_remote.release()));

    zx::channel local, remote;
    ASSERT_OK(zx::channel::create(0, &local, &remote));
    ASSERT_OK(fdio_service_connect_at(svc_local.get(), fuchsia_paver_Paver_Name, remote.release()));

    zx_status_t status;
    fuchsia_paver_Configuration* configuration;
    ASSERT_OK(fuchsia_paver_PaverQueryActiveConfiguration(local.get(), &status, &configuration));
}

} // namespace
