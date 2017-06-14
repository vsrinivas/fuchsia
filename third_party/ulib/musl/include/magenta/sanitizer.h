// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// Interfaces declared in this file are intended for the use of sanitizer
// runtime library implementation code.  Each sanitizer runtime works only
// with the appropriately sanitized build of libc.  These functions should
// never be called when using the unsanitized libc.  But these names are
// always exported so that the libc ABI is uniform across sanitized and
// unsanitized builds (only unsanitized shared library binaries are used at
// link time, including linking the sanitizer runtime shared libraries).

#include <magenta/compiler.h>
#include <string.h>

__BEGIN_CDECLS

// These are aliases for the functions defined in libc, which are always
// the unsanitized versions.  The sanitizer runtimes can call them by these
// aliases when they are overriding libc's definitions of the unadorned
// symbols.
__typeof(memcpy) __unsanitized_memcpy;
__typeof(memmove) __unsanitized_memmove;
__typeof(memset) __unsanitized_memset;

__END_CDECLS
