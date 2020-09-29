// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_MODULE_SYMBOLS_IMPL_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_MODULE_SYMBOLS_IMPL_H_

#include <map>

#include "gtest/gtest_prod.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/DebugInfo/DWARF/DWARFUnit.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/symbols/elf_symbol_record.h"
#include "src/developer/debug/zxdb/symbols/index.h"
#include "src/developer/debug/zxdb/symbols/location.h"
#include "src/developer/debug/zxdb/symbols/module_symbols.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace llvm {

class DWARFCompileUnit;
class DWARFContext;
class MemoryBuffer;

namespace object {
class Binary;
}  // namespace object

}  // namespace llvm

namespace zxdb {

class DwarfBinary;
class DwarfSymbolFactory;
class Variable;

// Represents the symbol interface for a module (executable or shared library). See ModuleSymbols.
//
// This provides a high-level interface on top of the DwarfBinary file (low-level stuff), the Index,
// and the SymbolFactory.
class ModuleSymbolsImpl : public ModuleSymbols {
 public:
  DwarfBinary* binary() { return binary_.get(); }
  DwarfSymbolFactory* symbol_factory() { return symbol_factory_.get(); }

  fxl::WeakPtr<ModuleSymbolsImpl> GetWeakPtr();

  // ModuleSymbols implementation.
  ModuleSymbolStatus GetStatus() const override;
  std::time_t GetModificationTime() const override;
  std::string GetBuildDir() const override;
  std::vector<Location> ResolveInputLocation(
      const SymbolContext& symbol_context, const InputLocation& input_location,
      const ResolveOptions& options = ResolveOptions()) const override;
  fxl::RefPtr<DwarfUnit> GetDwarfUnit(const SymbolContext& symbol_context,
                                      uint64_t absolute_address) const override;
  LineDetails LineDetailsForAddress(const SymbolContext& symbol_context, uint64_t absolute_address,
                                    bool greedy) const override;
  std::vector<std::string> FindFileMatches(std::string_view name) const override;
  std::vector<fxl::RefPtr<Function>> GetMainFunctions() const override;
  const Index& GetIndex() const override;
  LazySymbol IndexSymbolRefToSymbol(const IndexNode::SymbolRef&) const override;
  bool HasBinary() const override;

 private:
  FRIEND_MAKE_REF_COUNTED(ModuleSymbolsImpl);
  FRIEND_REF_COUNTED_THREAD_SAFE(ModuleSymbolsImpl);
  FRIEND_TEST(ModuleSymbols, ResolveMainFunction);

  // The input binary must be successfully initialized already.
  //
  // The build_dir, if not empty, will be used to override the compilation_dir of FileLine objects,
  // which is useful because in Fuchsia checkout, the compilation_dir will always be ".".
  //
  // If create_index is true, an index will be created for fast symbol lookup.
  // Normal callers will always want to create the index, unless you don't need to query a symbol
  // from its name, e.g., in some test scenarios or in symbolizer.
  explicit ModuleSymbolsImpl(std::unique_ptr<DwarfBinary> binary, const std::string& build_dir,
                             bool create_index = true);
  ~ModuleSymbolsImpl() override;

  // Helpers for ResolveInputLocation() for the different types of inputs.
  std::vector<Location> ResolveLineInputLocation(const SymbolContext& symbol_context,
                                                 const InputLocation& input_location,
                                                 const ResolveOptions& options) const;
  std::vector<Location> ResolveSymbolInputLocation(const SymbolContext& symbol_context,
                                                   const InputLocation& input_location,
                                                   const ResolveOptions& options) const;
  std::vector<Location> ResolveAddressInputLocation(const SymbolContext& symbol_context,
                                                    const InputLocation& input_location,
                                                    const ResolveOptions& options) const;

