// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ld/abi.h>

#include "static-tls-get-addr.h"

namespace ld::abi {
namespace {

#if defined(__x86_64__)

inline void* ThreadPointer() {
#ifdef __clang__
  return *(void* [[clang::address_space(257)]]*){};
#else
  void* tp;
  __asm__("mov %%fs:0,%0" : "=r"(tp));
  return tp;
#endif
}

#else

inline void* ThreadPointer() { return __builtin_thread_pointer(); }

#endif

}  // namespace

void* __tls_get_addr(ld::TlsGetAddrGot& got) {
  return StaticTlsGetAddr(got, _ld_static_tls_offsets, ThreadPointer());
}

}  // namespace ld::abi
