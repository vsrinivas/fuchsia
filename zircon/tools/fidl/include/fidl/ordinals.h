// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_ORDINALS_H_
#define ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_ORDINALS_H_

#include "raw_ast.h"

namespace fidl {
namespace ordinals {

// Returns the Selector. If the Selector attribute is present, the
// function returns its value; otherwise, it returns the name parameter.
std::string GetSelector(const raw::AttributeList* attributes, SourceSpan name);

// Computes the 32bits ordinal for this |method|.
//
// The ordinal value is equal to
//
//    *((int32_t *)sha256(library_name + "." + protocol_name + "/" + selector_name)) & 0x7fffffff;
//
// Note: the slash separator is between the protocol_name and selector_name.
//
// The selector_name is retrieved using GetSelector.
raw::Ordinal32 GetGeneratedOrdinal32(const std::vector<std::string_view>& library_name,
                                     const std::string_view& protocol_name,
                                     const raw::ProtocolMethod& method);

// Computes the 32bits ordinal for this |xunion_member|.
//
// The 32bits ordinal value is equal to
//
//    *((int32_t *)sha256(library_name + "." + xunion_declaration_name + "/" + selector_name)) &
//    0x7fffffff;
//
// Note: the slash separator is between the xunion_declaration_name and selector_name.
//
// The selector_name is retrieved using GetSelector.
raw::Ordinal32 GetGeneratedOrdinal32(const std::vector<std::string_view>& library_name,
                                     const std::string_view& xunion_declaration_name,
                                     const raw::XUnionMember& xunion_member);

// Computes the 32bits ordinal for this |union_member| as if it were a union member.
// This is used for the union to xunion migration to produce the xunion_ordinal field for unions.
raw::Ordinal32 GetGeneratedOrdinal32(const std::vector<std::string_view>& library_name,
                                     const std::string_view& union_declaration_name,
                                     const raw::UnionMember& union_member);

// Computes the 32bits ordinal from components.
raw::Ordinal32 GetGeneratedOrdinal32(const std::vector<std::string_view>& library_name,
                                     const std::string_view container_name,
                                     const std::string union_member_name,
                                     const raw::SourceElement& source_element);

// Computes the 64bits ordinal for this |method|.
//
// The ordinal value is equal to
//
//    *((int64_t *)sha256(library_name + "/" + protocol_name + "." + selector_name)) &
//    0x7fffffffffffffff;
//
// Note: the slash separator is between the library_name and protocol_name.
//
// The selector_name is retrieved using GetSelector.
raw::Ordinal64 GetGeneratedOrdinal64(const std::vector<std::string_view>& library_name,
                                     const std::string_view& protocol_name,
                                     const raw::ProtocolMethod& method);

}  // namespace ordinals
}  // namespace fidl

#endif  // ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_ORDINALS_H_
