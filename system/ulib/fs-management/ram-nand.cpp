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

zx_status_t create_ram_nand(const zircon_nand_RamNandInfo* config, char* out_path) {
    fbl::unique_fd control(open(kBasePath, O_RDWR));
    fzl::FdioCaller caller(fbl::move(control));
    char name[zircon_nand_NAME_LEN + 1];
    size_t out_name_size;
    zx_status_t status;
    zx_status_t st = zircon_nand_RamNandCtlCreateDevice(caller.borrow_channel(), config, &status,
                                                        name, zircon_nand_NAME_LEN, &out_name_size);
    if (st != ZX_OK || status != ZX_OK) {
        st = st != ZX_OK ? st : status;
        fprintf(stderr, "Could not create ram_nand device, %d\n", st);
        return st;
    }
    name[out_name_size] = '\0';

    strcpy(out_path, kBasePath);
    out_path[sizeof(kBasePath) - 1] = '/';
    strcpy(out_path + sizeof(kBasePath), name);
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
    if (io_status != ZX_OK || status != ZX_OK) {
        status = io_status != ZX_OK ? io_status : status;
        fprintf(stderr, "Could not unlink ram_nand, %d\n", status);
        return status;
    }
    return ZX_OK;
}
