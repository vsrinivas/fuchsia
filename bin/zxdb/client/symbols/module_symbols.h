// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <string>
#include <vector>

#include "garnet/bin/zxdb/client/symbols/location.h"
#include "garnet/bin/zxdb/client/symbols/module_symbol_status.h"
#include "garnet/public/lib/fxl/macros.h"

namespace zxdb {

class FileLine;
class LineDetails;

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

  // Returns information about this module. This is relatively slow because it
  // needs to count the index size.
  //
  // The name will be empty (local_file_name will be the symbol file) since
  // the name is the external name in the system that this class doesn't know
  // about. The base address will be 0 because this class doesn't know what the
  // base address is.
  virtual ModuleSymbolStatus GetStatus() const = 0;

  // Returns a symbolized Location object for the given module-relative
  // location. The address in the returned location object will also be
  // module-relative. The location will be of type kAddress if there is no
  // symbol for this location.
  virtual Location RelativeLocationForRelativeAddress(
      uint64_t address) const = 0;

  // Computes the line that corresponds to the given address. Unlike
  // RelativeLocationForRelativeAddress (which just returns the current source
  // line), this returns the entire set of contiguous line table entries with
  // code ranges with the same line as the given address.
  virtual LineDetails LineDetailsForRelativeAddress(uint64_t address) const = 0;

  // Returns the addresses (relative to the base of this module) for the given
  // function name. The function name must be an exact match. The addresses
  // will indicate the start of the function. Since a function implementation
  // can be duplicated more than once, there can be multiple results.
  virtual std::vector<uint64_t> RelativeAddressesForFunction(
      const std::string& name) const = 0;

  // Returns a vector of full file names that match the input.
  //
  // The name is matched from the right side with a left boundary of either a
  // slash or the beginning of the full path. This may match more than one file
  // name, and the caller is left to decide which one(s) it wants.
  //
  // In the future we may want to return an object representing the compilation
  // unit for each of the files.
  virtual std::vector<std::string> FindFileMatches(
      const std::string& name) const = 0;

  // Finds the addresses for all instantiations of the given line. Often there
  // will be one result, but inlining and templates could duplicate the code.
  //
  // It may not be possible to return the exact line. The line could have been
  // optimized out, it could have been a continuation of an earlier line, or
  // there could be no code at that line in the first place. This function will
  // try its best to find the best line if an exact match isn't possible.
  //
  // If you need to find out the exact actual location that this resolved to,
  // look up the restulting address again.
  //
  // If the file wasn't found or contains no code, it will return an empty
  // vector. If the file exists and contains code, it will always return
  // *something*.
  //
  // The input file name must be a full path that matches exactly. Use
  // FindFileMatches() to get these.
  virtual std::vector<uint64_t> RelativeAddressesForLine(
      const FileLine& line) const = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(ModuleSymbols);
};

}  // namespace zxdb
