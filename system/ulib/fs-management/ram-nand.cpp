// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs-management/ram-nand.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fbl/unique_fd.h>
#include <lib/fzl/fdio.h>
#include <zircon/device/ram-nand.h>
#include <zircon/types.h>
#include <zircon/nand/c/fidl.h>

namespace {

constexpr char kBasePath[] = "/dev/misc/nand-ctl";

} // namespace

zx_status_t create_ram_nand(const ram_nand_info_t* config, char* out_path) {
    fbl::unique_fd control(open(kBasePath, O_RDWR));
    if (!control) {
        fprintf(stderr, "Could not open nand-ctl\n");
        return ZX_ERR_BAD_STATE;
    }

    ram_nand_name_t response = {};
    ssize_t rc = (config->vmo == ZX_HANDLE_INVALID)
                     ? ioctl_ram_nand_create(control.get(), config, &response)
                     : ioctl_ram_nand_create_vmo(control.get(), config, &response);
    if (rc < 0) {
        fprintf(stderr, "Could not create ram_nand device\n");
        return static_cast<zx_status_t>(rc);
    }

    strcpy(out_path, kBasePath);
    out_path[sizeof(kBasePath) - 1] = '/';
    strcpy(out_path + sizeof(kBasePath), response.name);
    return ZX_OK;
}

zx_status_t destroy_ram_nand(const char* ram_nand_path) {
    fbl::unique_fd ram_nand(open(ram_nand_path, O_RDWR));
    if (!ram_nand) {
        fprintf(stderr, "Could not open ram_nand\n");
        return ZX_ERR_BAD_STATE;
    }
    fzl::FdioCaller caller(fbl::move(ram_nand));

    zx_status_t status;
    zx_status_t io_status = zircon_nand_RamNandUnlink(caller.borrow_channel(), &status);
    if (io_status != ZX_OK) {
        fprintf(stderr, "Could not unlink ram_nand (FIDL error)\n");
        return io_status;
    } else if (status != ZX_OK) {
        fprintf(stderr, "Could not open ram_nand\n");
        return status;
    }
    return ZX_OK;
}
