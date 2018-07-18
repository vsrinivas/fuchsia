// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/magma/tests/helper/config_namespace_helper.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <lib/fxl/files/unique_fd.h>
#include <lib/fdio/namespace.h>

bool InstallConfigDirectoryIntoGlobalNamespace()
{
  fxl::UniqueFD fd(open("/system/data/vulkan", O_RDONLY | O_DIRECTORY));
    if (!fd.is_valid()) {
        fprintf(stderr, "Could not open /system/data/vulkan: %s", strerror(errno));
        return false;
    }

    fdio_ns_t* ns;
    zx_status_t st = fdio_ns_get_installed(&ns);
    if (st != ZX_OK) {
        fprintf(stderr, "Could not get installed namespace: %d", st);
        return false;
    }

    st = fdio_ns_bind_fd(ns, "/config/vulkan", fd.get());
    if (st != ZX_OK) {
        fprintf(stderr, "Could not install entry to /config/vulkan: %d", st);
        return false;
    }

    return true;
}
