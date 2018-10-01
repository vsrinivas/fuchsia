// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>
#include <vector>

#include "garnet/bin/zxdb/symbols/location.h"
#include "garnet/public/lib/fxl/macros.h"

namespace zxdb {

class FileLine;
struct InputLocation;
struct ResolveOptions;

// Symbol interface for a Target. A target may or may not have a process, so
// this interface does not deal with anything related to addresses. See
// ProcessSymbols for that (which is most of the stuff).
//
// We can know about symbols associated with a target even when the process
// isn't loaded. For example, when setting a breakpoint on a symbol we can
// validate that it's a real symbol.
class TargetSymbols {
 public:
  TargetSymbols();
  virtual ~TargetSymbols();

  // Converts the given InputLocation into one or more locations. The input
  // can match zero, one, or many locations.
  //
  // If symbolize is true, the results will be symbolized, otherwise the
  // output locations will be regular addresses (this will be slightly faster).
  //
  // This function will assert if given a kAddress-based InputLocation since
  // that requires a running process. If you need that, use the variant on
  // ProcessSymbols.
  //
  // Since the modules aren't loaded, there are no load addresses. As a result,
  // all output addresses will be 0. This function's purpose is to expand
  // file/line information for symbols.
  virtual std::vector<Location> ResolveInputLocation(
      const InputLocation& input_location,
      const ResolveOptions& options) const = 0;

  // Gets file matches across all known modules. See
  // ModuleSymbols::FindFileMatches().
  virtual std::vector<std::string> FindFileMatches(
      const std::string& name) const = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(TargetSymbols);
};

}  // namespace zxdb
