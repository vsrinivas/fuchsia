// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <functional>
#include <string>

#include "garnet/bin/zxdb/symbols/location.h"
#include "garnet/bin/zxdb/symbols/resolve_options.h"
#include "garnet/public/lib/fxl/macros.h"

namespace zxdb {

struct InputLocation;
class LineDetails;
struct ModuleSymbolStatus;
struct ResolveOptions;
class TargetSymbols;

class ProcessSymbols {
 public:
  ProcessSymbols();
  virtual ~ProcessSymbols();

  virtual TargetSymbols* GetTargetSymbols() = 0;

  // Returns statistics on the currently-loaded modules.
  virtual std::vector<ModuleSymbolStatus> GetStatus() const = 0;

  // Converts the given InputLocation into one or more locations. The input
  // can match zero, one, or many locations.
  //
  // If symbolize is true, the results will be symbolized, otherwise the
  // output locations will be regular addresses (this will be slightly faster).
  virtual std::vector<Location> ResolveInputLocation(
      const InputLocation& input_location,
      const ResolveOptions& options = ResolveOptions()) const = 0;

  // Computes the line that corresponds to the given address. Unlike
  // ResolveInputLocation (which just returns the current source line), this
  // returns the entire set of contiguous line table entries with code ranges
  // with the same line as the given address.
  virtual LineDetails LineDetailsForAddress(uint64_t address) const = 0;

  // Returns a vector of addresses correponding to the beginning of the
  // implementation of a given function. Normally this will result in 0 (no
  // match found) or 1 (normal function implementation), but can be more than
  // one if the function is inlined in multiple places.
  virtual std::vector<uint64_t> AddressesForFunction(
      const std::string& name) const = 0;

  // Returns true if the code location is inside a module where there are
  // symbols loaded. If we did something like index ELF exports, those wouldn't
  // count. "Symbols loaded" here means there is real DWARF debugging
  // information available.
  virtual bool HaveSymbolsLoadedForModuleAt(uint64_t address) const = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(ProcessSymbols);
};

}  // namespace zxdb
