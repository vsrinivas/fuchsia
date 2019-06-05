// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/dwarf_symbol_factory.h"

#include <algorithm>

#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/DebugInfo/DWARF/DWARFDataExtractor.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugInfoEntry.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugLoc.h"
#include "llvm/DebugInfo/DWARF/DWARFSection.h"
#include "llvm/DebugInfo/DWARF/DWARFUnit.h"
#include "src/developer/debug/zxdb/symbols/array_type.h"
#include "src/developer/debug/zxdb/symbols/base_type.h"
#include "src/developer/debug/zxdb/symbols/code_block.h"
#include "src/developer/debug/zxdb/symbols/collection.h"
#include "src/developer/debug/zxdb/symbols/data_member.h"
#include "src/developer/debug/zxdb/symbols/dwarf_die_decoder.h"
#include "src/developer/debug/zxdb/symbols/enumeration.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/function_type.h"
#include "src/developer/debug/zxdb/symbols/inherited_from.h"
#include "src/developer/debug/zxdb/symbols/member_ptr.h"
#include "src/developer/debug/zxdb/symbols/modified_type.h"
#include "src/developer/debug/zxdb/symbols/module_symbols_impl.h"
#include "src/developer/debug/zxdb/symbols/namespace.h"
#include "src/developer/debug/zxdb/symbols/symbol.h"
#include "src/developer/debug/zxdb/symbols/variable.h"

