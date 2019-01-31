// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSCALLS_H_
#define ZIRCON_SYSCALLS_H_

#include <zircon/types.h>
#include <zircon/syscalls/types.h>

#include <zircon/syscalls/pci.h>
#include <zircon/syscalls/object.h>
#include <zircon/syscalls/profile.h>

__BEGIN_CDECLS

#if defined(__clang__)
#define ZX_SYSCALL_PARAM_ATTR(x)   __attribute__((annotate("zx_" #x)))
#else
#define ZX_SYSCALL_PARAM_ATTR(x)   // no-op
#endif

#include <zircon/syscalls/definitions.h>

// Compatibility wrappers for deprecated syscalls also go here, when
// there are any.

__END_CDECLS

#endif // ZIRCON_SYSCALLS_H_
