// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs-management/ram-nand.h>

#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fbl/string_buffer.h>
#include <fbl/unique_fd.h>
#include <lib/fzl/fdio.h>
#include <zircon/device/device.h>
#include <zircon/types.h>
#include <zircon/nand/c/fidl.h>

#include <utility>

namespace {

constexpr char kBasePath[] = "/dev/misc/nand-ctl";

} // namespace

namespace fs_mgmt {

zx_status_t RamNand::Create(const zircon_nand_RamNandInfo* config, std::unique_ptr<RamNand>* out) {
    fbl::unique_fd control(open(kBasePath, O_RDWR));
    fzl::FdioCaller caller(std::move(control));
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

    fbl::StringBuffer<PATH_MAX> path;
    path.AppendPrintf("%s/%s", kBasePath, name);

    fbl::unique_fd ram_nand(open(path.c_str(), O_RDWR));
    if (!ram_nand) {
        fprintf(stderr, "Could not open ram_nand\n");
        return ZX_ERR_INTERNAL;
    }

    out->reset(new RamNand(path.ToStringPiece(), std::move(ram_nand)));
    return ZX_OK;
}

RamNand::~RamNand() {
    if (unbind && fd_) {
      zx_status_t status = static_cast<zx_status_t>(ioctl_device_unbind(fd_.get()));
      if (status != ZX_OK) {
          fprintf(stderr, "Could not unbind ram_nand, %d\n", status);
      }
    }
}

} // namespace fs_mgmt
