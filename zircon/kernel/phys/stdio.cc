// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/boot-options/boot-options.h>
#include <stdarg.h>
#include <stdio.h>

#include <phys/stdio.h>

FILE FILE::stdout_;

PhysConsole::PhysConsole()
    : null_{[](void*, ktl::string_view s) -> int { return static_cast<int>(s.length()); }, nullptr},
      mux_files_{} {}

PhysConsole& PhysConsole::Get() {
  static PhysConsole gConsole;
  return gConsole;
}

void InitStdout() { FILE::stdout_ = PhysConsole::Get().mux_; }

void PhysConsole::SetMux(size_t idx, const FILE& f) {
  mux_files_[idx] = f;
  mux_.files()[idx] = &mux_files_[idx];
}

void debugf(const char* fmt, ...) {
  if (gBootOptions && !gBootOptions->phys_verbose) {
    return;
  }
  va_list args;
  va_start(args, fmt);
  vprintf(fmt, args);
  va_end(args);
}
