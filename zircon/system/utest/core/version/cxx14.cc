// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cxx14.h"

#include <zircon/syscalls.h>

#if __cplusplus >= 201703L
#error "This file's purpose is to be compiled with -std=c++14."
#endif

// zxtest is not API-compatible with C++14, but <zircon/syscalls.h> is.

std::string AssignSystemGetVersionString() {
  std::string s = zx_system_get_version_string();
  return s;
}

std::string ReturnSystemGetVersionString() { return zx_system_get_version_string(); }
