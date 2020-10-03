// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_MODULE_SYMBOLS_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_MODULE_SYMBOLS_H_

#include <stdint.h>

#include <ctime>
#include <set>
#include <string>
#include <vector>

#include "lib/fit/function.h"
#include "src/developer/debug/zxdb/symbols/index_node.h"
#include "src/developer/debug/zxdb/symbols/location.h"
#include "src/developer/debug/zxdb/symbols/module_symbol_status.h"
#include "src/developer/debug/zxdb/symbols/resolve_options.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/ref_counted.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace zxdb {

class DwarfUnit;
class FileLine;
class Function;
struct InputLocation;
class LineDetails;
class Index;
struct ResolveOptions;
class SymbolContext;

// Represents the symbols for a module (executable or shared library).
//
// All addresses in and out of the API of this class are absolute inside a running process. Since
// this class itself is independent of load addresses, the functions take a SymbolContext which is
// used to convert between the absolute addresses use as inputs and outputs, and the module-relative
// addresses used by the symbol tables. The SymbolContext comes from the LoadedModuleSymbols which
// is owned by the ProcessSymbols object (which knows about the load addresses for each of its
// modules).
//
// This class is reference counted because symbols have a backpointer to the module they come from
// via the CompileUnit.
class ModuleSymbols : public fxl::RefCountedThreadSafe<ModuleSymbols> {
 public:
  fxl::WeakPtr<ModuleSymbols> GetWeakPtr();

  // Returns information about this module. This is relatively slow because it needs to count the
  // index size.
  //
  // The name will be empty (local_file_name will be the symbol file) since the name is the external
  // name in the system that this class doesn't know about. The base address will be 0 because this
  // class doesn't know what the
  // base address is.
  virtual ModuleSymbolStatus GetStatus() const = 0;

  // Returns the last modification time of the symbols for this module. Will be 0 if it is not
  // known.
  virtual std::time_t GetModificationTime() const = 0;

  // Returns the build directory associated with this module, useful for source file lookup.
  virtual std::string GetBuildDir() const = 0;

  // Returns the extent of the mapped segments in memory.
  //
  // This may return 0 on error or in testing which indicates the mapped length is unknown.
  virtual uint64_t GetMappedLength() const = 0;

  // Converts the given InputLocation into one or more locations. Called by LoadedModuleSymbols. See
  // there for more info.
  virtual std::vector<Location> ResolveInputLocation(
      const SymbolContext& symbol_context, const InputLocation& input_location,
      const ResolveOptions& options = ResolveOptions()) const = 0;

  // Returns the low-level DwarfUnit that covers the given address, or null if no match.
  virtual fxl::RefPtr<DwarfUnit> GetDwarfUnit(const SymbolContext& symbol_context,
                                              uint64_t absolute_address) const = 0;

  // Computes the line that corresponds to the given address. Unlike ResolveInputLocation (which
  // just returns the current source line), this returns the entire set of contiguous line table
  // entries with code ranges with the same line as the given address.
  //
  // This function may return a 0 line number for code that does not have an associated line (could
  // be bookkeeping by compiler). These can't be merged with the previous or next line since it
  // could be misleading: say the bookkeeping code was at the end of an if block, the previous line
  // would be inside the block, but that block may not have been executed and showing the location
  // there would be misleading. Generally code blocks with a "0" line number should be skipped over.
  //
  // The |greedy| flag, if set, will cause a nonzero line to be extended over zero lines. This is
  // for cases where the caller wants the broadest reasonable definition of the current line.
  // Callers will want this especially if they don't handle 0 lines themselves because you can get
  // a sequence like: [line 45, line 0, line 45] and from a user perspective the right behavior is
  // to consider the whole thing "line 45".
  //
  // The SymbolContext will be used to interpret the absolute input address.
  virtual LineDetails LineDetailsForAddress(const SymbolContext& symbol_context,
                                            uint64_t absolute_address,
                                            bool greedy = false) const = 0;

  // Returns a vector of full file names that match the input.
  //
  // The name is matched from the right side with a left boundary of either a slash or the beginning
  // of the full path. This may match more than one file name, and the caller is left to decide
  // which one(s) it wants.
  //
  // In the future we may want to return an object representing the compilation unit for each of the
  // files.
  virtual std::vector<std::string> FindFileMatches(std::string_view name) const = 0;

  // Returns the functions marked with DW_AT_main_subprogram in this module.
  //
  // As of this writing, Clang doesn't mark the main function so this will be empty, but Rust does.
  // It's theoretically possible for the compiler to mark more than one main function, but it's not
  // obvious what that might mean.
  virtual std::vector<fxl::RefPtr<Function>> GetMainFunctions() const = 0;

  // Returns the symbol index for this module.
  virtual const Index& GetIndex() const = 0;

  // Converts the given SymbolRef from the symbol index to an actual Symbol object for reading.
  virtual LazySymbol IndexSymbolRefToSymbol(const IndexNode::SymbolRef&) const = 0;

  // Return whether this module has been given the opportunity to include symbols from the binary
  // itself, such as PLT entries.
  virtual bool HasBinary() const = 0;

  // The constructor takes an optional callback that will be executed when this class is destroyed.
  // This allows the SystemSymbols to keep track of all live ModuleSymbols for caching purposes.
  void set_deletion_cb(fit::callback<void(ModuleSymbols*)> cb) { deletion_cb_ = std::move(cb); }

  // The set of files that the frontend has warned the user about being newer than the symbol
  // file. This prevents duplicate warnings for each file.
  //
  // This set is stored here but not interpreted, it is here for the frontend's use only. If we
  // find more data like this, we should have a more generic way to associate frontend data with
  // client objects.
  const std::set<std::string>& newer_files_warned() const { return newer_files_warned_; }
  std::set<std::string>& newer_files_warned() { return newer_files_warned_; }

 protected:
  FRIEND_REF_COUNTED_THREAD_SAFE(ModuleSymbols);
  FRIEND_MAKE_REF_COUNTED(ModuleSymbols);

  explicit ModuleSymbols();
  virtual ~ModuleSymbols();

 private:
  fit::callback<void(ModuleSymbols*)> deletion_cb_;  // Possibly null.

  std::set<std::string> newer_files_warned_;  // See getter above.

  fxl::WeakPtrFactory<ModuleSymbols> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ModuleSymbols);
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_MODULE_SYMBOLS_H_
