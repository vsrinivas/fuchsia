// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_NAND_DRIVERS_NANDPART_NANDPART_UTILS_H_
#define SRC_DEVICES_NAND_DRIVERS_NANDPART_NANDPART_UTILS_H_

#include <fuchsia/hardware/nand/c/banjo.h>
#include <zircon/boot/image.h>
#include <zircon/types.h>

zx_status_t SanitizePartitionMap(zbi_partition_map_t* pmap, const nand_info_t& nand_info);

#endif  // SRC_DEVICES_NAND_DRIVERS_NANDPART_NANDPART_UTILS_H_
