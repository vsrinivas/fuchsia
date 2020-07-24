// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

// Methods in this file exist to provide default stubs for MSI
// support so that individual platforms do not need to provide
// them if they only partially support MSI.

#include <stdbool.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <dev/interrupt.h>

__WEAK bool msi_supports_masking() { return false; }

__WEAK void msi_mask_unmask(const msi_block_t* block, uint msi_id, bool mask) {
  PANIC_UNIMPLEMENTED;
}
