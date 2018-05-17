// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/verbs.h"

#include <inttypes.h>
#include <algorithm>
#include <vector>

#include "garnet/bin/zxdb/client/err.h"
#include "garnet/bin/zxdb/client/process.h"
#include "garnet/bin/zxdb/client/symbols.h"
#include "garnet/bin/zxdb/client/location.h"
#include "garnet/bin/zxdb/client/target.h"
#include "garnet/bin/zxdb/console/command.h"
#include "garnet/bin/zxdb/console/command_utils.h"
#include "garnet/bin/zxdb/console/console.h"
#include "garnet/bin/zxdb/console/output_buffer.h"
#include "garnet/public/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

// sym-info --------------------------------------------------------------------

const char kSymInfoShortHelp[] = "sym-info: Print process symbol information.";
const char kSymInfoHelp[] =
    R"(sym-info

  Prints out the symbol information for the current process.

Example

  sym-info
  process 2 sym-info
)";

Err DoSymInfo(ConsoleContext* context, const Command& cmd) {
  return Err("Unimplemented");
}

// sym-near --------------------------------------------------------------------

const char kSymNearShortHelp[] = "sym-near / sn: Print symbol for an address.";
const char kSymNearHelp[] =
    R"(sym-near <address>

  Alias: "sn"

  Finds the symbol nearest to the given address. This command is useful for
  finding what a pointer or a code location refers to.

Example

  sym-near 0x12345670
  process 2 sym-near 0x612a2519
)";

Err DoSymNear(ConsoleContext* context, const Command& cmd) {
  return Err("Unimplemented");
}

}  // namespace

void AppendSymbolVerbs(std::map<Verb, VerbRecord>* verbs) {
  (*verbs)[Verb::kSymInfo] =
      VerbRecord(&DoSymInfo, {"sym-info"}, kSymInfoShortHelp, kSymInfoHelp);
  (*verbs)[Verb::kSymNear] = VerbRecord(&DoSymNear, {"sym-near", "sn"},
                                        kSymNearShortHelp, kSymNearHelp);
}

}  // namespace zxdb
