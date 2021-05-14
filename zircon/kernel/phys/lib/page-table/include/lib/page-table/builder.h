// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PHYS_LIB_PAGE_TABLE_INCLUDE_LIB_PAGE_TABLE_BUILDER_H_
#define ZIRCON_KERNEL_PHYS_LIB_PAGE_TABLE_INCLUDE_LIB_PAGE_TABLE_BUILDER_H_

#include <lib/page-table/arch/arm64/builder.h>
#include <lib/page-table/arch/x86/builder.h>
#include <lib/page-table/types.h>

namespace page_table {

// Convenience class for building address spaces.
#if defined(__x86_64__) || defined(__i386__)
using AddressSpaceBuilder = ::page_table::x86::AddressSpaceBuilder;
#elif defined(__aarch64__)
using AddressSpaceBuilder = ::page_table::arm64::AddressSpaceBuilder;
#else
#error Unknown architecture.
#endif

}  // namespace page_table

#endif  // ZIRCON_KERNEL_PHYS_LIB_PAGE_TABLE_INCLUDE_LIB_PAGE_TABLE_BUILDER_H_
