// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/symbols/dwarf_die_decoder.h"

#include "garnet/public/lib/fxl/logging.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/DebugInfo/DWARF/DWARFFormValue.h"

namespace zxdb {

DwarfDieDecoder::DwarfDieDecoder(llvm::DWARFContext* context,
                                 llvm::DWARFUnit* unit)
    : context_(context),
      unit_(unit),
      extractor_(unit_->getDebugInfoExtractor()) {}

DwarfDieDecoder::~DwarfDieDecoder() = default;

void DwarfDieDecoder::AddPresenceCheck(llvm::dwarf::Attribute attribute,
                                       bool* present) {
  attrs_.emplace_back(
      attribute, [present](const llvm::DWARFFormValue&) { *present = true; });
}

void DwarfDieDecoder::AddUnsignedConstant(llvm::dwarf::Attribute attribute,
                                          llvm::Optional<uint64_t>* output) {
  attrs_.emplace_back(attribute, [output](const llvm::DWARFFormValue& form) {
    *output = form.getAsUnsignedConstant();
  });
}

void DwarfDieDecoder::AddSignedConstant(llvm::dwarf::Attribute attribute,
                                        llvm::Optional<int64_t>* output) {
  attrs_.emplace_back(attribute, [output](const llvm::DWARFFormValue& form) {
    *output = form.getAsSignedConstant();
  });
}

void DwarfDieDecoder::AddAddress(llvm::dwarf::Attribute attribute,
                                 llvm::Optional<uint64_t>* output) {
  attrs_.emplace_back(attribute, [output](const llvm::DWARFFormValue& form) {
    *output = form.getAsAddress();
  });
}

void DwarfDieDecoder::AddCString(llvm::dwarf::Attribute attribute,
                                 llvm::Optional<const char*>* output) {
  attrs_.emplace_back(attribute, [output](const llvm::DWARFFormValue& form) {
    *output = form.getAsCString();
  });
}

void DwarfDieDecoder::AddLineTableFile(llvm::dwarf::Attribute attribute,
                                       llvm::Optional<std::string>* output) {
  const llvm::DWARFDebugLine::LineTable* line_table =
      context_->getLineTableForUnit(unit_);
  const char* compilation_dir = unit_->getCompilationDir();
  if (line_table) {
    attrs_.emplace_back(attribute, [output, compilation_dir, line_table](
                                       const llvm::DWARFFormValue& form) {
      output->emplace();
      line_table->getFileNameByIndex(
          form.getAsUnsignedConstant().getValue(), compilation_dir,
          llvm::DILineInfoSpecifier::FileLineInfoKind::AbsoluteFilePath,
          output->getValue());
    });
  }
}

void DwarfDieDecoder::AddReference(llvm::dwarf::Attribute attribute,
                                   llvm::Optional<uint64_t>* unit_offset,
                                   llvm::Optional<uint64_t>* global_offset) {
  attrs_.emplace_back(attribute, [unit_offset, global_offset](
                                     const llvm::DWARFFormValue& form) {
    switch (form.getForm()) {
      case llvm::dwarf::DW_FORM_ref1:
      case llvm::dwarf::DW_FORM_ref2:
      case llvm::dwarf::DW_FORM_ref4:
      case llvm::dwarf::DW_FORM_ref8:
      case llvm::dwarf::DW_FORM_ref_udata:
        // A DWARF "form" is the way a value is encoded in the file. These
        // are all relative location of DIEs within the same unit.
        *unit_offset = form.getAsReferenceUVal();
        break;
      case llvm::dwarf::DW_FORM_ref_addr:
        // This is an absolute DIE address which can be used across units.
        *global_offset = form.getAsReferenceUVal();
        break;
      default:
        // Note that we don't handle DW_FORM_ref_sig8, DW_FORM_ref_sup4, or
        // DW_FORM_ref_sup8. The "sig8" one requries a different type encoding
        // that our Clang toolchain doesn't seem to generate. The "sup4/8" ones
        // require a shared separate symbol file we don't use.
        break;
    }
  });
}

void DwarfDieDecoder::AddReference(llvm::dwarf::Attribute attribute,
                                   llvm::DWARFDie* output) {
  attrs_.emplace_back(
      attribute, [this, output](const llvm::DWARFFormValue& form) {
        // See above version for comments.
        switch (form.getForm()) {
          case llvm::dwarf::DW_FORM_ref1:
          case llvm::dwarf::DW_FORM_ref2:
          case llvm::dwarf::DW_FORM_ref4:
          case llvm::dwarf::DW_FORM_ref8:
          case llvm::dwarf::DW_FORM_ref_udata: {
            auto ref_value = form.getAsReferenceUVal();
            if (ref_value)
              *output = unit_->getDIEForOffset(unit_->getOffset() + *ref_value);
            break;
          }
          case llvm::dwarf::DW_FORM_ref_addr: {
            auto ref_value = form.getAsReferenceUVal();
            if (ref_value)
              *output = unit_->getDIEForOffset(*ref_value);
            break;
          }
          default:
            // See above version for some comments.
            break;
        }
      });
}

void DwarfDieDecoder::AddFile(llvm::dwarf::Attribute attribute,
                              llvm::Optional<std::string>* output) {
  attrs_.emplace_back(
      attribute, [this, output](const llvm::DWARFFormValue& form) {
        llvm::Optional<uint64_t> file_index = form.getAsUnsignedConstant();
        if (!file_index)
          return;

        const llvm::DWARFDebugLine::LineTable* line_table =
            context_->getLineTableForUnit(unit_);
        const char* compilation_dir = unit_->getCompilationDir();

        std::string file_name;
        if (line_table->getFileNameByIndex(
                *file_index, compilation_dir,
                llvm::DILineInfoSpecifier::FileLineInfoKind::AbsoluteFilePath,
                file_name))
          *output = std::move(file_name);
      });
}

void DwarfDieDecoder::AddCustom(
    llvm::dwarf::Attribute attribute,
    std::function<void(const llvm::DWARFFormValue&)> callback) {
  attrs_.emplace_back(attribute, std::move(callback));
}

bool DwarfDieDecoder::Decode(const llvm::DWARFDie& die) {
  return Decode(*die.getDebugInfoEntry());
}

bool DwarfDieDecoder::Decode(const llvm::DWARFDebugInfoEntry& die) {
  // This indicates the abbreviation. Each DIE starts with an abbreviation
  // code.  The is the number that the DWARFAbbreviationDeclaration was derived
  // from above. We have to read it again to skip the offset over the number.
  //
  //  - A zero abbreviation code indicates a null DIE which is used to mark
  //    the end of a sequence of siblings.
  //
  //  - Otherwise this is a tag of an entry in the .debug_abbrev table (each
  //    entry in that table declares its own tag so it's not an index or an
  //    offset). The abbreviation entry indicates the attributes that this
  //    type of DIE contains, plus the data format for each.
  const llvm::DWARFAbbreviationDeclaration* abbrev =
      die.getAbbreviationDeclarationPtr();
  if (!abbrev)
    return false;

  uint32_t offset = die.getOffset();

  // Skip over the abbreviationcode. We don't actually need this (the abbrev
  // pointer above is derived from this) but we need to move offset past it.
  uint32_t abbr_code = extractor_.getULEB128(&offset);
  if (!abbr_code) {
    FXL_NOTREACHED();  // Should have gotten a null abbrev for this above.
    return false;
  }

  bool decoded_any = false;
  for (const llvm::DWARFAbbreviationDeclaration::AttributeSpec& spec :
       abbrev->attributes()) {
    bool decoded_current = false;
    llvm::DWARFFormValue form_value(spec.Form);

    for (const Dispatch& dispatch : attrs_) {
      if (spec.Attr != dispatch.first)
        continue;

      // Found the attribute, dispatch it and mark it read.
      form_value.extractValue(extractor_, &offset, unit_->getFormParams(),
                              unit_);
      dispatch.second(form_value);
      decoded_current = true;
      decoded_any = true;
      break;
    }

    if (!decoded_current) {
      // When the attribute wasn't read, skip over it to go to the next.
      form_value.skipValue(extractor_, &offset, unit_->getFormParams());
    }
  }
  return true;
}

}  // namespace zxdb
