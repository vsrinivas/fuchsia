// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_TARGET_SYMBOLS_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_TARGET_SYMBOLS_H_

#include <set>

#include "src/developer/debug/zxdb/symbols/system_symbols.h"

namespace zxdb {

struct InputLocation;
class Location;
struct ResolveOptions;
class SessionSymbolsImpl;

// Symbol interface for a Target. A target may or may not have a process, so this interface does not
// deal with anything related to addresses. See ProcessSymbols for that (which is most of the
// stuff).
//
// We can know about symbols associated with a target even when the process isn't loaded. For
// example, when setting a breakpoint on a symbol we can validate that it's a real symbol.
//
// The current implementation is that all modifications to the list of symbol modules is done by the
// ProcessSymbols which knows the actual symbols of the running program. This provides the minor
// benefit of symbols being available between identical runs of the same binary (useful for setting
// breakpoints).
//
// More useful would be that the symbols could be automatically loaded when we know the binary we'll
// be running, regardless of whether it's been started yet. This requires some system integration
// (how do you find the local binary for something on the target?) and may also depend on how
// typical programs will be started in the debugger (which may evolve).
class TargetSymbols {
 public:
  // This class is copyable and assignable (to support cloning Targets). The SessionSymbolsImpl
  // object must outlive this class.
  explicit TargetSymbols(SystemSymbols* system_symbols);
  TargetSymbols(const TargetSymbols& other);
  ~TargetSymbols();

  TargetSymbols& operator=(const TargetSymbols& other);

  SystemSymbols* system_symbols() { return system_symbols_; }

  // Notifications from ProcessSymbols to keep things in sync. Multiple add notifications are
  // allowed for the same module (this happens when the symbols exist, then the process is started
  // and the module is loaded for real).
  void AddModule(fxl::RefPtr<ModuleSymbols> module);
  void RemoveAllModules();

  // Returns the symbol information for all the modules known for this target.
  std::vector<const ModuleSymbols*> GetModuleSymbols() const;

  // Converts the given InputLocation into one or more locations. The input can match zero, one, or
  // many locations.
  //
  // If symbolize is true, the results will be symbolized, otherwise the output locations will be
  // regular addresses (this will be slightly faster).
  //
  // This function will assert if given a kAddress-based InputLocation since that requires a running
  // process. If you need that, use the variant on ProcessSymbols.
  //
  // Since the modules aren't loaded, there are no load addresses. As a result, all output addresses
  // will be 0. This function's purpose is to expand file/line information for symbols.
  std::vector<Location> ResolveInputLocation(const InputLocation& input_location,
                                             const ResolveOptions& options) const;

  // Gets file matches across all known modules. Unlike ModuleSymbols::FindFileMatches(), it returns
  // a list of pairs of filenames and build directories associated with the filenames.
  std::vector<std::pair<std::string, std::string>> FindFileMatches(std::string_view name) const;

  // Returns the shortest possible name that uniquely identifies the given file in the index.
  //
  // Multiple files with the same name can exist in the different directories. If a name is unique,
  // returns only the name part, otherwise appends directories from the right until the input is
  // disambiguated from all other files of the same name.
  //
  // If the name isn't in the index, returns just the file part.
  std::string GetShortestUniqueFileName(std::string_view file_name) const;

 private:
  // Comparison functor for modules. Does a pointer-identity comparison of the refptr.
  struct ModuleRefComparePtr {
    bool operator()(const fxl::RefPtr<ModuleSymbols>& a, const fxl::RefPtr<ModuleSymbols>& b) const;
  };

  SystemSymbols* const system_symbols_;  // Non-owning.

  // Since there are no addresses, there is no real ordering of these modules. Track them by pointer
  // identity to make keeping in sync with the ProcessSymbols more efficient.
  std::set<fxl::RefPtr<ModuleSymbols>, ModuleRefComparePtr> modules_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_TARGET_SYMBOLS_H_
