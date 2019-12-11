// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_MOCK_MODULE_SYMBOLS_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_MOCK_MODULE_SYMBOLS_H_

#include <map>

#include "src/developer/debug/zxdb/symbols/index.h"
#include "src/developer/debug/zxdb/symbols/module_symbols.h"
#include "src/developer/debug/zxdb/symbols/symbol.h"

namespace zxdb {

// A mock for symbol lookup.
class MockModuleSymbols : public ModuleSymbols {
 public:
  // Adds a mock mapping from the given name/address to the list of locations. The addresses are
  // mapped exactly to queries, we don't do anything fancy with ranges.
  //
  // The input name will be matched against both the globally qualified (including leading "::" if
  // present) and unqualified names which matches the implementation behavior of ignoring the
  // qualification when doing an index search. As a result, the input name should not have a
  // leading "::".
  void AddSymbolLocations(const std::string& name, std::vector<Location> locs);
  void AddSymbolLocations(uint64_t address, std::vector<Location> locs);

  // Adds a mock mapping from address to line details. This matches an exact address only, not a
  // range.
  void AddLineDetails(uint64_t absolute_address, LineDetails details);

  // Injects a response to IndexDieRefToSymbol for resolving symbols from the index. See index()
  // getter.
  void AddDieRef(const IndexNode::DieRef& die, fxl::RefPtr<Symbol> symbol);

  // Adds a name to the list of files considered for FindFileMatches().
  void AddFileName(const std::string& file_name);

  // Provides writable access to the index for tests to insert data. To hook up symbols, add them to
  // the index and call AddDieRef() with the same DieRef and the symbol you want it to resolve to.
  Index& index() { return index_; }

  void set_modification_time(std::size_t mtime) { modification_time_ = mtime; }

  // ModuleSymbols implementation.
  ModuleSymbolStatus GetStatus() const override;
  std::time_t GetModificationTime() const override { return modification_time_; }
  std::vector<Location> ResolveInputLocation(const SymbolContext& symbol_context,
                                             const InputLocation& input_location,
                                             const ResolveOptions& options) const override;
  LineDetails LineDetailsForAddress(const SymbolContext& symbol_context, uint64_t address,
                                    bool greedy) const override;
  std::vector<std::string> FindFileMatches(std::string_view name) const override;
  std::vector<fxl::RefPtr<Function>> GetMainFunctions() const override;
  const Index& GetIndex() const override;
  LazySymbol IndexDieRefToSymbol(const IndexNode::DieRef&) const override;
  bool HasBinary() const override;

 protected:
  // This class is derived from so these are protected.
  FRIEND_MAKE_REF_COUNTED(MockModuleSymbols);
  FRIEND_REF_COUNTED_THREAD_SAFE(MockModuleSymbols);

  explicit MockModuleSymbols(const std::string& local_file_name);
  ~MockModuleSymbols() override;

 private:
  Index index_;

  std::string local_file_name_;
  std::size_t modification_time_ = 0;

  // Maps manually-added symbols to their locations.
  std::map<std::string, std::vector<Location>> named_symbols_;
  std::map<uint64_t, std::vector<Location>> addr_symbols_;

  // Maps manually-added addresses to line details.
  std::map<uint64_t, LineDetails> lines_;

  // Maps manually-aded DieRefs offsets to symbols.
  std::map<uint32_t, fxl::RefPtr<Symbol>> die_refs_;

  std::vector<std::string> files_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_MOCK_MODULE_SYMBOLS_H_
