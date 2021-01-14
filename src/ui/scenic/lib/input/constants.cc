// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/input/constants.h"

#include <cstdlib>
#include <string>

#include "src/lib/files/file.h"

namespace scenic_impl::input {

uint32_t ChattyMax() {
  static bool first_run = true;
  static uint32_t chatty_max = 0;  // fallback value is quiet and friendly
  if (first_run) {
    first_run = false;  // don't try file again
    if (std::string str; files::ReadFileToString("/config/data/chatty_max", &str)) {
      if (int file_val = atoi(str.c_str()); file_val >= 0) {
        chatty_max = file_val;
      }
    }
  }
  return chatty_max;
}

}  // namespace scenic_impl::input
