// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_HYPERVISOR_INCLUDE_HYPERVISOR_KERNEL_ASPACE_H_
#define ZIRCON_KERNEL_HYPERVISOR_INCLUDE_HYPERVISOR_KERNEL_ASPACE_H_

#include <align.h>
#include <zircon/types.h>

#include <arch/kernel_aspace.h>

constexpr zx_vaddr_t kGuestGlobalAspaceBase = PAGE_ALIGN((USER_ASPACE_BASE + USER_ASPACE_SIZE) / 2);
constexpr size_t kGuestGlobalAspaceSize = 512ul << 30;

const zx_vaddr_t kGuestUserAspaceBase = USER_ASPACE_BASE;
const size_t kGuestUserAspaceSize = kGuestGlobalAspaceBase - USER_ASPACE_BASE;

#endif  // ZIRCON_KERNEL_HYPERVISOR_INCLUDE_HYPERVISOR_KERNEL_ASPACE_H_
