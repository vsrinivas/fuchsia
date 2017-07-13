// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>
#include <magenta/syscalls/types.h>

#include <magenta/syscalls/pci.h>
#include <magenta/syscalls/object.h>

__BEGIN_CDECLS

#if defined(__clang__)
#define MX_SYSCALL_PARAM_ATTR(x)   __attribute__((annotate("mx_" #x)))
#else
#define MX_SYSCALL_PARAM_ATTR(x)   // no-op
#endif

#include <magenta/syscalls/definitions.h>

// Compatibility wrappers for deprecated syscalls also go here, when
// there are any.

__END_CDECLS
