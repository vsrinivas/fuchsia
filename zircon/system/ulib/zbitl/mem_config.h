// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_ZBITL_MEM_CONFIG_H_
#define ZIRCON_SYSTEM_ULIB_ZBITL_MEM_CONFIG_H_

#include <lib/zbitl/items/mem_config.h>
#include <zircon/boot/e820.h>

#include <efi/boot-services.h>

namespace zbitl::internal {

// Convert descriptions of a physical memory range into the common |zbi_mem_range_t| type.
//
// Exposed for testing.
zbi_mem_range_t ToMemRange(const e820entry_t& range);
zbi_mem_range_t ToMemRange(const efi_memory_descriptor& range);

}  // namespace zbitl::internal

#endif  // ZIRCON_SYSTEM_ULIB_ZBITL_MEM_CONFIG_H_
