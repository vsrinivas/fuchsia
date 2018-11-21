// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <functional>
#include <string>

#include "garnet/bin/zxdb/symbols/location.h"
#include "garnet/bin/zxdb/symbols/resolve_options.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace zxdb {

struct InputLocation;
class LineDetails;
class LoadedModuleSymbols;
struct ModuleSymbolStatus;
struct ResolveOptions;
class TargetSymbols;

class ProcessSymbols {
 public:
  ProcessSymbols();
  virtual ~ProcessSymbols();

  virtual fxl::WeakPtr<const ProcessSymbols> GetWeakPtr() const = 0;

  virtual TargetSymbols* GetTargetSymbols() = 0;

  // Returns statistics on the currently-loaded modules.
  virtual std::vector<ModuleSymbolStatus> GetStatus() const = 0;

  // Returns the information for all the modules that were loaded with
  // symbol information.
  virtual std::vector<const LoadedModuleSymbols*> GetLoadedModuleSymbols()
      const = 0;

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

  // Returns true if the code location is inside a module where there are
  // symbols loaded. If we did something like index ELF exports, those wouldn't
  // count. "Symbols loaded" here means there is real DWARF debugging
  // information available.
  virtual bool HaveSymbolsLoadedForModuleAt(uint64_t address) const = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(ProcessSymbols);
};

}  // namespace zxdb
