// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_MODULE_SYMBOLS_IMPL_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_MODULE_SYMBOLS_IMPL_H_

#include "garnet/third_party/llvm/include/llvm/BinaryFormat/ELF.h"
#include "gtest/gtest_prod.h"
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

class DwarfSymbolFactory;
class Variable;

// Represents the symbols for a module (executable or shared library). See ModuleSymbols.
class ModuleSymbolsImpl : public ModuleSymbols {
 public:
  // These are invalid until Load() has completed successfully.
  llvm::DWARFContext* context() { return context_.get(); }
  llvm::DWARFUnitVector& compile_units() { return compile_units_; }
  DwarfSymbolFactory* symbol_factory() { return symbol_factory_.get(); }
  llvm::object::ObjectFile* object_file() {
    return static_cast<llvm::object::ObjectFile*>(binary_.get());
  }

  // Normal callers will always want to create the index. The only time this is unnecessary is
  // from certain tests that want to do it themselves.
  Err Load(bool create_index = true);

  fxl::WeakPtr<ModuleSymbolsImpl> GetWeakPtr();

  // ModuleSymbols implementation.
  ModuleSymbolStatus GetStatus() const override;
  std::time_t GetModificationTime() const override;
  std::vector<Location> ResolveInputLocation(
      const SymbolContext& symbol_context, const InputLocation& input_location,
      const ResolveOptions& options = ResolveOptions()) const override;
  LineDetails LineDetailsForAddress(const SymbolContext& symbol_context,
                                    uint64_t absolute_address) const override;
  std::vector<std::string> FindFileMatches(std::string_view name) const override;
  std::vector<fxl::RefPtr<Function>> GetMainFunctions() const override;
  const Index& GetIndex() const override;
  LazySymbol IndexDieRefToSymbol(const IndexNode::DieRef&) const override;
  bool HasBinary() const override;

 private:
  FRIEND_MAKE_REF_COUNTED(ModuleSymbolsImpl);
  FRIEND_REF_COUNTED_THREAD_SAFE(ModuleSymbolsImpl);
  FRIEND_TEST(ModuleSymbols, ResolveMainFunction);

  // You must call Load before using this class.
  ModuleSymbolsImpl(const std::string& name, const std::string& binary_name,
                    const std::string& build_id);
  ~ModuleSymbolsImpl() override;

  llvm::DWARFUnit* CompileUnitForRelativeAddress(uint64_t relative_address) const;

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

  // Resolves the line number information for the given file, which must be an exact match. This is
  // a helper function for ResolveLineInputLocation().
  //
  // This appends to the given output.
  void ResolveLineInputLocationForFile(const SymbolContext& symbol_context,
                                       const std::string& canonical_file, int line_number,
                                       const ResolveOptions& options,
                                       std::vector<Location>* output) const;

  void FillElfSymbols(const std::map<std::string, llvm::ELF::Elf64_Sym>& elf_syms);

  const std::string name_;
  const std::string binary_name_;
  const std::string build_id_;

  std::time_t modification_time_ = 0;  // Set when the file is loaded.

  std::unique_ptr<llvm::MemoryBuffer> binary_buffer_;  // Backing for binary_.
  std::unique_ptr<llvm::object::Binary> binary_;
  std::unique_ptr<llvm::DWARFContext> context_;  // binary_ must outlive this.

  llvm::DWARFUnitVector compile_units_;

  Index index_;

  std::map<std::string, uint64_t> plt_locations_;

  // Sorted list of ELF symbols by mangled name. See also elf_symbols_by_address vector.
  std::vector<ElfSymbolRecord> elf_symbols_by_name_;

  // List of all ELF symbols sorted by address. There is nothing ensuring the addresses are unique
  // so code should take care to allow for multiple matches. These indices refer to items inside the
  // elf_symbols_by_name vector.
  std::vector<size_t> elf_symbols_by_address_;

  fxl::RefPtr<DwarfSymbolFactory> symbol_factory_;

  fxl::WeakPtrFactory<ModuleSymbolsImpl> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ModuleSymbolsImpl);
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_MODULE_SYMBOLS_IMPL_H_
