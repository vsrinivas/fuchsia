// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_PRINT_COMMAND_UTILS_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_PRINT_COMMAND_UTILS_H_

#include "src/developer/debug/zxdb/common/err_or.h"
#include "src/developer/debug/zxdb/console/format_node_console.h"

namespace zxdb {

// This file provides shared infrastructure for commands that print ExprValues.

class Command;
struct ConsoleFormatOptions;
struct VerbRecord;

// Appends the formatting switches used by GetPrintCommandFormatOptions. These switch values start
// at 1,000,000 so they shouldn't collide with other switch integers.
//
// Commands using this function to populate their VerbRecord should include the below
// PRINT_COMMAND_SWITCH_HELP in their help.
void AppendPrintCommandSwitches(VerbRecord* record);

// Populates the formatting options with the given command's switches.
ErrOr<ConsoleFormatOptions> GetPrintCommandFormatOptions(const Command& cmd);

// Documentation for the switches appended by AppendPrintCommandSwitches().
#define PRINT_COMMAND_SWITCH_HELP                                             \
  "  --max-array=<number>\n"                                                  \
  "      Specifies the maximum array size to print. By default this is\n"     \
  "      256. Specifying large values will slow things down and make the\n"   \
  "      output harder to read, but the default is sometimes insufficient.\n" \
  "      This also applies to strings.\n"                                     \
  "\n"                                                                        \
  "  -r\n"                                                                    \
  "  --raw\n"                                                                 \
  "      Bypass pretty-printers and show the raw type information.\n"         \
  "\n"                                                                        \
  "  -t\n"                                                                    \
  "  --types\n"                                                               \
  "      Force type printing on. The type of every value printed will be\n"   \
  "      explicitly shown. Implies -v.\n"                                     \
  "\n"                                                                        \
  "  -v\n"                                                                    \
  "  --verbose\n"                                                             \
  "      Don't elide type names. Show reference addresses and pointer\n"      \
  "      types.\n"                                                            \
  "\n"                                                                        \
  "Number formatting options\n"                                               \
  "\n"                                                                        \
  "  Force numeric values to be of specific types with these options:\n"      \
  "\n"                                                                        \
  "  -c  Character\n"                                                         \
  "  -d  Signed decimal\n"                                                    \
  "  -u  Unsigned decimal\n"                                                  \
  "  -x  Unsigned hexadecimal\n"

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_PRINT_COMMAND_UTILS_H_
