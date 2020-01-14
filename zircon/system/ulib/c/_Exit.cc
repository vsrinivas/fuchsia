// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <unistd.h>
#include <zircon/compiler.h>
#include <zircon/sanitizer.h>
#include <zircon/syscalls.h>

namespace {

[[noreturn]] void Exit(int status) noexcept {
  status = __sanitizer_process_exit_hook(status);
  for (;;) {
    _zx_process_exit(status);
  }
}

}  // namespace

// Standard C specifies _Exit and C++ declares it noexcept.
// POSIX specifies _exit and in C++ it's not declared noexcept.
// They're actually the same thing, but a strict C++ compiler
// won't let us make them [[gnu::alias]] aliases directly here
// because the noexcept constitutes a type difference.  So both
// are separately defined identically and will be ICF'd together
// for the same effect as the alias, but we can't use the alias
// (unless we resort to assembly, but this seems cleaner).

__EXPORT _Noreturn void _Exit(int status) { Exit(status); }

__EXPORT _Noreturn void _exit(int status) { Exit(status); }
