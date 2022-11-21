// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/resolve_variant.h"

#include <inttypes.h>

#include "src/developer/debug/zxdb/expr/eval_context.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"
#include "src/developer/debug/zxdb/expr/resolve_collection.h"
#include "src/developer/debug/zxdb/symbols/data_member.h"
#include "src/developer/debug/zxdb/symbols/variant.h"
#include "src/developer/debug/zxdb/symbols/variant_part.h"

namespace zxdb {

namespace {

// Gets the active variant for the given value and extracts the single data member inside of it.
// Effectively, this gets the DataMember corresponding to the active value of a Rust enum.
//
// If the value isn't a variant or there isn't a single data member inside of it, returns an error.
// Rust enums currently look like this (this is an Option):
//
//   DW_TAG_structure_type
//     DW_AT_name ("Option<alloc::sync::Arc<fidl::client::ClientInner>>")
//     DW_AT_byte_size (0x08)
//     DW_AT_alignment (8)
//
//     DW_TAG_variant_part
//       // Disciminant (which enum value is active).
//       DW_AT_discr
//       DW_TAG_member  <==== The DW_AT_discr value refers to this record.
//         DW_AT_type (0x0000d042 "u64")
//         DW_AT_alignment (8)
//         DW_AT_data_member_location (0x00)
//         DW_AT_artificial (true)
//
//       // Definition for the "None" variant.
//       DW_TAG_variant
//         DW_AT_discr_value (0x00)
//         DW_TAG_member
//           DW_AT_name ("None")
//           DW_AT_type (Reference to the "None" member structure defined below)
//           DW_AT_alignment (8)
//           DW_AT_data_member_location (0x00)
//
//       // Definition for the "Some" variant. Note this starts at 0 offset which overlaps the
//       // discriminant, but that's OK because the "Some" structure defined below has 8 bytes
//       // of padding at the beginning.
//       DW_TAG_variant
//         DW_TAG_member
//           DW_AT_name ("Some")
//           DW_AT_type (Reference to the "Some" member structure defined below)
//           DW_AT_alignment (8)
//           DW_AT_data_member_location (0x00)
//
//     // Type of data for the contents of the "None" data. This contains no members.
//     DW_TAG_structure_type
//       DW_AT_name ("None")
//       DW_AT_byte_size (0x08)
//       DW_AT_alignment (8)
//       DW_TAG_template_type_parameter
//         DW_AT_type (0x00003d77 "alloc::sync::Arc<fidl::client::ClientInner>")
//         DW_AT_name ("T")
//
//     // Type of data for the contents of the "Some" data.
//     DW_TAG_structure_type
//       DW_AT_name ("Some")
//       DW_AT_byte_size (0x08)
//       DW_AT_alignment (8)
//       DW_TAG_template_type_parameter
//         DW_AT_type (0x00003d77 "alloc::sync::Arc<fidl::client::ClientInner>")
//         DW_AT_name ("T")
//
//       // Actual data of the "Some".
//       DW_TAG_member
//         DW_AT_name ("__0")
//         DW_AT_type (0x00003d77 "alloc::sync::Arc<fidl::client::ClientInner>")
//         DW_AT_alignment (8)
//         DW_AT_data_member_location (0x00)
//
// So this function will return the DW_TAG_member (of either "Some" or "None" structure type) inside
// of the DW_TAG_variant that's active, as indicated by the discriminant.
//
// In the non-error case, this will always return a valid data member (it won't be is_null()).
ErrOr<FoundMember> GetSingleActiveDataMember(const fxl::RefPtr<EvalContext>& context,
                                             const ExprValue& value) {
  fxl::RefPtr<Type> concrete = context->GetConcreteType(value.type());
  if (!concrete)
    return Err("Missing type information.");
  const Collection* collection = concrete->As<Collection>();
  if (!collection)
    return Err("Attempting to extract a variant from a non-collection.");

  const VariantPart* part = collection->variant_part().Get()->As<VariantPart>();
  if (!part)
    return Err("Missing variant part for variant.");

  fxl::RefPtr<Variant> variant;
  if (Err err = ResolveVariant(context, value, collection, part, &variant); err.has_error())
    return err;

  // Extract the one expected data member.
  if (variant->data_members().size() > 1)
    return Err("Expected a single variant data member, got %zu.", variant->data_members().size());
  const DataMember* member = variant->data_members()[0].Get()->As<DataMember>();
  if (!member)
    return Err("Invalid data member in variant symbol.");

  return FoundMember(collection, member);
}

}  // namespace

Err ResolveVariant(const fxl::RefPtr<EvalContext>& context, const ExprValue& value,
                   const Collection* collection, const VariantPart* variant_part,
                   fxl::RefPtr<Variant>* result) {
  // Resolve the discriminant value. It is effectively a member of the enclosing structure.
  const DataMember* discr_member = variant_part->discriminant().Get()->As<DataMember>();
  if (!discr_member)
    return Err("Missing discriminant for variant.");

  // Variants don't have static variant members or virtual inheritance.
  ErrOrValue discr_value =
      ResolveNonstaticMember(context, value, FoundMember(collection, discr_member));
  if (discr_value.has_error())
    return discr_value.err();

  // Expect the discriminant value to resolve to a <= 64-bit number.
  //
  // NOTE: there is some trickery with signed/unsigned values as described in the
  // Variant.discr_value() getter. If we need to support signed discriminants this block will have
  // to be updated.
  uint64_t discr = 0;
  if (Err err = discr_value.value().PromoteTo64(&discr); err.has_error())
    return err;

  // Check against all variants and also look for the default variant.
  const Variant* default_var = nullptr;
  for (const auto& lazy_var : variant_part->variants()) {
    const Variant* var = lazy_var.Get()->As<Variant>();
    if (!var)
      continue;

    if (var->discr_value()) {
      if (*var->discr_value() == discr) {
        // Found match.
        *result = RefPtrTo(var);
        return Err();
      }
    } else {
      // No discriminant value set on the variant means it's the default one.
      default_var = var;
    }
  }

  // No match means use the default value if there is one.
  if (default_var) {
    *result = RefPtrTo(default_var);
    return Err();
  }

  return Err("Discriminant value of 0x%" PRIx64 " does not match any of the Variants.", discr);
}

ErrOr<std::string> GetActiveRustVariantName(const fxl::RefPtr<EvalContext>& context,
                                            const ExprValue& value) {
  ErrOr<FoundMember> found_member = GetSingleActiveDataMember(context, value);
  if (found_member.has_error())
    return found_member.err();
  FX_DCHECK(!found_member.value().is_null());  // Should always be valid in non-error cases.

  // The name of the enum in Rust is the name of the data member.
  return found_member.value().data_member()->GetAssignedName();
}

ErrOrValue ResolveSingleVariantValue(const fxl::RefPtr<EvalContext>& context,
                                     const ExprValue& value) {
  fxl::RefPtr<Type> concrete = context->GetConcreteType(value.type());
  if (!concrete)
    return Err("Missing type information.");
  const Collection* collection = concrete->As<Collection>();
  if (!collection)
    return Err("Attempting to extract a variant from a non-collection.");

  const VariantPart* part = collection->variant_part().Get()->As<VariantPart>();
  if (!part)
    return Err("Missing variant part for variant.");

  fxl::RefPtr<Variant> variant;
  if (Err err = ResolveVariant(context, value, collection, part, &variant); err.has_error())
    return err;

  if (variant->data_members().empty())
    return ExprValue();  // Allow empty.

  // Extract the one expected data member.
  if (variant->data_members().size() > 1)
    return Err("Expected a single variant data member, got %zu.", variant->data_members().size());
  const DataMember* member = variant->data_members()[0].Get()->As<DataMember>();
  if (!member)
    return Err("Invalid data member in variant symbol.");

  return ResolveNonstaticMember(context, value, FoundMember(collection, member));
}

}  // namespace zxdb
