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
  std::string id = type_ctor_->identifier->copy_to_str();

  // Nullability is the first constraint.
  if (type_ctor_->nullability == types::Nullability::kNullable) {
    // Special case: types whose underlying type resolves to "struct" need
    // to be wrapped in "box<...>"
    if (underlying_type_.kind() == UnderlyingType::Kind::kStruct) {
      id = "box<" + id + ">";
    }
    constraints.emplace_back("optional");
  }

  // Certain wrapped types require special handling.
  if (!underlying_type_.is_behind_alias()) {
    if (underlying_type_.kind() == UnderlyingType::Kind::kArray) {
      // This type must be of the form "array<TYPE, SIZE>" and cannot have other
      // constraints, so return early.
      std::string size;
      if (type_ctor_->maybe_size != nullptr) {
        size = type_ctor_->maybe_size->copy_to_str();
      }
      return out + "array<" + wrapped_type_text_ + "," + size + ">";
    } else if (underlying_type_.kind() == UnderlyingType::Kind::kRequestHandle) {
      // Strip the prefix "client_end:" from the wrapped text, then use it as
      // a constraint on the server_end instead.
      //
      // This must be done because when converting a type like "request<P>" it
      // is always the case that P will be visited and converted first, and will
      // thus be passed to this converter as the wrapped_text "client_end:P," if
      // P is not an alias.  To make it into a valid server_end in the unaliased
      // case, we need to replace the "client_end" string with "server_end"
      // instead.
      std::string ptype = wrapped_type_text_;
      size_t colon_pos = ptype.find(':');
      if (colon_pos != std::string::npos) {
        // We want the string after the colon, so increment by one.
        colon_pos++;
        ptype = ptype.substr(colon_pos, ptype.length() - colon_pos);
      }
      constraints.emplace_back(ptype);
      id = "server_end";
    } else if (underlying_type_.kind() == UnderlyingType::Kind::kProtocol) {
      constraints.emplace_back(id);
      id = "client_end";
    } else if (underlying_type_.kind() == UnderlyingType::Kind::kVector) {
      if (wrapped_type_text_.empty()) {
        // Special case: bytes is a builtin alias for vector<uint8>.
        id = "bytes";
      } else {
        id = "vector<" + wrapped_type_text_ + ">";
      }
    }
  }
  out += id;

  // Process the remaining constraints in display order.
  if (type_ctor_->maybe_size != nullptr) {
    constraints.emplace_back(type_ctor_->maybe_size->copy_to_str());
  }
  if (type_ctor_->handle_subtype_identifier != nullptr) {
    constraints.emplace_back(type_ctor_->handle_subtype_identifier->copy_to_str());
  }
  if (type_ctor_->handle_rights != nullptr) {
    constraints.emplace_back(type_ctor_->handle_rights->copy_to_str());
  }

  // Build and append the constraints list.
  if (!constraints.empty() > 0) {
    std::string constraints_str = ":";
    if (constraints.size() == 1) {
      constraints_str += constraints[0];
    } else {
      bool first = true;
      for (const std::string& constraint : constraints) {
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
