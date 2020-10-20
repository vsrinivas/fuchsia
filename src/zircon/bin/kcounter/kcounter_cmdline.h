// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_ZIRCON_BIN_KCOUNTER_KCOUNTER_CMDLINE_H_
#define SRC_ZIRCON_BIN_KCOUNTER_KCOUNTER_CMDLINE_H_

#include <stdio.h>

struct KcounterCmdline {
  bool help;
  bool list;
  bool terse;
  bool verbose;
  int period;
  int unparsed_args_start;
  int cpuid;
};

constexpr int kNoCpuIdChosen = -1;

// Prints program usage to |into|, using |myname| for the application name.
void kcounter_usage(const char* myname, FILE* into);

// true if successful with |cmdline| filled out, false otherwise with error printed to |err|.
bool kcounter_parse_cmdline(int argc, const char* const argv[], FILE* err,
                            KcounterCmdline* cmdline);

#endif  // SRC_ZIRCON_BIN_KCOUNTER_KCOUNTER_CMDLINE_H_
