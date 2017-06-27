// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <stddef.h>

__BEGIN_CDECLS

// See memcpy().  Invocations of this implementation are guaranteed to not be optimized
// away.  I.e. if you invoke mandatory_memcpy(dst, src, n), and then access
// |dst|, the compiler will not issue an access to |src|.  The intention is that
// this is suitable for copying shared data from an untrusted source.  The
// untrusted source should not be able to modify |dst| after the
// mandatory_memcpy() completes.
void* mandatory_memcpy(void* dst, const void* src, size_t n);

// See memset().  Invocations of this implementation are guaranteed to not be optimized
// away.  I.e. if you invoke mandatory_memset(dst, c, n), |dst| will be
// written to, even if |dst| will not be read from again for the rest of its
// lifetime.  The intention is that this is suitable for zeroing a buffer
// containing sensitive data.
void* mandatory_memset(void* dst, int c, size_t n);

__END_CDECLS
