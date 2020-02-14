// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_NAND_DRIVERS_NANDPART_NANDPART_UTILS_H_
#define SRC_STORAGE_NAND_DRIVERS_NANDPART_NANDPART_UTILS_H_

#include <zircon/boot/image.h>
#include <zircon/types.h>

#include <ddk/protocol/nand.h>

zx_status_t SanitizePartitionMap(zbi_partition_map_t* pmap,
                                 const fuchsia_hardware_nand_Info& nand_info);

#endif  // SRC_STORAGE_NAND_DRIVERS_NANDPART_NANDPART_UTILS_H_
