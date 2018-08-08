// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/symbols/dwarf_symbol_factory.h"

#include "garnet/bin/zxdb/client/symbols/base_type.h"
#include "garnet/bin/zxdb/client/symbols/code_block.h"
#include "garnet/bin/zxdb/client/symbols/data_member.h"
#include "garnet/bin/zxdb/client/symbols/dwarf_die_decoder.h"
#include "garnet/bin/zxdb/client/symbols/function.h"
#include "garnet/bin/zxdb/client/symbols/modified_type.h"
#include "garnet/bin/zxdb/client/symbols/module_symbols_impl.h"
#include "garnet/bin/zxdb/client/symbols/namespace.h"
#include "garnet/bin/zxdb/client/symbols/struct_class.h"
#include "garnet/bin/zxdb/client/symbols/symbol.h"
#include "garnet/bin/zxdb/client/symbols/variable.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/DebugInfo/DWARF/DWARFDataExtractor.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugInfoEntry.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugLoc.h"
#include "llvm/DebugInfo/DWARF/DWARFSection.h"
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

// Decodes the contents of DW_AT_Location attribute.
VariableLocation DecodeVariableLocation(const llvm::DWARFUnit* unit,
                                        const llvm::DWARFFormValue& form) {
  if (form.isFormClass(llvm::DWARFFormValue::FC_Block) ||
      form.isFormClass(llvm::DWARFFormValue::FC_Exprloc)) {
    // These forms are both a block of data which is interpreted as a DWARF
    // expression. There is no validity range for this so assume the expression
    // is valid as long as the variable is in scope.
    llvm::ArrayRef<uint8_t> block = *form.getAsBlock();
    return VariableLocation(block.data(), block.size());
  }

  if (!form.isFormClass(llvm::DWARFFormValue::FC_SectionOffset))
    return VariableLocation();  // Unknown type.

  // This form is a "section offset" reference to a block in the .debug_loc
  // table that contains a list of valid ranges + associated expressions.
  llvm::DWARFContext& context = unit->getContext();
  const llvm::DWARFObject& object = context.getDWARFObj();
  const llvm::DWARFSection& debug_loc_section = object.getLocSection();
  if (debug_loc_section.Data.empty()) {
    // LLVM dumpLocation() falls back on the DWARFObject::getLocDWOSection()
    // call in this case. We don't support DWOs yet so just fail.
    return VariableLocation();
  }
  // Already type-checked this above so can count on the Optional return value
  // being valid.
  uint32_t offset = *form.getAsSectionOffset();

  // Extract the LLVM location list.
  llvm::DWARFDebugLoc debug_loc;
  llvm::DWARFDataExtractor data(object, debug_loc_section,
                                context.isLittleEndian(),
                                object.getAddressSize());
  llvm::Optional<llvm::DWARFDebugLoc::LocationList> location_list =
      debug_loc.parseOneLocationList(data, &offset);
  if (!location_list)
    return VariableLocation();  // No locations.

  // Convert from llvm::DWARFDebugLoc::Entry to VariableLocation::Entry.
  std::vector<VariableLocation::Entry> entries;
  for (const llvm::DWARFDebugLoc::Entry& llvm_entry : location_list->Entries) {
    entries.emplace_back();
    VariableLocation::Entry& dest = entries.back();

    dest.begin = llvm_entry.Begin;
    dest.end = llvm_entry.End;
    const uint8_t* data =
        reinterpret_cast<const uint8_t*>(llvm_entry.Loc.data());
    dest.expression.assign(data, data + llvm_entry.Loc.size());
  }
  return VariableLocation(std::move(entries));
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

  return DecodeSymbol(die);
}

