// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The implementation for Conversion and its various subclasses.
#include <vector>

#include "fidl/converter.h"

namespace fidl::conv {

std::string TypeConversion::Write(Syntax syntax) {
  std::string original = type_ctor_->copy_to_str();
  if (syntax == Syntax::kOld) {
    return prefix() + original;
  }

  std::string out = prefix();
  std::vector<std::string> constraints;

  // Certain wrapped types require special handling.
  if (original.find("array") == original.find_first_not_of(" \t")) {
    // This type must be of the form "array<TYPE, SIZE>"
    size_t bracket_pos = original.find('>');
    std::string arr = original.substr(0, bracket_pos);
    return out + "array<" + wrapped_type_text_ + "," + type_ctor_->maybe_size->copy_to_str() + ">";
  } else if (original.find("vector") == original.find_first_not_of(" \t")) {
    out += "vector<" + wrapped_type_text_ + ">";
  } else {
    out += type_ctor_->identifier->copy_to_str();
  }
  // TODO(azaslavsky): process other special cases (request<P>, etc).

  // Process various types of constraints in order.
  if (type_ctor_->nullability == types::Nullability::kNullable) {
    constraints.emplace_back("optional");
  }
  if (type_ctor_->maybe_size != nullptr) {
    constraints.emplace_back(type_ctor_->maybe_size->copy_to_str());
  }
  if (type_ctor_->handle_subtype_identifier != nullptr) {
    constraints.emplace_back(type_ctor_->handle_subtype_identifier->copy_to_str());
  }
  if (type_ctor_->handle_rights != nullptr) {
    constraints.emplace_back(type_ctor_->handle_rights->copy_to_str());
  }
  if (!constraints.empty() > 0) {
    std::string constraints_str = ":";
    if (constraints.size() == 1) {
      constraints_str += constraints[0];
    } else {
      bool first = true;
      for(const std::string& constraint : constraints){
        if (first) {
          constraints_str += "<";
        } else {
          constraints_str += ",";
        }
        first = false;
        constraints_str += constraint;
      }
      constraints_str += ">";
    }
    out += constraints_str;
  }
  return out;
};

std::string NameAndTypeConversion::Write(Syntax syntax) {
  std::string ctor = !type_text_.empty() ? type_text_ : type_ctor_->copy_to_str();
  if (syntax == Syntax::kOld) {
    return prefix() + ctor + " " + identifier_->copy_to_str();
  }

  return prefix() + identifier_->copy_to_str() + " " + ctor;
};

std::string MemberedDeclarationConversion::Write(Syntax syntax) {
  std::string out;
  if (syntax == Syntax::kOld) {
    out += prefix() + get_decl_str() + " " + identifier_->copy_to_str();
  } else {
    out += prefix() + "type " + identifier_->copy_to_str() + " = " + get_decl_str();
  }
  for (const std::string& member : members_) {
    out += member;
  }
  return out;
};

std::string BitsDeclarationConversion::Write(Syntax syntax) {
  std::string out;
  Token name_token = identifier_->start_;
  const char* start_pos = name_token.span().data().data();
  const char* end_pos = name_token.span().data().data() + name_token.span().data().length();
  std::string name = std::string(start_pos, end_pos);

  if (syntax == Syntax::kOld) {
    out += prefix() + get_decl_str() + " " + name + get_wrapped_type();
  } else {
    out += prefix() + "type " + name + " = " + get_decl_str() + get_wrapped_type();
  }
  for (const std::string& member : members_) {
    out += member;
  }
  return out;
};

}  // namespace fidl::conv
