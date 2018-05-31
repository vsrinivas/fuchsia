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

// Represents the symbols for a module (executable or shared library).
//
// All addresses in and out of the API of this class are module-relative (hence
// the "Relative*" naming). This way, the symbol information can be shared
// between multiple processes that have mapped the same .so file (often at
// different addresses). This means that callers have to offset addresses when
// calling into this class, and offset them in the opposite way when they get
// the results.
class ModuleSymbols {
 public:
  ModuleSymbols();
  virtual ~ModuleSymbols();

  // Path name to the local symbol file.
  virtual const std::string& GetLocalFileName() const = 0;

  // Returns a symbolized Location object for the given module-relative
  // location. The address in the returned location object will also be
  // module-relative. The location will be of type kAddress if there is no
  // symbol for this location.
  virtual Location RelativeLocationForRelativeAddress(
      uint64_t address) const = 0;

  // Returns the addresses (relative to the base of this module) for the given
  // function name. The function name must be an exact match. The addresses
  // will indicate the start of the function. Since a function implementation
  // can be duplicated more than once, there can be multiple results.
  virtual std::vector<uint64_t> RelativeAddressesForFunction(
      const std::string& name) const = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(ModuleSymbols);
};

}  // namespace zxdb
