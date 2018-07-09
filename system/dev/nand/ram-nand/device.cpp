// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device.h"

#include <stdlib.h>

#include <zircon/device/ram-nand.h>

zx_status_t RamNandDevice::Bind() {
    char name[NAME_MAX];
    zx_status_t status = ram_nand_.Init(name);
    if (status != ZX_OK) {
        return status;
    }
    return DdkAdd(name);
}

zx_off_t RamNandDevice::DdkGetSize() {
    return ram_nand_.GetSize();
}

void RamNandDevice::DdkUnbind() {
    ram_nand_.Unbind();
    DdkRemove();
}

zx_status_t RamNandDevice::DdkIoctl(uint32_t op, const void* in_buf, size_t in_len,
                                    void* out_buf, size_t out_len, size_t* out_actual) {
    zx_status_t status = ram_nand_.Ioctl(op, in_buf, in_len, out_buf, out_len, out_actual);
    if (status == ZX_OK && op == IOCTL_RAM_NAND_UNLINK) {
        DdkRemove();
    }
    return status;
}

void RamNandDevice::Query(nand_info_t* info_out, size_t* nand_op_size_out) {
    ram_nand_.Query(info_out, nand_op_size_out);
}

void RamNandDevice::Queue(nand_op_t* operation) {
    ram_nand_.Queue(operation);
}

zx_status_t RamNandDevice::GetFactoryBadBlockList(uint32_t* bad_blocks, uint32_t bad_block_len,
                                           uint32_t* num_bad_blocks) {
    return ram_nand_.GetFactoryBadBlockList(bad_blocks, bad_block_len, num_bad_blocks);
}