fxl::RefPtr<Symbol> DwarfSymbolFactory::DecodeSymbol(
    const llvm::DWARFDie& die) {
  int tag = die.getTag();
  if (ModifiedType::IsTypeModifierTag(tag))
    return DecodeModifiedType(die);

  fxl::RefPtr<Symbol> symbol;
  switch (tag) {
    case llvm::dwarf::DW_TAG_base_type:
      symbol = DecodeBaseType(die);
      break;
    case llvm::dwarf::DW_TAG_formal_parameter:
    case llvm::dwarf::DW_TAG_variable:
      symbol = DecodeVariable(die);
      break;
    case llvm::dwarf::DW_TAG_lexical_block:
      symbol = DecodeLexicalBlock(die);
      break;
    case llvm::dwarf::DW_TAG_member:
      symbol = DecodeDataMember(die);
      break;
    case llvm::dwarf::DW_TAG_namespace:
      symbol = DecodeNamespace(die);
      break;
    case llvm::dwarf::DW_TAG_subprogram:
      symbol = DecodeFunction(die);
      break;
    case llvm::dwarf::DW_TAG_structure_type:
    case llvm::dwarf::DW_TAG_class_type:
      symbol = DecodeStructClass(die);
      break;
    default:
      // All unhandled Tag types get a Symbol that has the correct tag, but
      // no other data.
      symbol = fxl::MakeRefCounted<Symbol>(static_cast<int>(die.getTag()));
  }

  // Only set the parent block if it hasn't been set already by the
  // type-specific factory. In particular, we want the function specification's
  // parent block if there was a specification since it will contain the
  // namespace and class stuff.
  if (!symbol->parent()) {
    llvm::DWARFDie parent = die.getParent();
    if (parent)
      symbol->set_parent(MakeLazy(parent));
  }

  return symbol;
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
  // name, parent context, and declaration locations. Then we'll overlay our
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

  if (name)
    function->set_assigned_name(*name);
  if (linkage_name)
    function->set_linkage_name(*linkage_name);
  function->set_code_ranges(MakeCodeRanges(low_pc, high_pc));
  function->set_decl_line(MakeFileLine(decl_file, decl_line));
  if (type)
    function->set_return_type(MakeLazy(type));

  // Handle sub-DIEs: parameters, child blocks, and variables.
  std::vector<LazySymbol> parameters;
  std::vector<LazySymbol> inner_blocks;
  std::vector<LazySymbol> variables;
  for (const llvm::DWARFDie& child : die) {
    switch (child.getTag()) {
      case llvm::dwarf::DW_TAG_formal_parameter:
        parameters.push_back(MakeLazy(child));
        break;
      case llvm::dwarf::DW_TAG_variable:
        variables.push_back(MakeLazy(child));
        break;
      case llvm::dwarf::DW_TAG_lexical_block:
        inner_blocks.push_back(MakeLazy(child));
        break;
      default:
        break;  // Skip everything else.
    }
  }
  function->set_parameters(std::move(parameters));
  function->set_inner_blocks(std::move(inner_blocks));
  function->set_variables(std::move(variables));

  if (is_specification) {
    // Always set the parent symbol when parsing a specification. This is the
    // thing that will carry the namespace and struct/class membership
    // information.
    llvm::DWARFDie parent = die.getParent();
    if (parent)
      function->set_parent(MakeLazy(parent));
  }

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

fxl::RefPtr<Symbol> DwarfSymbolFactory::DecodeDataMember(
    const llvm::DWARFDie& die) {
  DwarfDieDecoder decoder(symbols_->context(), die.getDwarfUnit());

  llvm::Optional<const char*> name;
  decoder.AddCString(llvm::dwarf::DW_AT_name, &name);

  llvm::DWARFDie type;
  decoder.AddReference(llvm::dwarf::DW_AT_type, &type);

  llvm::Optional<uint64_t> member_offset;
  decoder.AddUnsignedConstant(llvm::dwarf::DW_AT_data_member_location,
                              &member_offset);

  if (!decoder.Decode(die))
    return fxl::MakeRefCounted<Symbol>();

  auto result = fxl::MakeRefCounted<DataMember>();
  if (name)
    result->set_assigned_name(*name);
  if (type)
    result->set_type(MakeLazy(type));
  if (member_offset)
    result->set_member_location(static_cast<uint32_t>(*member_offset));
  return result;
}

fxl::RefPtr<Symbol> DwarfSymbolFactory::DecodeLexicalBlock(
    const llvm::DWARFDie& die) {
  DwarfDieDecoder decoder(symbols_->context(), die.getDwarfUnit());

  llvm::Optional<uint64_t> low_pc;
  decoder.AddAddress(llvm::dwarf::DW_AT_low_pc, &low_pc);

  llvm::Optional<uint64_t> high_pc;
  decoder.AddAddress(llvm::dwarf::DW_AT_high_pc, &high_pc);

  // TODO(brettw) handle DW_AT_ranges.

  if (!decoder.Decode(die))
    return fxl::MakeRefCounted<Symbol>();

  auto block = fxl::MakeRefCounted<CodeBlock>(Symbol::kTagLexicalBlock);
  block->set_code_ranges(MakeCodeRanges(low_pc, high_pc));

  // Handle sub-DIEs: child blocks and variables.
  std::vector<LazySymbol> inner_blocks;
  std::vector<LazySymbol> variables;
  for (const llvm::DWARFDie& child : die) {
    switch (child.getTag()) {
      case llvm::dwarf::DW_TAG_variable:
        variables.push_back(MakeLazy(child));
        break;
      case llvm::dwarf::DW_TAG_lexical_block:
        inner_blocks.push_back(MakeLazy(child));
        break;
      default:
        break;  // Skip everything else.
    }
  }
  block->set_inner_blocks(std::move(inner_blocks));
  block->set_variables(std::move(variables));

  return block;
}

fxl::RefPtr<Symbol> DwarfSymbolFactory::DecodeModifiedType(
    const llvm::DWARFDie& die) {
  DwarfDieDecoder decoder(symbols_->context(), die.getDwarfUnit());

  llvm::Optional<const char*> name;
  decoder.AddCString(llvm::dwarf::DW_AT_name, &name);

  llvm::DWARFDie modified;
  decoder.AddReference(llvm::dwarf::DW_AT_type, &modified);

  if (!decoder.Decode(die) || !modified.isValid())
    return fxl::MakeRefCounted<Symbol>();

  auto result = fxl::MakeRefCounted<ModifiedType>(die.getTag());
  result->set_modified(MakeLazy(modified));
  if (name)
    result->set_assigned_name(*name);

  return result;
}

fxl::RefPtr<Symbol> DwarfSymbolFactory::DecodeNamespace(
    const llvm::DWARFDie& die) {
  DwarfDieDecoder decoder(symbols_->context(), die.getDwarfUnit());

  llvm::Optional<const char*> name;
  decoder.AddCString(llvm::dwarf::DW_AT_name, &name);

  if (!decoder.Decode(die))
    return fxl::MakeRefCounted<Symbol>();

  auto result = fxl::MakeRefCounted<Namespace>();
  if (name)
    result->set_assigned_name(*name);
  return result;
}

fxl::RefPtr<Symbol> DwarfSymbolFactory::DecodeStructClass(
    const llvm::DWARFDie& die) {
  DwarfDieDecoder decoder(symbols_->context(), die.getDwarfUnit());

  llvm::Optional<const char*> name;
  decoder.AddCString(llvm::dwarf::DW_AT_name, &name);

  llvm::Optional<uint64_t> byte_size;
  decoder.AddUnsignedConstant(llvm::dwarf::DW_AT_byte_size, &byte_size);

  if (!decoder.Decode(die))
    return fxl::MakeRefCounted<Symbol>();

  auto result = fxl::MakeRefCounted<StructClass>(die.getTag());
  if (name)
    result->set_assigned_name(*name);
  if (byte_size)
    result->set_byte_size(static_cast<uint32_t>(*byte_size));

  // Handle sub-DIEs: data members.
  std::vector<LazySymbol> data;
  for (const llvm::DWARFDie& child : die) {
    switch (child.getTag()) {
      case llvm::dwarf::DW_TAG_member:
        data.push_back(MakeLazy(child));
        break;
      default:
        break;  // Skip everything else.
    }
  }
  result->set_data_members(std::move(data));
  return result;
}

fxl::RefPtr<Symbol> DwarfSymbolFactory::DecodeVariable(
    const llvm::DWARFDie& die) {
  DwarfDieDecoder decoder(symbols_->context(), die.getDwarfUnit());

  llvm::Optional<const char*> name;
  decoder.AddCString(llvm::dwarf::DW_AT_name, &name);

  VariableLocation location;
  decoder.AddCustom(llvm::dwarf::DW_AT_location,
                    [unit = die.getDwarfUnit(),
                     &location](const llvm::DWARFFormValue& value) {
                      location = DecodeVariableLocation(unit, value);
                    });

  llvm::DWARFDie type;
  decoder.AddReference(llvm::dwarf::DW_AT_type, &type);

  if (!decoder.Decode(die))
    return fxl::MakeRefCounted<Symbol>();

  auto result = fxl::MakeRefCounted<Variable>(die.getTag());
  if (name)
    result->set_assigned_name(*name);
  if (type)
    result->set_type(MakeLazy(type));
  result->set_location(std::move(location));
  return result;
}

}  // namespace zxdb
