// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <functional>
#include <string>

#include "garnet/bin/zxdb/client/symbols/location.h"
#include "garnet/public/lib/fxl/macros.h"

namespace zxdb {

class LineDetails;
class TargetSymbols;

class ProcessSymbols {
 public:
  struct ModuleStatus {
    // Name of the executable or shared library.
    std::string name;

    // Build ID extracted from file.
    std::string build_id;

    // Load address.
    uint64_t base = 0;

    // True if the symbols were successfully loaded.
    bool symbols_loaded = false;

    // Local file name with the symbols if the symbols were loaded.
    std::string symbol_file;
  };

  ProcessSymbols();
  virtual ~ProcessSymbols();

  virtual TargetSymbols* GetTargetSymbols() = 0;

  // Returns statistics on the currently-loaded modules.
  virtual std::vector<ModuleStatus> GetStatus() const = 0;

  // Attempts to symbolize the given address. If not possible, the returned
  // location will be an address-only location.
  virtual Location LocationForAddress(uint64_t address) const = 0;

  // Computes the line that corresponds to the given address. Unlike
  // LocationForAddress (which just returns the current source line), this
  // returns the entire set of contiguous line table entries with code ranges
  // with the same line as the given address.
  virtual LineDetails LineDetailsForAddress(uint64_t address) const = 0;

  // Returns a vector of addresses correponding to the beginning of the
  // implementation of a given function. Normally this will result in 0 (no
  // match found) or 1 (normal function implementation), but can be more than
  // one if the function is inlined in multiple places.
  virtual std::vector<uint64_t> AddressesForFunction(
      const std::string& name) const = 0;

  // See ModuleSymbols::RelativeAddressesForLine(). This returns absolute
  // addresses for all loaded modules.
  virtual std::vector<uint64_t> AddressesForLine(
      const FileLine& line) const = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(ProcessSymbols);
};

}  // namespace zxdb
