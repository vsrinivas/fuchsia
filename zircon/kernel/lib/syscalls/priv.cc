// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "priv.h"

#include <lib/boot-options/boot-options.h>

bool DebuggingSyscallsEnabled() {
  static const bool enabled = gBootOptions->enable_debugging_syscalls;
  return enabled;
}

SerialDebugSyscalls SerialSyscallsEnabled() {
  static const SerialDebugSyscalls serial = gBootOptions->enable_serial_syscalls;
  return serial;
}