  // Symbolizes the given address if possible. The function can be specified if it's already known
  // (some code paths have to compute that before calling). Otherwise, the address will be looked up
  // and the function computed if necessary.
  //
  // The DWARF and ELF versions can fail to match a symbol for that category. The general version
  // will try both and will return a raw address if nothing matches.
  Location LocationForAddress(const SymbolContext& symbol_context, uint64_t absolute_address,
                              const ResolveOptions& options, const Function* optional_func) const;
  std::optional<Location> DwarfLocationForAddress(const SymbolContext& symbol_context,
                                                  uint64_t absolute_address,
                                                  const ResolveOptions& options,
                                                  const Function* optional_func) const;
  std::optional<Location> ElfLocationForAddress(const SymbolContext& symbol_context,
                                                uint64_t absolute_address,
                                                const ResolveOptions& options) const;

  // Converts the given global or static variable to a Location. This doesn't work for local
  // variables which are dynamic and based on the current CPU state and stack.
  Location LocationForVariable(const SymbolContext& symbol_context,
                               fxl::RefPtr<Variable> variable) const;

  // Converts a Function object to a found location according to the options and adds it to the
  // list. May append nothing if there is no code for the function.
  void AppendLocationForFunction(const SymbolContext& symbol_context, const ResolveOptions& options,
                                 const Function* func, std::vector<Location>* result) const;

  // Looks up an ELF or PLT symbol. The symbol name only matches the mangled names and must not
  // include the "$elf(...)" or "$plt(...)" annotation. Returns a vector of one entry if found, an
  // empty vector if not.
  std::vector<Location> ResolvePltName(const SymbolContext& symbol_context,
                                       const std::string& mangled_name) const;
  std::vector<Location> ResolveElfName(const SymbolContext& symbol_context,
                                       const std::string& mangled_name) const;

  // Resolves the line number information for the given file, which must be an exact match. This is
  // a helper function for ResolveLineInputLocation().
  //
  // This appends to the given output.
  void ResolveLineInputLocationForFile(const SymbolContext& symbol_context,
                                       const std::string& canonical_file, int line_number,
                                       const ResolveOptions& options,
                                       std::vector<Location>* output) const;

  // Constructs an ElfSymbol object for the given record.
  //
  // Lookup for an address wants to return a Location identifying that exact location, even if
  // the address in question isn't at the beginning of the symbol. Such callers can specify the
  // relative address that the returned Location should have. If nullopt, the address for the
  // location will be the beginning of the ELF symbol.
  Location MakeElfSymbolLocation(const SymbolContext& symbol_context,
                                 std::optional<uint64_t> relative_address,
                                 const ElfSymbolRecord& record) const;

  // Fills the forward and backward indices for ELF symbols.
  void FillElfSymbols();

  std::unique_ptr<DwarfBinary> binary_;

  std::string build_dir_;

  Index index_;

  // Maps the mangled symbol name to the elf symbol record. There can technically be more than
  // one entry for a name.
  //
  // This structure is also the canonical storage for ElfSymbolRecords used by elf_addresses_.
  //
  // We could potentially store this in the Index instead of in this separate location. This will be
  // required if we need to support unmangled PLT names. A plan:
  //
  //  - Declare that PLT symbols are encoded as "ns::ClassName::$plt(name)" since special
  //    annotations are per-component.
  //  - Make an "elf" type of IndexNode that the $plt component will match.
  //  - Rename IndexNode::DieRef to IndexNode::SymbolRef and convert the is_declaration flag to an
  //    enum. Add an enum value "ELF symbol".
  //  - Store the ELFSymbolRecords here in a vector that can be indexed into. Use the offset ot the
  //    IndexNode::SymbolRef to mean this index when it's an ELF type of SymbolRef. Hook this up to
  //    IndexDieRefToSymbol.
  using ElfSymbolMap = std::multimap<std::string, const ElfSymbolRecord>;
  ElfSymbolMap mangled_elf_symbols_;

  // All symbols in the mangled_elf_symbols_ map (pointers owned by that structure) sorted by the
  // relative address. Theoretically there can be more than one symbol for the same address.
  std::vector<const ElfSymbolRecord*> elf_addresses_;

  fxl::RefPtr<DwarfSymbolFactory> symbol_factory_;

  fxl::WeakPtrFactory<ModuleSymbolsImpl> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ModuleSymbolsImpl);
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_MODULE_SYMBOLS_IMPL_H_
