// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/system_monitor/gt_log.h"

namespace gt {

GuiToolsLogLevel g_log_level = INFO;
std::ofstream g_no_output_ofstream;

void SetUpLogging(int argc, const char* argv[]) {
  // Do not process stream input. Similar to a /dev/null stream, nothing sent
  // here goes anywhere.
  g_no_output_ofstream.setstate(std::ios_base::badbit);

  // TODO(sm_bug.com/48): Process |argc| and |argv|.
}

}  // namespace gt
