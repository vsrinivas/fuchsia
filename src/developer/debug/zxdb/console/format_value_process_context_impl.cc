// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/format_value_process_context_impl.h"

#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/target.h"
#include "src/developer/debug/zxdb/symbols/input_location.h"
#include "src/developer/debug/zxdb/symbols/process_symbols.h"

namespace zxdb {

FormatValueProcessContextImpl::FormatValueProcessContextImpl(Target* target) {
  if (Process* process = target->GetProcess())
    weak_process_ = process->GetWeakPtr();
}
FormatValueProcessContextImpl::FormatValueProcessContextImpl(Process* process)
    : weak_process_(process->GetWeakPtr()) {}
FormatValueProcessContextImpl::~FormatValueProcessContextImpl() = default;

Location FormatValueProcessContextImpl::GetLocationForAddress(
    uint64_t address) const {
  if (!weak_process_)
    return Location(Location::State::kAddress, address);  // Can't symbolize.

  auto locations =
      weak_process_->GetSymbols()->ResolveInputLocation(InputLocation(address));

  // Given an exact address, ResolveInputLocation() should only return one
  // result.
  FXL_DCHECK(locations.size() == 1u);
  return locations[0];
}

}  // namespace zxdb
