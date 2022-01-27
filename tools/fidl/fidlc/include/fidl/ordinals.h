// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDL_FIDLC_INCLUDE_FIDL_ORDINALS_H_
#define TOOLS_FIDL_FIDLC_INCLUDE_FIDL_ORDINALS_H_

#include <lib/fit/function.h>

#include <string_view>

#include "fidl/source_span.h"

namespace fidl::raw {
class Ordinal64;
class SourceElement;
}  // namespace fidl::raw

namespace fidl::flat {
struct AttributeList;
}  // namespace fidl::flat

namespace fidl::ordinals {

using MethodHasher = fit::function<raw::Ordinal64(
    const std::vector<std::string_view>& library_name, const std::string_view& protocol_name,
    const std::string_view& selector_name, const raw::SourceElement& source_element)>;

// Returns the selector. If the @selector attribute is present, the
// function returns its value; otherwise, it returns the name parameter.
std::string GetSelector(const flat::AttributeList* attributes, SourceSpan name);

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
                                     const std::string_view& selector_name,
                                     const raw::SourceElement& source_element);

}  // namespace fidl::ordinals

#endif  // TOOLS_FIDL_FIDLC_INCLUDE_FIDL_ORDINALS_H_
