// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_TYPESPACE_H_
#define TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_TYPESPACE_H_

#include "tools/fidl/fidlc/include/fidl/flat/name.h"
#include "tools/fidl/fidlc/include/fidl/flat/types.h"
#include "tools/fidl/fidlc/include/fidl/flat_ast.h"
#include "tools/fidl/fidlc/include/fidl/reporter.h"

namespace fidl::flat {

constexpr uint32_t kHandleSameRights = 0x80000000;  // ZX_HANDLE_SAME_RIGHTS

class TypeResolver;
struct LayoutInvocation;
struct LayoutParameterList;
struct TypeConstraints;

// Typespace provides builders for all types (e.g. array, vector, string), and
// ensures canonicalization, i.e. the same type is represented by one object,
// shared amongst all uses of said type. For instance, while the text
// `vector<uint8>:7` may appear multiple times in source, these all indicate
// the same type.
//
// TODO(fxbug.dev/76219): Implement canonicalization.
class Typespace final : private ReporterMixin {
 public:
  // Initializes the typespace with builtin types from the root library.
  explicit Typespace(const Library* root_library, Reporter* reporter);
  Typespace(const Typespace&) = delete;
  Typespace(Typespace&&) = default;

  const Type* Create(TypeResolver* resolver, const Reference& layout,
                     const LayoutParameterList& parameters, const TypeConstraints& constraints,
                     LayoutInvocation* out_params);

  const PrimitiveType* GetPrimitiveType(types::PrimitiveSubtype subtype);
  const InternalType* GetInternalType(types::InternalSubtype subtype);
  const Type* GetUnboundedStringType();
  const Type* GetStringType(size_t max_size);
  const Type* GetUntypedNumericType();

 private:
  class Creator;

  const Type* Intern(std::unique_ptr<Type> type);

  std::vector<std::unique_ptr<Type>> types_;
  std::map<types::PrimitiveSubtype, std::unique_ptr<PrimitiveType>> primitive_types_;
  std::map<types::InternalSubtype, std::unique_ptr<InternalType>> internal_types_;
  std::unique_ptr<StringType> unbounded_string_type_;
  std::unique_ptr<UntypedNumericType> untyped_numeric_type_;
  std::vector<std::unique_ptr<Size>> sizes_;
  std::optional<Name> vector_layout_name_;
  std::optional<Name> pointer_type_name_;
};

}  // namespace fidl::flat

#endif  // TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_TYPESPACE_H_
