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
  // Adds a mock mapping from the given name/address to the list of locations. The locations are
  // returned exactly as given so should be given in absolute addresses.
  //
  // The identifier names here should be non-globally qualified (no leading "::"). This is because
  // when the list is queried against, input Identifiers will be looked up as normal and
  // non-globally qualified (if different). So to match everything, the underlying data needs to be
  // non-qualified.
  //
  // The String variant does simple parsing based on "::" and is not template-aware. So anything
  // complex should use the Identifier variant.
  //
  // The address variant takes absolute addresses. We don't do anything with ranges to find the
  // previous symbol, they should match exactly.
  void AddSymbolLocations(const Identifier& name, std::vector<Location> locs);
  void AddSymbolLocations(const std::string& name, std::vector<Location> locs);
  void AddSymbolLocations(uint64_t address, std::vector<Location> locs);

  // Adds a mock mapping from address to line details. This matches an exact address only, not a
  // range.
  void AddLineDetails(uint64_t absolute_address, LineDetails details);

  // Injects a response to IndexSymbolRefToSymbol for resolving symbols from the index. See index()
  // getter.
  void AddSymbolRef(const IndexNode::SymbolRef& die, fxl::RefPtr<Symbol> symbol);

  // Adds a name to the list of files considered for FindFileMatches().
  void AddFileName(const std::string& file_name);

  // Provides writable access to the index for tests to insert data. To hook up symbols, add them to
  // the index and call AddSymbolRef() with the same SymbolRef and the symbol you want it to resolve
  // to.
  Index& index() { return index_; }

  void set_modification_time(std::size_t mtime) { modification_time_ = mtime; }
  void set_mapped_length(uint64_t len) { mapped_length_ = len; }

  // ModuleSymbols implementation.
  ModuleSymbolStatus GetStatus() const override;
  std::time_t GetModificationTime() const override { return modification_time_; }
  std::string GetBuildDir() const override { return ""; }
  uint64_t GetMappedLength() const override { return mapped_length_; }
  std::vector<Location> ResolveInputLocation(const SymbolContext& symbol_context,
                                             const InputLocation& input_location,
                                             const ResolveOptions& options) const override;
  fxl::RefPtr<DwarfUnit> GetDwarfUnit(const SymbolContext& symbol_context,
                                      uint64_t absolute_address) const override;
  LineDetails LineDetailsForAddress(const SymbolContext& symbol_context, uint64_t address,
                                    bool greedy) const override;
  std::vector<std::string> FindFileMatches(std::string_view name) const override;
  std::vector<fxl::RefPtr<Function>> GetMainFunctions() const override;
  const Index& GetIndex() const override;
  LazySymbol IndexSymbolRefToSymbol(const IndexNode::SymbolRef&) const override;
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
  uint64_t mapped_length_ = 0;

  // Maps manually-added symbols to their locations.
  std::map<Identifier, std::vector<Location>> named_symbols_;
  std::map<uint64_t, std::vector<Location>> addr_symbols_;

  // Maps manually-added addresses to line details.
  std::map<uint64_t, LineDetails> lines_;

  // Maps manually-aded SymbolRefs offsets to symbols.
  std::map<uint32_t, fxl::RefPtr<Symbol>> die_refs_;

  std::vector<std::string> files_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_MOCK_MODULE_SYMBOLS_H_
