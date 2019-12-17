// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/cmdline.h>

#include "priv.h"

bool DebuggingSyscallsEnabled() {
  static bool enabled = gCmdline.GetBool("kernel.enable-debugging-syscalls", false);
  return enabled;
}

bool SerialSyscallsEnabled() {
  static bool enabled = gCmdline.GetBool("kernel.enable-serial-syscalls", false);
  return enabled;
}
