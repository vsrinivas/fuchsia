// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_USERABI_INCLUDE_LIB_USERABI_USERBOOT_INTERNAL_H_
#define ZIRCON_KERNEL_LIB_USERABI_INCLUDE_LIB_USERABI_USERBOOT_INTERNAL_H_

#include <zircon/syscalls/resource.h>

#include <object/handle.h>

HandleOwner get_resource_handle(zx_rsrc_kind_t kind);

#endif  // ZIRCON_KERNEL_LIB_USERABI_INCLUDE_LIB_USERABI_USERBOOT_INTERNAL_H_
