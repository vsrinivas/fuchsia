// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/flat/name.h"

namespace fidl::flat {

std::shared_ptr<NamingContext> NamingContext::Create(const Name& decl_name) {
  assert(decl_name.span().has_value() && "cannot have a naming context from a name without a span");
  return Create(decl_name.span().value());
}

Name NamingContext::ToName(Library* library, SourceSpan declaration_span) {
  if (parent_ == nullptr)
    return Name::CreateSourced(library, name_);
  return Name::CreateAnonymous(library, declaration_span, shared_from_this());
}

}  // namespace fidl::flat
