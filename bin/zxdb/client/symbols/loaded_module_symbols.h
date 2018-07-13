// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <string>
#include <vector>

#include "garnet/bin/zxdb/client/symbols/location.h"
#include "garnet/public/lib/fxl/macros.h"

namespace zxdb {

class FileLine;
class LineDetails;
class ModuleSymbols;

// Represents the symbol information for a module that's loaded. Unlike
// ModuleSymbols (which only uses module-relative symbols), all addresses
// consumed and emitted by this class are real addresses in the process.
//
// It might be nice if this derived from ModuleSymbols (since all those
// functions are still valid to be called), but that would cause undesirable
// diamond inheritance in the implementation. So use composition instead.
//
// The duplication of functions in this code vs. ModuleSymbols is unfortunate.
// If we add more functions, it may be worth making all ModuleSymbols functions
// take a load address and avoid the trampoline. But that complicates the
// callers.
class LoadedModuleSymbols {
 public:
  LoadedModuleSymbols();
  virtual ~LoadedModuleSymbols();

  // Returns the ModuleSymbols object (for relative address queries).
  virtual ModuleSymbols* GetModuleSymbols() = 0;

  // Base address for the module.
  virtual uint64_t GetLoadAddress() const = 0;

  // Returns a symbolized Location object for an address in the debugged
  // process' address space. The location will be of type kAddress if there is
  // no symbol for this location.
  virtual Location LocationForAddress(uint64_t address) const = 0;

  // Computes the line that corresponds to the given address. Unlike
  // LocationForAddress (which just returns the current source line), this
  // returns the entire set of contiguous line table entries with code ranges
  // with the same line as the given address.
  virtual LineDetails LineDetailsForAddress(uint64_t address) const = 0;

  // Returns the addresses in the process' address space for the given function
  // name. The function name must be an exact match. The addresses will
  // indicate the start of the function. Since a function implementation can be
  // duplicated more than once, there can be multiple results.
  virtual std::vector<uint64_t> AddressesForFunction(
      const std::string& name) const = 0;

  // See ModuleSymbols::AddressesForLine. This returns absolute addresses.
  virtual std::vector<uint64_t> AddressesForLine(
      const FileLine& line) const = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(LoadedModuleSymbols);
};

}  // namespace zxdb
