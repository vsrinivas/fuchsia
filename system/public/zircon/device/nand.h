// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/nand/c/fidl.h>

#define NAND_CLASS_PARTMAP 1   // NAND device contains multiple partitions.
#define NAND_CLASS_FTL 2       // NAND device is a FTL partition.
#define NAND_CLASS_BBS 3       // NAND device is a bad block skip partition.
#define NAND_CLASS_DUMMY 4     // Test or otherwise unknown device.

// TODO(ZX-2677): Remove this when done.
typedef zircon_nand_Info nand_info_t;
