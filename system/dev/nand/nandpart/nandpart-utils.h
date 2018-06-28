// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/nand.h>

#include <zircon/boot/image.h>
#include <zircon/types.h>

zx_status_t SanitizePartitionMap(zbi_partition_map_t* pmap, const nand_info_t& nand_info);
