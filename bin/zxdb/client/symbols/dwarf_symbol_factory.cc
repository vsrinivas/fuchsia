// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/symbols/dwarf_symbol_factory.h"

#include "garnet/bin/zxdb/client/symbols/base_type.h"
#include "garnet/bin/zxdb/client/symbols/code_block.h"
#include "garnet/bin/zxdb/client/symbols/dwarf_die_decoder.h"
#include "garnet/bin/zxdb/client/symbols/function.h"
#include "garnet/bin/zxdb/client/symbols/modified_type.h"
#include "garnet/bin/zxdb/client/symbols/module_symbols_impl.h"
#include "garnet/bin/zxdb/client/symbols/symbol.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugInfoEntry.h"
#include "llvm/DebugInfo/DWARF/DWARFUnit.h"

namespace zxdb {

namespace {

// Generates ranges for a CodeBlock. The attributes may be not present, this
// function will compute what it can given the information (which may be an
// empty vector).
//
// TODO(brettw) add a parameter for DW_AT_ranges to handle discontiguous code
// ranges also (normally either that or low/high will be set).

CodeBlock::CodeRanges MakeCodeRanges(const llvm::Optional<uint64_t>& low_pc,
                                     const llvm::Optional<uint64_t>& high_pc) {
  CodeBlock::CodeRanges code_ranges;
  if (low_pc && high_pc) {
    code_ranges.push_back(CodeBlock::CodeRange(*low_pc, *high_pc));
  }
  return code_ranges;
}

// Extracts a FileLine if possible from the given input. If the optional values
// aren't present, returns an empty FileLine.
FileLine MakeFileLine(const llvm::Optional<std::string>& file,
                      const llvm::Optional<uint64_t>& line) {
  if (file && line)
    return FileLine(*file, static_cast<int>(*line));
  return FileLine();
}

}  // namespace

DwarfSymbolFactory::DwarfSymbolFactory(fxl::WeakPtr<ModuleSymbolsImpl> symbols)
    : symbols_(symbols) {}
DwarfSymbolFactory::~DwarfSymbolFactory() = default;

fxl::RefPtr<Symbol> DwarfSymbolFactory::CreateSymbol(void* data_ptr,
                                                     uint32_t offset) {
  if (!symbols_)
    return fxl::MakeRefCounted<Symbol>();

  auto* unit = static_cast<llvm::DWARFCompileUnit*>(data_ptr);
  llvm::DWARFDie die = unit->getDIEForOffset(offset);
  if (!die.isValid())
    return fxl::MakeRefCounted<Symbol>();

  int tag = die.getTag();
  if (ModifiedType::IsTypeModifierTag(tag))
    return DecodeModifierType(die);

  switch (tag) {
    case llvm::dwarf::DW_TAG_base_type:
      return DecodeBaseType(die);
    case llvm::dwarf::DW_TAG_subprogram:
      return DecodeFunction(die);
    default:
      // All unhandled Tag types get a Symbol that has the correct tag, but
      // no other data.
      return fxl::MakeRefCounted<Symbol>(static_cast<int>(die.getTag()));
  }
}

LazySymbol DwarfSymbolFactory::MakeLazy(const llvm::DWARFDie& die) {
  return LazySymbol(fxl::RefPtr<SymbolFactory>(this), die.getDwarfUnit(),
                    die.getOffset());
}

fxl::RefPtr<Symbol> DwarfSymbolFactory::DecodeFunction(
    const llvm::DWARFDie& die, bool is_specification) {
  DwarfDieDecoder decoder(symbols_->context(), die.getDwarfUnit());

  llvm::DWARFDie specification;
  decoder.AddReference(llvm::dwarf::DW_AT_specification, &specification);

  llvm::Optional<const char*> name;
  decoder.AddCString(llvm::dwarf::DW_AT_name, &name);

  llvm::Optional<const char*> linkage_name;
  decoder.AddCString(llvm::dwarf::DW_AT_linkage_name, &linkage_name);

  llvm::Optional<uint64_t> low_pc;
  decoder.AddAddress(llvm::dwarf::DW_AT_low_pc, &low_pc);

  llvm::Optional<uint64_t> high_pc;
  decoder.AddAddress(llvm::dwarf::DW_AT_high_pc, &high_pc);

  llvm::DWARFDie type;
  decoder.AddReference(llvm::dwarf::DW_AT_type, &type);

  llvm::Optional<std::string> decl_file;
  decoder.AddFile(llvm::dwarf::DW_AT_decl_file, &decl_file);

  llvm::Optional<uint64_t> decl_line;
  decoder.AddUnsignedConstant(llvm::dwarf::DW_AT_decl_line, &decl_line);

  // TODO(brettw) handle DW_AT_ranges.

  if (!decoder.Decode(die))
    return fxl::MakeRefCounted<Symbol>();

  fxl::RefPtr<Function> function;

  // If this DIE has a link to a function specification (and we haven't already
  // followed such a link), first read that in to get things like the mangled
  // name, enclosing context, and declaration locations. Then we'll overlay our
  // values on that object.
  if (!is_specification && specification) {
    auto spec = DecodeFunction(specification, true);
    Function* spec_function = spec->AsFunction();
    // If the specification is invalid, just ignore it and read out the values
    // that we can find in this DIE. An empty one will be created below.
    if (spec_function)
      function = fxl::RefPtr<Function>(spec_function);
  }
  if (!function)
    function = fxl::MakeRefCounted<Function>();

  // Only set the enclosing block if it hasn't been set already. We want the
  // function specification's enclosing block if there was a specification
  // since it will contain the namespace and class stuff.
  if (!function->enclosing()) {
    llvm::DWARFDie parent = die.getParent();
    if (parent)
      function->set_enclosing(MakeLazy(parent));
  }

  if (name)
    function->set_name(*name);
  if (linkage_name)
    function->set_linkage_name(*linkage_name);
  function->set_code_ranges(MakeCodeRanges(low_pc, high_pc));
  function->set_decl_line(MakeFileLine(decl_file, decl_line));
  if (type)
    function->set_return_type(MakeLazy(type));

  return function;
}

fxl::RefPtr<Symbol> DwarfSymbolFactory::DecodeBaseType(
    const llvm::DWARFDie& die) {
  // This object and its setup could be cached for better performance.
  DwarfDieDecoder decoder(symbols_->context(), die.getDwarfUnit());

  llvm::Optional<const char*> name;
  decoder.AddCString(llvm::dwarf::DW_AT_name, &name);

  llvm::Optional<uint64_t> encoding;
  decoder.AddUnsignedConstant(llvm::dwarf::DW_AT_encoding, &encoding);

  llvm::Optional<uint64_t> byte_size;
  decoder.AddUnsignedConstant(llvm::dwarf::DW_AT_byte_size, &byte_size);

  llvm::Optional<uint64_t> bit_size;
  decoder.AddUnsignedConstant(llvm::dwarf::DW_AT_byte_size, &bit_size);

  llvm::Optional<uint64_t> bit_offset;
  decoder.AddUnsignedConstant(llvm::dwarf::DW_AT_byte_size, &bit_offset);

  if (!decoder.Decode(die))
    return fxl::MakeRefCounted<Symbol>();

  auto base_type = fxl::MakeRefCounted<BaseType>();
  if (name)
    base_type->set_assigned_name(*name);
  if (encoding)
    base_type->set_base_type(static_cast<int>(*encoding));
  if (byte_size)
    base_type->set_byte_size(static_cast<uint32_t>(*byte_size));
  if (bit_size)
    base_type->set_bit_size(static_cast<int>(*bit_size));
  if (bit_offset)
    base_type->set_bit_offset(static_cast<int>(*bit_offset));

  return base_type;
}

fxl::RefPtr<Symbol> DwarfSymbolFactory::DecodeModifierType(
    const llvm::DWARFDie& die) {
  DwarfDieDecoder decoder(symbols_->context(), die.getDwarfUnit());

  llvm::DWARFDie modified;
  decoder.AddReference(llvm::dwarf::DW_AT_type, &modified);

  if (!decoder.Decode(die) || !modified.isValid())
    return fxl::MakeRefCounted<Symbol>();

  auto result = fxl::MakeRefCounted<ModifiedType>(die.getTag());
  result->set_modified(MakeLazy(modified));
  return result;
}

}  // namespace zxdb
