// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PHYS_INCLUDE_PHYS_KERNEL_PACKAGE_H_
#define ZIRCON_KERNEL_PHYS_INCLUDE_PHYS_KERNEL_PACKAGE_H_

#include <ktl/string_view.h>

// The default kernel package (i.e., STORAGE_KERNEL BOOTFS namespace) in which
// we will pick a kernel ZBI to boot.
//
// TODO(fxbug.dev/68585): Support kernel page selection via a boot option.
constexpr ktl::string_view kDefaultKernelPackage = "zircon";

// The name of the kernel ZBI within a kernel package.
constexpr ktl::string_view kKernelZbiName = "kernel.zbi";

#endif  // ZIRCON_KERNEL_PHYS_INCLUDE_PHYS_KERNEL_PACKAGE_H_
