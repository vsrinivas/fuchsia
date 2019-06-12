// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/system_monitor/gt_log.h"

namespace gt {

GuiToolsLogLevel g_log_level = INFO;
std::ofstream g_no_output_ofstream;

bool SetUpLogging(int argc, const char* const* argv) {
  int log_level = g_log_level;
  // TODO(smbug.com/31): add --help output.
  for (int i = 0; i < argc; ++i) {
    if (strcmp("--quiet", argv[i]) == 0) {
      // It's okay if this goes outside of the enum range.
      ++log_level;
    }
    else if (strcmp("--verbose", argv[i]) == 0) {
      // It's okay if this goes outside of the enum range.
      --log_level;
    }
    g_log_level = static_cast<GuiToolsLogLevel>(log_level);
  }

  // Do not process stream input. Similar to a /dev/null stream, nothing sent
  // here goes anywhere.
  g_no_output_ofstream.setstate(std::ios_base::badbit);
  return true;
}

}  // namespace gt
