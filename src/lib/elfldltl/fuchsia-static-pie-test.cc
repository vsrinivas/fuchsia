// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fuchsia-static-pie.h"

#include <zircon/syscalls.h>

#include <string_view>

using namespace std::literals;

namespace {

void DebugWrite(std::string_view s) { zx_debug_write(s.data(), s.size()); }

[[noreturn]] void Panic(std::string_view str) {
  DebugWrite(str);
  __builtin_trap();
}

}  // namespace

extern "C" [[noreturn]] void _start(zx_handle_t bootstrap, const void* vdso) {
  StaticPieSetup(vdso);

  // Give the kernel time to drain the debuglog before we produce any output.
  zx_nanosleep(zx_deadline_after(ZX_SEC(2)));

  static int data_location;
  static int* volatile data_address = &data_location;
  static int* const volatile relro_address = &data_location;

  // This makes absolutely sure the compiler doesn't think it knows how to
  // optimize away the fetches and tests.
  int* from_data;
  __asm__("" : "=g"(from_data) : "0"(data_address));

  // Since data_location has internal linkage, the references here will use
  // pure PC-relative address materialization.

  if (from_data != &data_location) {
    Panic("address in data not relocated properly"sv);
  }

  int* from_relro;
  __asm__("" : "=g"(from_relro) : "0"(relro_address));
  if (from_relro != &data_location) {
    Panic("address in RELRO not relocated properly"sv);
  }

  DebugWrite("Hello, world!\n"sv);
  zx_process_exit(0);
}
