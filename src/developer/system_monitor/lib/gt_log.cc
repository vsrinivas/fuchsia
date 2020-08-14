// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/system_monitor/lib/gt_log.h"

namespace gt {

GuiToolsLogLevel g_log_level = GuiToolsLogLevel::INFO;
std::ofstream g_no_output_ofstream;

bool SetUpLogging(int argc, const char* const* argv) {
  int log_level = static_cast<int>(g_log_level);
  int low = static_cast<int>(GuiToolsLogLevel::DEBUG);
  int high = static_cast<int>(GuiToolsLogLevel::FATAL);
  // TODO(fxbug.dev/31): add --help output.
  for (int i = 0; i < argc; ++i) {
    if (strcmp("--quiet", argv[i]) == 0) {
      if (log_level < high) {
        ++log_level;
      }
    } else if (strcmp("--verbose", argv[i]) == 0) {
      if (log_level > low) {
        --log_level;
      }
    }
    g_log_level = static_cast<GuiToolsLogLevel>(log_level);
  }

  // Do not process stream input. Similar to a /dev/null stream, nothing sent
  // here goes anywhere.
  g_no_output_ofstream.setstate(std::ios_base::badbit);
  return true;
}

}  // namespace gt
