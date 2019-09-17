// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unwindstack/Log.h>

#include "src/lib/fxl/strings/string_printf.h"

// This file contains an implementation of the Android Log.cpp file which
// only logs to stdout. This keeps us from needing to fork that file.

namespace unwindstack {

static bool g_print_to_stdout = true;

void log_to_stdout(bool enable) {
  g_print_to_stdout = enable;
}

// Send the data to the log.
void log(uint8_t indent, const char* format, ...) {
  std::string real_format;
  if (indent > 0) {
    real_format = fxl::StringPrintf("%*s%s", 2 * indent, " ", format);
  } else {
    real_format = format;
  }
  va_list args;
  va_start(args, format);
  if (g_print_to_stdout) {
    real_format += '\n';
    vprintf(real_format.c_str(), args);
  }
  va_end(args);
}

}  // namespace unwindstack
