// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "priv.h"

#include <lib/cmdline.h>

bool DebuggingSyscallsEnabled() {
  static bool enabled = gCmdline.GetBool("kernel.enable-debugging-syscalls", false);
  return enabled;
}

SerialState SerialSyscallsEnabled() {
  static const char* serial = []() {
    return gCmdline.GetString("kernel.enable-serial-syscalls");
  }();

  if (serial == nullptr) {
    return SerialState::kDisabled;
  }

  if (strcmp(serial, "true") == 0) {
    return SerialState::kEnabled;
  }

  if (strcmp(serial, "output-only") == 0) {
    return SerialState::kOutputOnly;
  }

  return SerialState::kDisabled;
}
