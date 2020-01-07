// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SYSROOT_ZIRCON_SYSCALLS_H_
#define SYSROOT_ZIRCON_SYSCALLS_H_

#include <zircon/syscalls/types.h>
#include <zircon/types.h>

#include <zircon/syscalls/object.h>
#include <zircon/syscalls/pci.h>
#include <zircon/syscalls/profile.h>

#if defined(__clang__)
#define ZX_ACQUIRE_HANDLE __attribute__((acquire_handle("Fuchsia")))
#define ZX_RELEASE_HANDLE __attribute__((release_handle("Fuchsia")))
#define ZX_USE_HANDLE __attribute__((use_handle("Fuchsia")))
#else
#define ZX_ACQUIRE_HANDLE
#define ZX_RELEASE_HANDLE
#define ZX_USE_HANDLE
#endif

__BEGIN_CDECLS

#include <zircon/syscalls/definitions.h>

// Compatibility wrappers for deprecated syscalls also go here, when
// there are any.

__END_CDECLS

#endif  // SYSROOT_ZIRCON_SYSCALLS_H_
