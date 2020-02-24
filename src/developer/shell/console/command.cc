// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/console/command.h"

#include <stdlib.h>

#include <regex>
#include <string_view>
#include <vector>

namespace shell::console {

Command::Command() = default;

Command::~Command() = default;

namespace {

// Temporary parser.  Delete when we have something good.

bool IsIdentifierChar(char ch) { return isalnum(ch) || ch == '_'; }

class Marker {
 public:
  Marker(const std::string& str) : source_(str) {}
  Marker(const Marker& m, size_t offset) : source_(m.source_.data() + offset) {}

  Marker MatchNextIdentifier(const std::string& keyword);
  Marker MatchAnyIdentifier(std::string* identifier);
  Marker MatchSpecial(const std::string& special);
  Marker MatchIntegerLiteral(int64_t* val);
  const char* data() { return source_.data(); }
  bool operator==(const Marker& other) { return other.source_.data() == source_.data(); }

 private:
  std::string_view source_;
};

Marker Marker::MatchNextIdentifier(const std::string& keyword) {
  size_t pos = 0;
  while (std::isspace(source_[pos])) {
    pos++;
  }
  if (source_.find(keyword, pos) == pos &&
      (pos == source_.length() || !IsIdentifierChar(source_[pos + keyword.length()]))) {
    return Marker(*this, pos + keyword.length());
  }
  return *this;
}

Marker Marker::MatchAnyIdentifier(std::string* identifier) {
  size_t pos = 0;
  while (std::isspace(source_[pos])) {
    pos++;
  }
  if (isdigit(source_[pos] || !IsIdentifierChar(source_[pos]))) {
    return *this;
  }
  while (IsIdentifierChar(source_[pos])) {
    identifier->push_back(source_[pos]);
    pos++;
  }
  return Marker(*this, pos);
}

Marker Marker::MatchSpecial(const std::string& special) {
  size_t pos = 0;
  while (std::isspace(source_[pos])) {
    pos++;
  }
  if (source_.find(special, pos) != pos) {
    return *this;
  }
  return Marker(*this, pos + special.length());
}

Marker Marker::MatchIntegerLiteral(int64_t* val) {
  size_t pos = 0;
  while (std::isspace(source_[pos])) {
    pos++;
  }

  std::string number;
  const char* begin = source_.data() + pos;
  const char* curr = begin;
  while (isdigit(*curr)) {
    number += *curr;
    curr++;
  }
  if (*curr == '_') {
    if (curr - begin > 3) {
      return *this;
    }
    do {
      ++curr;
      for (int i = 0; i < 3; i++) {
        if (!isdigit(*curr)) {
          return *this;
        }
        number += *curr;
        curr++;
      }
    } while (*curr == '_');
  }
  if (curr == begin) {
    return *this;
  }
  size_t idx;
  *val = std::stoll(number, &idx, 10);
  if (errno == ERANGE || idx == source_.length()) {
    return *this;
  }
  return Marker(*this, idx);
}

// The result of a parsing step.  Contains an updated position (marker()), the node_id of the
// topmost node (node_id()), the type of that node (type()), and whether there was an error
// (result()).
class ParseResult {
 public:
  ParseResult(const Marker& marker, const Err& err, llcpp::fuchsia::shell::ShellType&& type,
              uint64_t node_id)
      : marker_(marker), err_(err), type_(std::move(type)), node_id_(node_id) {}
  Marker marker() { return marker_; }
  Err result() { return err_; }
  llcpp::fuchsia::shell::ShellType type() { return std::move(type_); }
  uint64_t node_id() { return node_id_; }

 private:
  Marker marker_;
  Err err_;
  llcpp::fuchsia::shell::ShellType type_;
  uint64_t node_id_;
};

class Parser {
 public:
  Parser(AstBuilder* builder) : builder_(builder) {}
  ParseResult ParseValue(Marker m) {
    // int64 Integer literals for now.
    int64_t i;
    Marker after_int = m.MatchIntegerLiteral(&i);
    if (after_int == m) {
      return ParseResult(m, Err(kBadParse, "Integer not found"), builder_->undef(), 0);
    }

    uint64_t node_id = builder_->AddIntegerLiteral(i);

    llcpp::fuchsia::shell::BuiltinType type = llcpp::fuchsia::shell::BuiltinType::INTEGER;
    llcpp::fuchsia::shell::BuiltinType* type_ptr = builder_->ManageCopyOf(&type);

    return ParseResult(after_int, Err(),
                       llcpp::fuchsia::shell::ShellType::WithBuiltinType(fidl::unowned(type_ptr)),
                       node_id);
  }

  ParseResult ParseSimpleExpression(Marker m) {
    // Skip some levels for now
    return ParseValue(m);
  }

  ParseResult ParseExpression(Marker m) { return ParseSimpleExpression(m); }

  ParseResult ParseVariableDecl(Marker m) {
    bool is_const = false;
    Marker end_var = m.MatchNextIdentifier("var");
    if (end_var == m) {
      end_var = m.MatchNextIdentifier("const");
      if (end_var == m) {
        return ParseResult(m, Err(kBadParse, "Keyword var or const not found"), builder_->undef(),
                           0);
      }
      is_const = true;
    }
    std::string identifier;
    Marker end_ident = end_var.MatchAnyIdentifier(&identifier);
    if (end_ident == end_var) {
      return ParseResult(m, Err(kBadParse, "Identifier not found"), builder_->undef(), 0);
    }
    Marker end_equals = end_ident.MatchSpecial("=");
    if (end_equals == end_ident) {
      return ParseResult(m, Err(kBadParse, "'=' not found"), builder_->undef(), 0);
    }
    ParseResult result = ParseExpression(end_equals);
    if (!result.result().ok()) {
      return result;
    }
    uint64_t node_id =
        builder_->AddVariableDeclaration(identifier, result.type(), result.node_id(), is_const);
    return ParseResult(result.marker(), Err(), builder_->undef(), node_id);
  }

 private:
  AstBuilder* builder_;
};

}  // namespace

bool Command::Parse(const std::string& line) {
  if (line.empty()) {
    return true;
  }

  Marker marker(line);
  AstBuilder builder;
  Parser p(&builder);
  ParseResult result = p.ParseVariableDecl(marker);
  builder.SetRoot(result.node_id());
  parse_error_ = result.result();
  if (parse_error_.ok()) {
    accumulated_nodes_ = std::move(builder);
    return true;
  }
  return false;
}

}  // namespace shell::console
