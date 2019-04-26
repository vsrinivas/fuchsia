// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_ORDINALS_H_
#define ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_ORDINALS_H_

#include "raw_ast.h"

namespace fidl {
namespace ordinals {

// Returns the Selector. If the Selector attribute is present, the
// function returns its value; otherwise, it returns the name parameter.
std::string GetSelector(const raw::AttributeList* attributes,
                        SourceLocation name);

// Computes the ordinal for this method.
//
// The ordinal value is equal to
//
//    *((int32_t *)sha256(library_name + "." + interface_name + "/" + method_name)) & 0x7fffffff;
//
// If |method| has an Selector attribute, that value will be used as the
// method_name.
raw::Ordinal GetGeneratedOrdinal(const std::vector<std::string_view>& library_name,
                                 const std::string_view& interface_name,
                                 const raw::InterfaceMethod& method);

// Retrieves the correct ordinal for this method.
//
// If |method.ordinal| is not null, this method will return |method.ordinal|.
// Otherwise, the ordinal value is computed with GetGeneratedOrdinal.
raw::Ordinal GetOrdinal(const std::vector<std::string_view>& library_name,
                        const std::string_view& interface_name,
                        const raw::InterfaceMethod& method);

// Retrieves the correct ordinal for |xunion_member|, following the same
// algorithm as GetOrdinal() for interface methods above.
raw::Ordinal GetOrdinal(const std::vector<std::string_view>& library_name,
                        const std::string_view& xunion_declaration_name,
                        const raw::XUnionMember& xunion_member);

} // namespace ordinals
} // namespace fidl

#endif // ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_ORDINALS_H_
