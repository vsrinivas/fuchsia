// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <string>
#include <vector>

#include "src/developer/debug/zxdb/symbols/location.h"
#include "src/developer/debug/zxdb/symbols/module_symbol_index_node.h"
#include "src/developer/debug/zxdb/symbols/module_symbol_status.h"
#include "src/developer/debug/zxdb/symbols/resolve_options.h"
#include "src/lib/fxl/macros.h"

namespace zxdb {

class FileLine;
struct InputLocation;
class LineDetails;
class ModuleSymbolIndex;
struct ResolveOptions;
class SymbolContext;

// Represents the symbols for a module (executable or shared library).
//
// All addresses in and out of the API of this class are absolute inside a
// running process. Since this class itself is independent of load addresses,
// the functions take a SymbolContext which is used to convert between the
// absolute addresses use as inputs and outputs, and the module-relative
// addresses used by the symbol tables.
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

  // Converts the given InputLocation into one or more locations. Called by
  // LoadedModuleSymbols. See there for more info.
  virtual std::vector<Location> ResolveInputLocation(
      const SymbolContext& symbol_context, const InputLocation& input_location,
      const ResolveOptions& options = ResolveOptions()) const = 0;

  // Computes the line that corresponds to the given address. Unlike
  // ResolveInputLocation (which just returns the current source line), this
  // returns the entire set of contiguous line table entries with code ranges
  // with the same line as the given address.
  //
  // This function may return a 0 line number for code that does not have
  // an associated line (could be bookkeeping by compiler). These can't
  // be merged with the previous or next line since it could be misleading:
  // say the bookkeeping code was at the end of an if block, the previous line
  // would be inside the block, but that block may not have been executed and
  // showing the location there would be misleading. Generally code blocks
  // with a "0" line number should be skipped over.
  //
  // The SymbolContext will be used to interpret the absolute input address.
  virtual LineDetails LineDetailsForAddress(
      const SymbolContext& symbol_context, uint64_t absolute_address) const = 0;

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

  // Returns the symbol index for this module.
  virtual const ModuleSymbolIndex& GetIndex() const = 0;

  // Converts the given DieRef from the symbol index to an actual Symbol
  // object for reading.
  virtual LazySymbol IndexDieRefToSymbol(
      const ModuleSymbolIndexNode::DieRef&) const = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(ModuleSymbols);
};

}  // namespace zxdb
