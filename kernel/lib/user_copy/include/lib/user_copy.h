// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <arch/user_copy.h>
#include <magenta/compiler.h>
#include <err.h>

__BEGIN_CDECLS

inline status_t copy_to_user_unsafe(void* dst, const void* src, size_t len) {
  return arch_copy_to_user(dst, src, len);
}
inline status_t copy_from_user_unsafe(void* dst, const void* src, size_t len) {
  return arch_copy_from_user(dst, src, len);
}

__END_CDECLS
