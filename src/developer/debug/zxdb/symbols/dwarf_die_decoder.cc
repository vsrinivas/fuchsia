// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/dwarf_die_decoder.h"

#include <lib/syslog/cpp/macros.h>

#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/DebugInfo/DWARF/DWARFFormValue.h"
#include "src/developer/debug/zxdb/common/file_util.h"
#include "src/developer/debug/zxdb/symbols/const_value.h"

namespace zxdb {

namespace {

// The maximum nesting of abstract origin references we'll follow recursively
// before givin up. Prevents blowing out the stack for corrupt symbols.
constexpr int kMaxAbstractOriginRefsToFollow = 8;

}  // namespace

DwarfDieDecoder::DwarfDieDecoder(llvm::DWARFContext* context) : context_(context) {}

DwarfDieDecoder::~DwarfDieDecoder() = default;

void DwarfDieDecoder::AddPresenceCheck(llvm::dwarf::Attribute attribute, bool* present) {
  attrs_.emplace_back(
      attribute, [present](llvm::DWARFUnit*, const llvm::DWARFFormValue&) { *present = true; });
}

void DwarfDieDecoder::AddBool(llvm::dwarf::Attribute attribute, llvm::Optional<bool>* output) {
  attrs_.emplace_back(attribute, [output](llvm::DWARFUnit*, const llvm::DWARFFormValue& form) {
    *output = !!form.getAsUnsignedConstant();
  });
}

void DwarfDieDecoder::AddUnsignedConstant(llvm::dwarf::Attribute attribute,
                                          llvm::Optional<uint64_t>* output) {
  attrs_.emplace_back(attribute, [output](llvm::DWARFUnit*, const llvm::DWARFFormValue& form) {
    *output = form.getAsUnsignedConstant();
  });
}

void DwarfDieDecoder::AddSignedConstant(llvm::dwarf::Attribute attribute,
                                        llvm::Optional<int64_t>* output) {
  attrs_.emplace_back(attribute, [output](llvm::DWARFUnit*, const llvm::DWARFFormValue& form) {
    *output = form.getAsSignedConstant();
  });
}

void DwarfDieDecoder::AddAddress(llvm::dwarf::Attribute attribute,
                                 llvm::Optional<uint64_t>* output) {
  attrs_.emplace_back(attribute, [output](llvm::DWARFUnit*, const llvm::DWARFFormValue& form) {
    *output = form.getAsAddress();
  });
}

void DwarfDieDecoder::AddHighPC(llvm::Optional<HighPC>* output) {
  attrs_.emplace_back(llvm::dwarf::DW_AT_high_pc,
                      [output](llvm::DWARFUnit*, const llvm::DWARFFormValue& form) {
                        if (form.isFormClass(llvm::DWARFFormValue::FC_Constant)) {
                          auto as_constant = form.getAsUnsignedConstant();
                          if (as_constant)
                            *output = HighPC(true, *as_constant);
                        } else if (form.isFormClass(llvm::DWARFFormValue::FC_Address)) {
                          auto as_addr = form.getAsAddress();
                          if (as_addr)
                            *output = HighPC(false, *as_addr);
                        }
                      });
}

void DwarfDieDecoder::AddCString(llvm::dwarf::Attribute attribute,
                                 llvm::Optional<const char*>* output) {
  attrs_.emplace_back(attribute, [output](llvm::DWARFUnit*, const llvm::DWARFFormValue& form) {
    if (auto res = form.getAsCString(); res) {
      *output = *res;
    }
  });
}

void DwarfDieDecoder::AddLineTableFile(llvm::dwarf::Attribute attribute,
                                       llvm::Optional<std::string>* output) {
  attrs_.emplace_back(
      attribute, [this, output](llvm::DWARFUnit* unit, const llvm::DWARFFormValue& form) {
        const llvm::DWARFDebugLine::LineTable* line_table = context_->getLineTableForUnit(unit);
        if (!line_table)
          return;

        output->emplace();
        // Pass "" for the compilation directory so it doesn't rebase the file name. Our output
        // file names are always relative to the build (compilation) dir.
        line_table->getFileNameByIndex(
            form.getAsUnsignedConstant().value(), "",
            llvm::DILineInfoSpecifier::FileLineInfoKind::RelativeFilePath, output->value());
        output->value() = NormalizePath(output->value());
      });
}

void DwarfDieDecoder::AddConstValue(llvm::dwarf::Attribute attribute, ConstValue* output) {
  // The ConstValue already holds an "unset" state so we don't need an optional. Assume it's already
  // in the unset state when adding.
  attrs_.emplace_back(attribute, [output](llvm::DWARFUnit*, const llvm::DWARFFormValue& form) {
    if (form.getForm() == llvm::dwarf::DW_FORM_udata) {
      if (auto opt_unsigned = form.getAsUnsignedConstant()) {
        *output = ConstValue(*opt_unsigned);
        return;
      }
    } else if (form.getForm() == llvm::dwarf::DW_FORM_sdata) {
      if (auto opt_signed = form.getAsSignedConstant()) {
        *output = ConstValue(static_cast<int64_t>(*opt_signed));
        return;
      }
    } else if (form.isFormClass(llvm::DWARFFormValue::FC_Block)) {
      llvm::ArrayRef<uint8_t> block = *form.getAsBlock();
      if (block.size() > 0) {
        *output = ConstValue(std::vector<uint8_t>(block.data(), block.data() + block.size()));
        return;
      }
    }
  });
}

void DwarfDieDecoder::AddSectionOffset(llvm::dwarf::Attribute attribute,
                                       llvm::Optional<uint64_t>* offset) {
  // The returned section offset will be the raw value. The caller will have to look up the
  // address of the elf section it references and interpret it accordingly.
  attrs_.emplace_back(attribute, [offset](llvm::DWARFUnit*, const llvm::DWARFFormValue& form) {
    // The getAsSectionOffset() call will return "None" if the form class doesn't match, so we don't
    // need to check also.
    *offset = form.getAsSectionOffset();
  });
}

void DwarfDieDecoder::AddBlock(llvm::dwarf::Attribute attribute,
                               llvm::Optional<std::vector<uint8_t>>* dest) {
  attrs_.emplace_back(attribute, [dest](llvm::DWARFUnit*, const llvm::DWARFFormValue& form) {
    if (form.isFormClass(llvm::DWARFFormValue::FC_Block) ||
        form.isFormClass(llvm::DWARFFormValue::FC_Exprloc)) {
      llvm::ArrayRef<uint8_t> block = *form.getAsBlock();
      *dest = std::vector<uint8_t>(block.begin(), block.end());
    }
  });
}

void DwarfDieDecoder::AddReference(llvm::dwarf::Attribute attribute, llvm::DWARFDie* output) {
  attrs_.emplace_back(attribute,
                      [this, output](llvm::DWARFUnit* unit, const llvm::DWARFFormValue& form) {
                        *output = DecodeReference(unit, form);
                      });
}

void DwarfDieDecoder::AddFile(llvm::dwarf::Attribute attribute,
                              llvm::Optional<std::string>* output) {
  attrs_.emplace_back(
      attribute, [this, output](llvm::DWARFUnit* unit, const llvm::DWARFFormValue& form) {
        llvm::Optional<uint64_t> file_index = form.getAsUnsignedConstant();
        if (!file_index)
          return;

        const llvm::DWARFDebugLine::LineTable* line_table = context_->getLineTableForUnit(unit);
        if (!line_table)
          return;

        // Pass "" for the compilation directory so it doesn't rebase the file name. Our output file
        // names are always relative to the build (compilation) dir.
        std::string file_name;
        if (line_table->getFileNameByIndex(
                *file_index, "", llvm::DILineInfoSpecifier::FileLineInfoKind::RelativeFilePath,
                file_name)) {
          *output = NormalizePath(file_name);
        }
      });
}

void DwarfDieDecoder::AddAbstractParent(llvm::DWARFDie* output) {
  FX_DCHECK(!abstract_parent_);
  abstract_parent_ = output;
}

void DwarfDieDecoder::AddCustom(llvm::dwarf::Attribute attribute, AttributeHandler callback) {
  attrs_.emplace_back(attribute, std::move(callback));
}

bool DwarfDieDecoder::Decode(const llvm::DWARFDie& die) {
  seen_attrs_.clear();
  return DecodeInternal(die, kMaxAbstractOriginRefsToFollow);
}

bool DwarfDieDecoder::DecodeInternal(const llvm::DWARFDie& die,
                                     int abstract_origin_refs_to_follow) {
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
  const llvm::DWARFAbbreviationDeclaration* abbrev = die.getAbbreviationDeclarationPtr();
  if (!abbrev)
    return false;

  llvm::DWARFUnit* unit = die.getDwarfUnit();
  llvm::DWARFDataExtractor extractor = unit->getDebugInfoExtractor();
  uint64_t offset = die.getOffset();

  // Skip over the abbreviationcode. We don't actually need this (the abbrev
  // pointer above is derived from this) but we need to move offset past it.
  uint64_t abbr_code = extractor.getULEB128(&offset);
  if (!abbr_code) {
    FX_NOTREACHED();  // Should have gotten a null abbrev for this above.
    return false;
  }

  // Set when we encounter an abstract origin attribute.
  llvm::DWARFDie abstract_origin;

  for (const llvm::DWARFAbbreviationDeclaration::AttributeSpec& spec : abbrev->attributes()) {
    // Set to true when the form_value has been decoded. Otherwise, the value
    // needs to be skipped to advance through the data.
    bool decoded_current = false;
    llvm::DWARFFormValue form_value(spec.Form);

    // Tracks if the current attribute should be looked up and dispatched.
    // This loop doesn't return early so the form_value.skipValue() call at the
    // bottom will be called when necessary (otherwise the loop won't advance).
    bool needs_dispatch = true;

    if (spec.Attr == llvm::dwarf::DW_AT_abstract_origin) {
      // Abtract origins are handled after loop completion. Explicitly don't
      // check for duplicate attributes in this case so we can follow more than
      // one link in the chain.
      form_value.extractValue(extractor, &offset, unit->getFormParams(), unit);
      abstract_origin = DecodeReference(unit, form_value);
      decoded_current = true;
    } else {
      // Track attributes that we've already seen and don't decode duplicates
      // (most DIEs won't have duplicates, this is for when we recursively
      // underlay values following abstract origins). This is brute-force
      // because the typical number of attributes is small enough that this
      // should be more efficient than a set which requires per-element heap
      // allocations.
      if (std::find(seen_attrs_.begin(), seen_attrs_.end(), spec.Attr) != seen_attrs_.end())
        needs_dispatch = false;
      else
        seen_attrs_.push_back(spec.Attr);
    }

    if (needs_dispatch) {
      // Check for a handler for this attribute and dispatch it.
      for (const Dispatch& dispatch : attrs_) {
        if (spec.Attr != dispatch.first)
          continue;

        // Found the attribute, dispatch it and mark it read.
        if (!decoded_current) {
          if (spec.isImplicitConst()) {
            // In the "implicit const" form, the value is stored in the abbreviation declaration
            // which is not passed in to the DWARFFormValue constructor or its extractValue()
            // member. So this form has to be specially decoded.
            form_value =
                llvm::DWARFFormValue::createFromSValue(spec.Form, spec.getImplicitConstValue());
          } else {
            form_value.extractValue(extractor, &offset, unit->getFormParams(), unit);
          }
          decoded_current = true;
        }
        dispatch.second(unit, form_value);
        break;
      }
    }

    if (!decoded_current) {
      // When the attribute wasn't read, skip over it to go to the next.
      form_value.skipValue(extractor, &offset, unit->getFormParams());
    }
  }

  // Recursively decode abstract origins. The attributes on the abstract origin
  // DIE "underlay" any attributes present on the current one.
  if (abstract_origin.isValid() && abstract_origin_refs_to_follow > 0) {
    return DecodeInternal(abstract_origin, abstract_origin_refs_to_follow - 1);
  } else {
    // The deepest DIE in the abstract origin chain was found (which will be the original DIE itself
    // if there was no abstract origin).
    if (abstract_parent_)
      *abstract_parent_ = die.getParent();
  }

  return true;
}

llvm::DWARFDie DwarfDieDecoder::DecodeReference(llvm::DWARFUnit* unit,
                                                const llvm::DWARFFormValue& form) {
  switch (form.getForm()) {
    case llvm::dwarf::DW_FORM_ref1:
    case llvm::dwarf::DW_FORM_ref2:
    case llvm::dwarf::DW_FORM_ref4:
    case llvm::dwarf::DW_FORM_ref8:
    case llvm::dwarf::DW_FORM_ref_udata: {
      // A DWARF "form" is the way a value is encoded in the file. These
      // are all relative location of DIEs within the same unit.
      auto ref_value = form.getAsReferenceUVal();
      if (ref_value)
        return unit->getDIEForOffset(unit->getOffset() + *ref_value);
      break;
    }
    case llvm::dwarf::DW_FORM_ref_addr: {
      // This is an absolute DIE address which can be used across units.
      auto ref_value = form.getAsReferenceUVal();
      if (ref_value)
        return context_->getDIEForOffset(*ref_value);
      break;
    }
    default:
      // Note that we don't handle DW_FORM_ref_sig8, DW_FORM_ref_sup4, or
      // DW_FORM_ref_sup8. The "sig8" one requries a different type encoding
      // that our Clang toolchain doesn't seem to generate. The "sup4/8" ones
      // require a shared separate symbol file we don't use.
      //
      // TODO(fxbug.dev/97388): Support DW_AT_signature and DW_FORM_ref_sig8.
      break;
  }
  return llvm::DWARFDie();
}

}  // namespace zxdb
