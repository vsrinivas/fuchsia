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
