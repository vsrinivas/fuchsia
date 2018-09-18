// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

// Methods in this file exist to provide default stubs for MSI
// support so that individual platforms do not need to provide
// them if they only partially support MSI.

#include <dev/interrupt.h>

#include <stdbool.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__WEAK bool msi_is_supported() {
    return false;
}

__WEAK bool msi_supports_masking() {
    return false;
}

__WEAK void msi_mask_unmask(const msi_block_t* block, uint msi_id, bool mask) {
    PANIC_UNIMPLEMENTED;
}

__WEAK zx_status_t msi_alloc_block(uint requested_irqs,
                                   bool can_target_64bit,
                                   bool is_msix,
                                   msi_block_t* out_block) {
    PANIC_UNIMPLEMENTED;
    __UNREACHABLE;
}

__WEAK void msi_free_block(msi_block_t* block) {
    PANIC_UNIMPLEMENTED;
}

__WEAK void msi_register_handler(const msi_block_t* block,
                                 uint msi_id,
                                 int_handler handler,
                                 void *ctx) {
    PANIC_UNIMPLEMENTED;
}
