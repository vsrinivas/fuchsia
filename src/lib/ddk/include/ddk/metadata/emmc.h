// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_DDK_INCLUDE_DDK_METADATA_EMMC_H_
#define SRC_LIB_DDK_INCLUDE_DDK_METADATA_EMMC_H_

#include <stdbool.h>

typedef struct emmc_config {
  // If true, the discard command may be issued to eMMC devices on this bus. The default value is
  // true if no metadata is specified.
  bool enable_trim;
} emmc_config_t;

#endif  // SRC_LIB_DDK_INCLUDE_DDK_METADATA_EMMC_H_
