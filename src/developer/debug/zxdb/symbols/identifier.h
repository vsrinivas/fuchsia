// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_IDENTIFIER_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_IDENTIFIER_H_

#include <string>
#include <utility>
#include <vector>

#include "src/developer/debug/zxdb/symbols/identifier_base.h"

namespace zxdb {

// A simple identifier component consisting only of an opaque string.
class IdentifierComponent {
 public:
  IdentifierComponent();
  explicit IdentifierComponent(std::string name) : name_(std::move(name)) {}
  IdentifierComponent(SpecialIdentifier si, std::string name = std::string())
      : special_(si), name_(std::move(name)) {}

  bool operator==(const IdentifierComponent& other) const {
    return special_ == other.special_ && name_ == other.name_;
  }
  bool operator!=(const IdentifierComponent& other) const { return !operator==(other); }

  SpecialIdentifier special() const { return special_; }
  const std::string& name() const { return name_; }

  std::string GetName(bool include_debug) const;

 private:
  SpecialIdentifier special_ = SpecialIdentifier::kNone;
  std::string name_;
};

// An identifier consisting of a sequence of opaque names.
//
// Code in the symbols directory must use this identifier type since no assumptions can be made
// about what the compiler has generated in the symbol file. Some symbols like lambdas can have
// compiler-internally-generated names which are not parseable in the language of the compilation
// unit.
//
// See also "ParsedIdentifier" in the "expr" library which adds more parsing when possible.
using Identifier = IdentifierBase<IdentifierComponent>;

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_IDENTIFIER_H_
