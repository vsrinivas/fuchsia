// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_SUBSTATEMENT_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_SUBSTATEMENT_H_

#include <optional>
#include <vector>

#include "lib/fit/function.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/symbols/arch.h"
#include "src/developer/debug/zxdb/symbols/function.h"

namespace zxdb {

class ArchInfo;
class Function;
class Err;
class Location;
class MemoryDump;
class Process;
class ProcessSymbols;

struct SubstatementCall {
  // Address of the call instruction.
  TargetPointer call_addr;

  // Destination of the call if known. Will be the same as call_addr for inlines. This will be
  // nullopt for indirect call instructions.
  std::optional<TargetPointer> call_dest;

  // Set for inline calls. Null for real function calls.
  fxl::RefPtr<Function> inline_call;

  // Calls are sorted by their call address.
  bool operator<(const SubstatementCall& other) const { return call_addr < other.call_addr; }
};

// Extracts all calls present in the line of code on the given address. Calls both before and after
// the address will be considered, as long as it is within the contiguous range of addresses
// covering that line. Other address ranges corresponding to the same line will not be considered.
//
// A symbolized location should be provided. This function will be used to compute inline calls.
// This must be passed in because the location in the inline call chain could be ambiguous (see the
// Stack object for more about ambiguous inlines).
//
// This function needs to fetch memory so must be asynchronous. The Err in the callback will be set
// for transport errors. If there's no symbol information it will not be considered an error.
// Rather, the result vector will be empty.
void GetSubstatementCallsForLine(Process* process, const Location& loc,
                                 fit::function<void(const Err&, std::vector<SubstatementCall>)> cb);

// Extracts all physical function calls (not inlines) for the given memory region in the given
// ranges list. This assumes the memory region starts at an instruction boundary. The ranges list
// can contain many entries and can be discontiguous as long as the memory dump covers them.
std::vector<SubstatementCall> GetSubstatementCallsForMemory(const ArchInfo* arch_info,
                                                            const ProcessSymbols* symbols,
                                                            const Location& loc,
                                                            const AddressRanges& ranges,
                                                            const MemoryDump& mem);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_SUBSTATEMENT_H_
