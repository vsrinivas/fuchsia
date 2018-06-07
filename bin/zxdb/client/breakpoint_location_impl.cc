// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/breakpoint_location_impl.h"

#include "garnet/bin/zxdb/client/breakpoint_impl.h"
#include "garnet/bin/zxdb/client/process.h"
#include "garnet/bin/zxdb/client/symbols/process_symbols.h"

namespace zxdb {

BreakpointLocationImpl::BreakpointLocationImpl(BreakpointImpl* bp,
                                               Process* process,
                                               uint64_t address)
    : breakpoint_(bp), process_(process), address_(address) {}

BreakpointLocationImpl::~BreakpointLocationImpl() = default;

Process* BreakpointLocationImpl::GetProcess() const { return process_; }

Location BreakpointLocationImpl::GetLocation() const {
  // This isn't cached because it isn't needed so often and it will take extra
  // work to handle module loads and unloads for the cache.
  return process_->GetSymbols()->LocationForAddress(address_);
}

bool BreakpointLocationImpl::IsEnabled() const { return enabled_; }

void BreakpointLocationImpl::SetEnabled(bool enabled) {
  if (enabled_ == enabled)
    return;

  enabled_ = enabled;
  breakpoint_->DidChangeLocation();
}

}  // namespace zxdb