namespace zxdb {

namespace {

// Generates ranges for a CodeBlock. The attributes may be not present, this
// function will compute what it can given the information (which may be an
// empty vector).
AddressRanges GetCodeRanges(const llvm::DWARFDie& die) {
  AddressRanges::RangeVector code_ranges;

  // It would be trivially more efficient to get the DW_AT_ranges, etc.
  // attributes out when we're iterating through the DIE. But the address
  // ranges have many different forms and also vary between DWARF versions 4
  // and 5. It's easier to let LLVM deal with this complexity.
  auto expected_ranges = die.getAddressRanges();
  if (!expected_ranges || expected_ranges->empty())
    return AddressRanges();

  code_ranges.reserve(expected_ranges->size());
  for (const llvm::DWARFAddressRange& range : *expected_ranges) {
    if (range.valid())
      code_ranges.emplace_back(range.LowPC, range.HighPC);
  }

  // Can't trust DWARF to have stored them in any particular order.
  return AddressRanges(AddressRanges::kNonCanonical, std::move(code_ranges));
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

    // These location list begin and end values are "relative to the applicable
    // base address of the compilation unit referencing this location list."
    //
    // "The applicable base address of a location list entry is determined by
    // the closest preceding base address selection entry in the same location
    // list. If there is no such selection entry, then the applicable base
    // address defaults to the base address of the compilation unit."
    //
    // LLVM doesn't seem to handle the "base address selection entry" in
    // location lists, so we assume that Clang won't generate them either.
    // Assume all addresses are relative to the compilation unit's base
    // address which is in DW_AT_low_pc
    auto base_address = const_cast<llvm::DWARFUnit*>(unit)->getBaseAddress();
    if (base_address) {
      dest.begin = base_address->Address + llvm_entry.Begin;
      dest.end = base_address->Address + llvm_entry.End;
      const uint8_t* data =
          reinterpret_cast<const uint8_t*>(llvm_entry.Loc.data());
      dest.expression.assign(data, data + llvm_entry.Loc.size());
    } else {
      FXL_NOTREACHED() << "No base address.";
    }
  }
  return VariableLocation(std::move(entries));
}

// Extracts the subrange size from an array subrange DIE. Puts the result in
// *size and returns true on success, false on failure.
bool ReadArraySubrange(llvm::DWARFContext* context,
                       const llvm::DWARFDie& subrange_die, uint64_t* size) {
  // Extract the DW_AT_count attribute (an unsigned number).
  DwarfDieDecoder range_decoder(context, subrange_die.getDwarfUnit());
  llvm::Optional<uint64_t> count;
  range_decoder.AddUnsignedConstant(llvm::dwarf::DW_AT_count, &count);
  if (!range_decoder.Decode(subrange_die) || !count)
    return false;
  *size = *count;
  return true;
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
  DwarfTag tag = static_cast<DwarfTag>(die.getTag());
  if (DwarfTagIsTypeModifier(tag))
    return DecodeModifiedType(die);

  fxl::RefPtr<Symbol> symbol;
  switch (tag) {
    case DwarfTag::kArrayType:
      symbol = DecodeArrayType(die);
      break;
    case DwarfTag::kBaseType:
      symbol = DecodeBaseType(die);
      break;
    case DwarfTag::kEnumerationType:
      symbol = DecodeEnum(die);
      break;
    case DwarfTag::kFormalParameter:
    case DwarfTag::kVariable:
      symbol = DecodeVariable(die);
      break;
    case DwarfTag::kSubroutineType:
      symbol = DecodeFunctionType(die);
      break;
    case DwarfTag::kImportedDeclaration:
      symbol = DecodeImportedDeclaration(die);
      break;
    case DwarfTag::kInheritance:
      symbol = DecodeInheritedFrom(die);
      break;
    case DwarfTag::kLexicalBlock:
      symbol = DecodeLexicalBlock(die);
      break;
    case DwarfTag::kMember:
      symbol = DecodeDataMember(die);
      break;
    case DwarfTag::kNamespace:
      symbol = DecodeNamespace(die);
      break;
    case DwarfTag::kPtrToMemberType:
      symbol = DecodeMemberPtr(die);
      break;
    case DwarfTag::kInlinedSubroutine:
    case DwarfTag::kSubprogram:
      symbol = DecodeFunction(die, tag);
      break;
    case DwarfTag::kStructureType:
    case DwarfTag::kClassType:
    case DwarfTag::kUnionType:
      symbol = DecodeCollection(die);
      break;
    case DwarfTag::kUnspecifiedType:
      symbol = DecodeUnspecifiedType(die);
      break;
    default:
      // All unhandled Tag types get a Symbol that has the correct tag, but
      // no other data.
      symbol = fxl::MakeRefCounted<Symbol>(static_cast<DwarfTag>(die.getTag()));
  }

  // Set the parent block if it hasn't been set already by the type-specific
  // factory. In particular, we want the function/variable specification's
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
    const llvm::DWARFDie& die, DwarfTag tag, bool is_specification) {
  DwarfDieDecoder decoder(symbols_->context(), die.getDwarfUnit());

  llvm::DWARFDie parent;
  decoder.AddAbstractParent(&parent);

  llvm::DWARFDie specification;
  decoder.AddReference(llvm::dwarf::DW_AT_specification, &specification);

  llvm::Optional<const char*> name;
  decoder.AddCString(llvm::dwarf::DW_AT_name, &name);

  llvm::Optional<const char*> linkage_name;
  decoder.AddCString(llvm::dwarf::DW_AT_linkage_name, &linkage_name);

  llvm::DWARFDie return_type;
  decoder.AddReference(llvm::dwarf::DW_AT_type, &return_type);

  // Declaration location.
  llvm::Optional<std::string> decl_file;
  llvm::Optional<uint64_t> decl_line;
  decoder.AddFile(llvm::dwarf::DW_AT_decl_file, &decl_file);
  decoder.AddUnsignedConstant(llvm::dwarf::DW_AT_decl_line, &decl_line);

  // Call location (inline functions only).
  llvm::Optional<std::string> call_file;
  llvm::Optional<uint64_t> call_line;
  if (tag == DwarfTag::kInlinedSubroutine) {
    decoder.AddFile(llvm::dwarf::DW_AT_call_file, &call_file);
    decoder.AddUnsignedConstant(llvm::dwarf::DW_AT_call_line, &call_line);
  }

  VariableLocation frame_base;
  decoder.AddCustom(llvm::dwarf::DW_AT_frame_base,
                    [unit = die.getDwarfUnit(),
                     &frame_base](const llvm::DWARFFormValue& value) {
                      frame_base = DecodeVariableLocation(unit, value);
                    });

  llvm::DWARFDie object_ptr;
  decoder.AddReference(llvm::dwarf::DW_AT_object_pointer, &object_ptr);

  if (!decoder.Decode(die))
    return fxl::MakeRefCounted<Symbol>();

  fxl::RefPtr<Function> function;

  // If this DIE has a link to a function specification (and we haven't already
  // followed such a link), first read that in to get things like the mangled
  // name, parent context, and declaration locations. Then we'll overlay our
  // values on that object.
  if (!is_specification && specification) {
    auto spec = DecodeFunction(specification, tag, true);
    Function* spec_function = spec->AsFunction();
    // If the specification is invalid, just ignore it and read out the values
    // that we can find in this DIE. An empty one will be created below.
    if (spec_function)
      function = fxl::RefPtr<Function>(spec_function);
  }
  if (!function)
    function = fxl::MakeRefCounted<Function>(tag);

  if (name)
    function->set_assigned_name(*name);
  if (linkage_name)
    function->set_linkage_name(*linkage_name);
  function->set_code_ranges(GetCodeRanges(die));
  function->set_decl_line(MakeFileLine(decl_file, decl_line));
  function->set_call_line(MakeFileLine(call_file, call_line));
  if (return_type)
    function->set_return_type(MakeLazy(return_type));
  function->set_frame_base(std::move(frame_base));
  if (object_ptr)
    function->set_object_pointer(MakeLazy(object_ptr));

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
      case llvm::dwarf::DW_TAG_inlined_subroutine:
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

  if (parent && !function->parent()) {
    // Set the parent symbol when it hasn't already been set. We always want
    // the specification's parent instead of the implementation block's
    // parent (if they're different) because the namespace and enclosing
    // class information comes from the declaration.
    //
    // If this is already set, it means we recursively followed the
    // specification which already set it.
    function->set_parent(MakeLazy(parent));
  }

  if (tag == DwarfTag::kInlinedSubroutine) {
    // In contrast to the logic for parent() above, the direct containing block
    // of the inlined subroutine will save the CodeBlock inlined functions are
    // embedded in.
    llvm::DWARFDie direct_parent = die.getParent();
    if (direct_parent)
      function->set_containing_block(MakeLazy(direct_parent));
  }

  return function;
}

// We expect array types to have two things:
// - An attribute linking to the underlying type of the array.
// - One or more DW_TAG_subrange_type children that hold the size of the array
//   in a DW_AT_count attribute.
//
// The subrange child is weird because the subrange links to its own type.
// LLVM generates a synthetic type __ARRAY_SIZE_TYPE__ that the
// DW_TAG_subrange_count DIE references from DW_AT_type attribute. We ignore
// this and only use the count.
//
// One might expect 2-dimensional arrays to be expressed as an array of one
// dimension where the contained type is an array of another. But both Clang
// and GCC generate one array entry with two subrange children. The order of
// these represents the declaration order in the code.
fxl::RefPtr<Symbol> DwarfSymbolFactory::DecodeArrayType(
    const llvm::DWARFDie& die) {
  // Extract the type attribute from the root DIE (should be a
  // DW_TAG_array_type).
  DwarfDieDecoder array_decoder(symbols_->context(), die.getDwarfUnit());
  llvm::DWARFDie type;
  array_decoder.AddReference(llvm::dwarf::DW_AT_type, &type);
  if (!array_decoder.Decode(die) || !type)
    return fxl::MakeRefCounted<Symbol>();

  // Need the concrete symbol for the contained type for the array constructor.
  auto contained = DecodeSymbol(type);
  if (!contained)
    return fxl::MakeRefCounted<Symbol>();
  Type* contained_type = contained->AsType();
  if (!contained_type)
    return fxl::MakeRefCounted<Symbol>();

  // Find all subranges stored in the declaration order. More than one means a
  // multi-dimensional array.
  std::vector<uint64_t> subrange_sizes;
  for (const llvm::DWARFDie& child : die) {
    if (child.getTag() == llvm::dwarf::DW_TAG_subrange_type) {
      subrange_sizes.push_back(0);
      if (!ReadArraySubrange(symbols_->context(), child,
                             &subrange_sizes.back()))
        return fxl::MakeRefCounted<Symbol>();
    }
  }

  // Require a subrange with a count in it. If we find cases where this isn't
  // the case, we could add support for array types with unknown lengths,
  // but currently ArrayType requires a size.
  if (subrange_sizes.empty())
    return fxl::MakeRefCounted<Symbol>();

  // Work backwards in the array dimensions generating nested array
  // definitions. The innermost definition refers to the contained type.
  fxl::RefPtr<Type> cur(contained_type);
  for (int i = static_cast<int>(subrange_sizes.size()) - 1; i >= 0; i--) {
    auto new_array =
        fxl::MakeRefCounted<ArrayType>(std::move(cur), subrange_sizes[i]);
    cur = std::move(new_array);
  }
  return cur;
}

fxl::RefPtr<Symbol> DwarfSymbolFactory::DecodeBaseType(
    const llvm::DWARFDie& die) {
  // This object and its setup could be cached for better performance.
  DwarfDieDecoder decoder(symbols_->context(), die.getDwarfUnit());

  // Types must always use the parent of the abstract origin (if it exists) so
  // they can be nested in the correct namespace.
  llvm::DWARFDie parent;
  decoder.AddAbstractParent(&parent);

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

  if (parent)
    base_type->set_parent(MakeLazy(parent));

  return base_type;
}

fxl::RefPtr<Symbol> DwarfSymbolFactory::DecodeCollection(
    const llvm::DWARFDie& die) {
  DwarfDieDecoder decoder(symbols_->context(), die.getDwarfUnit());

  // Types must always use the parent of the abstract origin (if it exists) so
  // they can be nested in the correct namespace.
  llvm::DWARFDie parent;
  decoder.AddAbstractParent(&parent);

  llvm::Optional<const char*> name;
  decoder.AddCString(llvm::dwarf::DW_AT_name, &name);

  llvm::Optional<uint64_t> byte_size;
  decoder.AddUnsignedConstant(llvm::dwarf::DW_AT_byte_size, &byte_size);

  llvm::Optional<bool> is_declaration;
  decoder.AddBool(llvm::dwarf::DW_AT_declaration, &is_declaration);

  if (!decoder.Decode(die))
    return fxl::MakeRefCounted<Symbol>();

  auto result =
      fxl::MakeRefCounted<Collection>(static_cast<DwarfTag>(die.getTag()));
  if (name)
    result->set_assigned_name(*name);
  if (byte_size)
    result->set_byte_size(static_cast<uint32_t>(*byte_size));

  // Handle sub-DIEs: data members and inheritance.
  std::vector<LazySymbol> data;
  std::vector<LazySymbol> inheritance;
  for (const llvm::DWARFDie& child : die) {
    switch (child.getTag()) {
      case llvm::dwarf::DW_TAG_inheritance:
        inheritance.push_back(MakeLazy(child));
        break;
      case llvm::dwarf::DW_TAG_member:
        data.push_back(MakeLazy(child));
        break;
      default:
        break;  // Skip everything else.
    }
  }
  result->set_data_members(std::move(data));
  result->set_inherited_from(std::move(inheritance));
  if (is_declaration)
    result->set_is_declaration(*is_declaration);

  if (parent)
    result->set_parent(MakeLazy(parent));

  return result;
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

fxl::RefPtr<Symbol> DwarfSymbolFactory::DecodeEnum(const llvm::DWARFDie& die) {
  DwarfDieDecoder main_decoder(symbols_->context(), die.getDwarfUnit());

  // Types must always use the parent of the abstract origin (if it exists) so
  // they can be nested in the correct namespace.
  llvm::DWARFDie parent;
  main_decoder.AddAbstractParent(&parent);

  // Name is optional (enums can be anonymous).
  llvm::Optional<const char*> type_name;
  main_decoder.AddCString(llvm::dwarf::DW_AT_name, &type_name);

  llvm::Optional<uint64_t> byte_size;
  main_decoder.AddUnsignedConstant(llvm::dwarf::DW_AT_byte_size, &byte_size);

  llvm::Optional<bool> is_declaration;
  main_decoder.AddBool(llvm::dwarf::DW_AT_declaration, &is_declaration);

  // The type is optional for an enumeration.
  llvm::DWARFDie type;
  main_decoder.AddReference(llvm::dwarf::DW_AT_type, &type);

  // For decoding the individual enum values.
  DwarfDieDecoder enumerator_decoder(symbols_->context(), die.getDwarfUnit());

  llvm::Optional<const char*> enumerator_name;
  enumerator_decoder.AddCString(llvm::dwarf::DW_AT_name, &enumerator_name);

  // Enum values can be signed or unsigned. This is determined by looking at
  // the form of the storage for the underlying types. Since there are many
  // values, we set the "signed" flag if any of them were signed, since a
  // small positive integer could be represented either way but a signed value
  // must be encoded differently.
  llvm::Optional<uint64_t> enumerator_value;
  bool is_signed = false;
  enumerator_decoder.AddCustom(
      llvm::dwarf::DW_AT_const_value,
      [&enumerator_value, &is_signed](const llvm::DWARFFormValue& value) {
        if (value.getForm() == llvm::dwarf::DW_FORM_udata) {
          enumerator_value = value.getAsUnsignedConstant();
        } else if (value.getForm() == llvm::dwarf::DW_FORM_sdata) {
          // Cast signed values to unsigned.
          if (auto signed_value = value.getAsSignedConstant()) {
            is_signed = true;
            enumerator_value = static_cast<uint64_t>(*signed_value);
          }
          // Else case is corrupted symbols or an unsupported format, just
          // ignore this one.
        }
      });

  if (!main_decoder.Decode(die))
    return fxl::MakeRefCounted<Symbol>();

  Enumeration::Map map;
  for (const llvm::DWARFDie& child : die) {
    if (child.getTag() != llvm::dwarf::DW_TAG_enumerator)
      continue;

    enumerator_name.reset();
    enumerator_value.reset();
    if (!enumerator_decoder.Decode(child))
      continue;
    if (enumerator_name && enumerator_value)
      map[*enumerator_value] = std::string(*enumerator_name);
  }

  LazySymbol lazy_type;
  if (type)
    lazy_type = MakeLazy(type);
  const char* type_name_str = type_name ? *type_name : "";
  auto result =
      fxl::MakeRefCounted<Enumeration>(type_name_str, std::move(lazy_type),
                                       *byte_size, is_signed, std::move(map));
  if (parent)
    result->set_parent(MakeLazy(parent));
  if (is_declaration)
    result->set_is_declaration(*is_declaration);
  return result;
}

fxl::RefPtr<Symbol> DwarfSymbolFactory::DecodeFunctionType(
    const llvm::DWARFDie& die) {
  DwarfDieDecoder decoder(symbols_->context(), die.getDwarfUnit());

  // Types must always use the parent of the abstract origin (if it exists) so
  // they can be nested in the correct namespace.
  llvm::DWARFDie parent;
  decoder.AddAbstractParent(&parent);

  llvm::DWARFDie return_type;
  decoder.AddReference(llvm::dwarf::DW_AT_type, &return_type);

  if (!decoder.Decode(die))
    return fxl::MakeRefCounted<Symbol>();

  // Handle sub-DIEs (this only has parameters).
  std::vector<LazySymbol> parameters;
  for (const llvm::DWARFDie& child : die) {
    switch (child.getTag()) {
      case llvm::dwarf::DW_TAG_formal_parameter:
        parameters.push_back(MakeLazy(child));
        break;
      default:
        break;  // Skip everything else.
    }
  }

  LazySymbol lazy_return_type;
  if (return_type)
    lazy_return_type = MakeLazy(return_type);

  auto function = fxl::MakeRefCounted<FunctionType>(lazy_return_type,
                                                    std::move(parameters));
  if (parent)
    function->set_parent(MakeLazy(parent));
  return function;
}

// Imported declarations are "using" statements that don't provide a new name
// like "using std::vector;".
//
// Type renames like "using Foo = std::vector;" is encoded as a typedef.
fxl::RefPtr<Symbol> DwarfSymbolFactory::DecodeImportedDeclaration(
    const llvm::DWARFDie& die) {
  DwarfDieDecoder decoder(symbols_->context(), die.getDwarfUnit());

  llvm::DWARFDie imported;
  decoder.AddReference(llvm::dwarf::DW_AT_import, &imported);

  if (!decoder.Decode(die) || !imported)
    return fxl::MakeRefCounted<Symbol>();

  return fxl::MakeRefCounted<ModifiedType>(DwarfTag::kImportedDeclaration,
                                           MakeLazy(imported));
}

fxl::RefPtr<Symbol> DwarfSymbolFactory::DecodeInheritedFrom(
    const llvm::DWARFDie& die) {
  DwarfDieDecoder decoder(symbols_->context(), die.getDwarfUnit());

  llvm::DWARFDie type;
  decoder.AddReference(llvm::dwarf::DW_AT_type, &type);

  llvm::Optional<uint64_t> member_offset;
  decoder.AddUnsignedConstant(llvm::dwarf::DW_AT_data_member_location,
                              &member_offset);

  if (!decoder.Decode(die))
    return fxl::MakeRefCounted<Symbol>();

  LazySymbol lazy_type;
  if (type)
    lazy_type = MakeLazy(type);
  if (!member_offset) {
    // According to the spec the offset could be a location description which
    // won't have been read as an unsigned. See InheritedFrom::offset() for
    // more. If this triggers, we should implement and test this feature.
    fprintf(stderr,
            "DW_TAG_inheritance has a non-constant offset for the base class. "
            "Please file a bug with a repro so we can support this case.\n");
    return fxl::MakeRefCounted<Symbol>();
  }

  return fxl::MakeRefCounted<InheritedFrom>(std::move(lazy_type),
                                            *member_offset);
}

fxl::RefPtr<Symbol> DwarfSymbolFactory::DecodeLexicalBlock(
    const llvm::DWARFDie& die) {
  auto block = fxl::MakeRefCounted<CodeBlock>(DwarfTag::kLexicalBlock);
  block->set_code_ranges(GetCodeRanges(die));

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

fxl::RefPtr<Symbol> DwarfSymbolFactory::DecodeMemberPtr(
    const llvm::DWARFDie& die) {
  DwarfDieDecoder decoder(symbols_->context(), die.getDwarfUnit());

  llvm::DWARFDie container_type;
  decoder.AddReference(llvm::dwarf::DW_AT_containing_type, &container_type);
  llvm::DWARFDie type;
  decoder.AddReference(llvm::dwarf::DW_AT_type, &type);

  if (!decoder.Decode(die) || !container_type || !type)
    return fxl::MakeRefCounted<Symbol>();

  auto member_ptr =
      fxl::MakeRefCounted<MemberPtr>(MakeLazy(container_type), MakeLazy(type));
  return member_ptr;
}

fxl::RefPtr<Symbol> DwarfSymbolFactory::DecodeModifiedType(
    const llvm::DWARFDie& die) {
  DwarfDieDecoder decoder(symbols_->context(), die.getDwarfUnit());

  // Types must always use the parent of the abstract origin (if it exists) so
  // they can be nested in the correct namespace.
  llvm::DWARFDie parent;
  decoder.AddAbstractParent(&parent);

  llvm::Optional<const char*> name;
  decoder.AddCString(llvm::dwarf::DW_AT_name, &name);

  llvm::DWARFDie modified;
  decoder.AddReference(llvm::dwarf::DW_AT_type, &modified);

  if (!decoder.Decode(die))
    return fxl::MakeRefCounted<Symbol>();

  // Modified type may be null for "void*".
  LazySymbol lazy_modified;
  if (modified)
    lazy_modified = MakeLazy(modified);

  auto result = fxl::MakeRefCounted<ModifiedType>(
      static_cast<DwarfTag>(die.getTag()), std::move(lazy_modified));
  if (name)
    result->set_assigned_name(*name);

  if (parent)
    result->set_parent(MakeLazy(parent));

  return result;
}

fxl::RefPtr<Symbol> DwarfSymbolFactory::DecodeNamespace(
    const llvm::DWARFDie& die) {
  DwarfDieDecoder decoder(symbols_->context(), die.getDwarfUnit());

  // Types must always use the parent of the abstract origin (if it exists) so
  // they can be nested in the correct namespace.
  llvm::DWARFDie parent;
  decoder.AddAbstractParent(&parent);

  llvm::Optional<const char*> name;
  decoder.AddCString(llvm::dwarf::DW_AT_name, &name);

  if (!decoder.Decode(die))
    return fxl::MakeRefCounted<Symbol>();

  auto result = fxl::MakeRefCounted<Namespace>();
  if (name)
    result->set_assigned_name(*name);

  if (parent)
    result->set_parent(MakeLazy(parent));
  return result;
}

// Clang and GCC use "unspecified" types to encode "decltype(nullptr)". When
// used as a variable this appears as a pointer with 0 value, despite not
// having any declared size in the symbols. Therefore, we make up a byte size
// equal to the pointer size (8 bytes on our 64-bit systems).
fxl::RefPtr<Symbol> DwarfSymbolFactory::DecodeUnspecifiedType(
    const llvm::DWARFDie& die) {
  DwarfDieDecoder decoder(symbols_->context(), die.getDwarfUnit());

  // Types must always use the parent of the abstract origin (if it exists) so
  // they can be nested in the correct namespace.
  llvm::DWARFDie parent;
  decoder.AddAbstractParent(&parent);

  llvm::Optional<const char*> name;
  decoder.AddCString(llvm::dwarf::DW_AT_name, &name);

  if (!decoder.Decode(die))
    return fxl::MakeRefCounted<Symbol>();

  auto result = fxl::MakeRefCounted<Type>(DwarfTag::kUnspecifiedType);
  if (name)
    result->set_assigned_name(*name);
  result->set_byte_size(8);  // Assume pointer.
  if (parent)
    result->set_parent(MakeLazy(parent));

  return result;
}

fxl::RefPtr<Symbol> DwarfSymbolFactory::DecodeVariable(
    const llvm::DWARFDie& die, bool is_specification) {
  DwarfDieDecoder decoder(symbols_->context(), die.getDwarfUnit());

  llvm::DWARFDie specification;
  decoder.AddReference(llvm::dwarf::DW_AT_specification, &specification);

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

  fxl::RefPtr<Variable> variable;

  // If this DIE has a link to a specification (and we haven't already followed
  // such a link), first read that in to get things like the mangled name,
  // parent context, and declaration locations. Then we'll overlay our values
  // on that object.
  if (!is_specification && specification) {
    auto spec = DecodeVariable(specification, true);
    Variable* spec_variable = spec->AsVariable();
    // If the specification is invalid, just ignore it and read out the values
    // that we can find in this DIE. An empty one will be created below.
    if (spec_variable)
      variable = fxl::RefPtr<Variable>(spec_variable);
  }
  if (!variable) {
    variable =
        fxl::MakeRefCounted<Variable>(static_cast<DwarfTag>(die.getTag()));
  }

  if (name)
    variable->set_assigned_name(*name);
  if (type)
    variable->set_type(MakeLazy(type));
  variable->set_location(std::move(location));

  if (!variable->parent()) {
    // Set the parent symbol when it hasn't already been set. As with
    // functions, we always want the specification's parent. See
    // DecodeFunction().
    llvm::DWARFDie parent = die.getParent();
    if (parent)
      variable->set_parent(MakeLazy(parent));
  }
  return variable;
}

}  // namespace zxdb
