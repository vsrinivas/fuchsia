// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs-management/ram-nand.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fbl/unique_fd.h>
#include <zircon/device/ram-nand.h>
#include <zircon/types.h>

namespace {

constexpr char base_path[] = "/dev/misc/nand-ctl";

}  // namespace

int create_ram_nand(const ram_nand_info_t* config, char* out_path) {
    fbl::unique_fd control(open(base_path, O_RDWR));
    if (!control) {
        fprintf(stderr, "Could not open nand-ctl\n");
        return -1;
    }

    ram_nand_name_t response = {};
    if (ioctl_ram_nand_create(control.get(), config, &response) < 0) {
        fprintf(stderr, "Could not create ram_nand device\n");
        return -1;
    }

    strcpy(out_path, base_path);
    out_path[sizeof(base_path) - 1] = '/';
    strcpy(out_path + sizeof(base_path), response.name);
    return 0;
}

int destroy_ram_nand(const char* ram_nand_path) {
    fbl::unique_fd ram_nand(open(ram_nand_path, O_RDWR));
    if (!ram_nand) {
        fprintf(stderr, "Could not open ram_nand\n");
        return -1;
    }

    if (ioctl_ram_nand_unlink(ram_nand.get()) != ZX_OK) {
        fprintf(stderr, "Could not shut off ram_nand\n");
        return -1;
    }
    return 0;
}
