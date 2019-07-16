// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/template_parameter.h"

namespace zxdb {

TemplateParameter::TemplateParameter(const std::string& name, LazySymbol type, bool is_value)
    : Symbol(is_value ? DwarfTag::kTemplateValueParameter : DwarfTag::kTemplateTypeParameter),
      name_(name),
      type_(std::move(type)) {}

TemplateParameter::~TemplateParameter() = default;

const std::string& TemplateParameter::GetAssignedName() const { return name_; }

Identifier TemplateParameter::ComputeIdentifier() const {
  // This is a simple one-word name so we have to provide a custom implementation because the
  // default one qualifies the assigned name with namespaces and such.
  return Identifier(IdentifierQualification::kRelative, IdentifierComponent(name_));
}

}  // namespace zxdb
