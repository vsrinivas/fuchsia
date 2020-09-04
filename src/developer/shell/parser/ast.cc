// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/parser/ast.h"

#include <lib/syslog/cpp/macros.h>

#include <limits>
#include <sstream>

#include "third_party/icu/source/common/unicode/utf8.h"

namespace shell::parser::ast {
namespace {

uint8_t CharFromHex(char ch) {
  if (ch - '0' <= 9) {
    return ch - '0';
  } else if (ch - 'A' <= 5) {
    return ch - 'A' + 10;
  } else {
    return ch - 'a' + 10;
  }
}

}  // namespace

const std::vector<std::shared_ptr<Node>> Terminal::kEmptyChildren = {};

std::string Terminal::ToString(std::string_view unit) const {
  std::string prefix;

  return prefix + "'" + std::string(unit.substr(start(), size_)) + "'";
}

std::string Nonterminal::ToString(std::string_view unit) const {
  std::stringstream out;

  out << Name() << "(";

  std::string spacer = "";
  for (const auto& child : Children()) {
    out << spacer << child->ToString(unit);
    spacer = " ";
  }

  out << ")";
  return out.str();
}

std::string Error::ToString(std::string_view unit) const { return "E[" + message_ + "]"; }

DecimalGroup::DecimalGroup(size_t start, size_t size, std::string_view content)
    : Terminal(start, size, content), digits_(content.size()) {
  digits_ = content.size();

  for (const char& ch : content) {
    uint64_t old_value = value_;
    value_ *= 10;
    FX_DCHECK(value_ >= old_value) << "Insufficient precision to store DecimalGroup value.";
    value_ += ch - '0';
  }
}

HexGroup::HexGroup(size_t start, size_t size, std::string_view content)
    : Terminal(start, size, content), digits_(content.size()) {
  digits_ = content.size();

  FX_DCHECK(digits_ <= 16) << "Insufficient precision to store HexGroup value.";

  for (const char& ch : content) {
    value_ *= 16;
    value_ += CharFromHex(ch);
  }
}

std::string EscapeSequence::Decode(std::string_view sequence) {
  if (sequence == "\\n") {
    return "\n";
  } else if (sequence == "\\t") {
    return "\t";
  } else if (sequence == "\\r") {
    return "\r";
  } else if (sequence == "\\\"") {
    return "\"";
  } else if (sequence == "\\\\") {
    return "\\";
  } else if (sequence == "\\\n") {
    // TODO: Do something fancy for escaped newlines?
    return "\n";
  } else if (sequence.size() == 8 && sequence.substr(0, 2) == "\\u") {
    uint32_t codepoint = CharFromHex(sequence[2]) << 20;
    codepoint += CharFromHex(sequence[3]) << 16;
    codepoint += CharFromHex(sequence[4]) << 12;
    codepoint += CharFromHex(sequence[5]) << 8;
    codepoint += CharFromHex(sequence[6]) << 4;
    codepoint += CharFromHex(sequence[7]);

    // TODO: Figure out how to handle a bad unicode character. U8_APPEND_UNSAFE should just give us
    // some garbage we can ignore for now.
    uint8_t bytes[4] = {0, 0, 0, 0};
    size_t length = 0;
    U8_APPEND_UNSAFE(bytes, length, codepoint);
    return std::string(reinterpret_cast<char*>(bytes), length);
  } else {
    // We might get odd things if we're in an error path, so fail gently.
    return "";
  }
}

Integer::Integer(size_t start, std::vector<std::shared_ptr<Node>> children)
    : Nonterminal(start, std::move(children)) {
  for (const auto& child : Children()) {
    if (auto hex = child->AsHexGroup()) {
      auto next = value_ << hex->digits() * 4;
      FX_DCHECK(next >= value_) << "Insufficient precision to store Integer value.";
      value_ = next + hex->value();
    } else if (auto dec = child->AsDecimalGroup()) {
      auto old_value = value_;
      for (size_t i = 0; i < dec->digits(); i++, value_ *= 10)
        ;
      FX_DCHECK(value_ >= old_value) << "Insufficient precision to store Integer value.";
      value_ += dec->value();
    }
  }
}

String::String(size_t start, std::vector<std::shared_ptr<Node>> children)
    : Nonterminal(start, std::move(children)) {
  for (const auto& child : Children()) {
    if (auto entity = child->AsStringEntity()) {
      value_ += entity->content();
    }
  }
}

VariableDecl::VariableDecl(size_t start, std::vector<std::shared_ptr<Node>> children)
    : Nonterminal(start, std::move(children)) {
  for (const auto& child : Children()) {
    if (child->IsConst()) {
      is_const_ = true;
    } else if (auto expr = child->AsExpression()) {
      expression_ = expr;
    } else if (auto id = child->AsIdentifier()) {
      identifier_ = id->identifier();
    }
  }
}

Identifier::Identifier(size_t start, std::vector<std::shared_ptr<Node>> children)
    : Nonterminal(start, std::move(children)) {
  for (const auto& child : Children()) {
    if (auto ue = child->AsUnescapedIdentifier()) {
      identifier_ = ue->identifier();
      break;
    }
  }
}

Object::Object(size_t start, std::vector<std::shared_ptr<Node>> children)
    : Nonterminal(start, std::move(children)) {
  for (const auto& child : Children()) {
    if (auto field = child->AsField()) {
      fields_.push_back(field);
    }
  }
}

Field::Field(size_t start, std::vector<std::shared_ptr<Node>> children)
    : Nonterminal(start, std::move(children)) {
  bool seen_name = false;
  for (const auto& child : Children()) {
    if (seen_name == false) {
      if (auto ident = child->AsIdentifier()) {
        name_ = ident->identifier();
        seen_name = true;
      } else if (auto str = child->AsString()) {
        name_ = str->value();
        seen_name = true;
      }
    } else if (!child->IsError() && !child->IsFieldSeparator()) {
      value_ = child.get();
    }
  }
}

Path::Path(size_t start, std::vector<std::shared_ptr<Node>> children)
    : Nonterminal(start, std::move(children)) {
  std::stringstream element;

  is_local_ = true;
  bool seen_element = false;
  for (const auto& child : Children()) {
    if (auto el = child->AsPathElement()) {
      element << el->content();
    } else if (child->IsPathSeparator()) {
      if (element.tellp() > 0) {
        seen_element = true;

        if (element.str() != ".") {
          elements_.push_back(element.str());
        }

        element.str("");
        element.clear();
      }

      if (!seen_element) {
        is_local_ = false;
      }
    }
  }

  if (element.tellp() > 0 && element.str() != ".") {
    elements_.push_back(element.str());
  }
}

AddSub::AddSub(size_t start, std::vector<std::shared_ptr<Node>> children)
    : Nonterminal(start, std::move(children)) {
  for (const auto& child : Children()) {
    if (auto op = child->AsOperator()) {
      if (op->op() == "-") {
        type_ = kSubtract;
      } else {
        FX_DCHECK(op->op() == "+");
        type_ = kAdd;
      }
    } else if (!child->IsError()) {
      if (!a_) {
        a_ = child.get();
      } else {
        b_ = child.get();
      }
    }
  }
}

// Visit implementations
void Terminal::VisitVoid(NodeVisitor<void>* visitor) const { visitor->VisitTerminal(*this); }
void Error::VisitVoid(NodeVisitor<void>* visitor) const { visitor->VisitError(*this); }
void Const::VisitVoid(NodeVisitor<void>* visitor) const { visitor->VisitConst(*this); }
void Var::VisitVoid(NodeVisitor<void>* visitor) const { visitor->VisitVar(*this); }
void FieldSeparator::VisitVoid(NodeVisitor<void>* visitor) const {
  visitor->VisitFieldSeparator(*this);
}
void DecimalGroup::VisitVoid(NodeVisitor<void>* visitor) const {
  visitor->VisitDecimalGroup(*this);
}
void HexGroup::VisitVoid(NodeVisitor<void>* visitor) const { visitor->VisitHexGroup(*this); }
void UnescapedIdentifier::VisitVoid(NodeVisitor<void>* visitor) const {
  visitor->VisitUnescapedIdentifier(*this);
}
void StringEntity::VisitVoid(NodeVisitor<void>* visitor) const {
  visitor->VisitStringEntity(*this);
}
void EscapeSequence::VisitVoid(NodeVisitor<void>* visitor) const {
  visitor->VisitEscapeSequence(*this);
}
void PathElement::VisitVoid(NodeVisitor<void>* visitor) const { visitor->VisitPathElement(*this); }
void PathEscape::VisitVoid(NodeVisitor<void>* visitor) const { visitor->VisitPathEscape(*this); }
void PathSeparator::VisitVoid(NodeVisitor<void>* visitor) const {
  visitor->VisitPathSeparator(*this);
}
void Operator::VisitVoid(NodeVisitor<void>* visitor) const { visitor->VisitOperator(*this); }
void Nonterminal::VisitVoid(NodeVisitor<void>* visitor) const { visitor->VisitNonterminal(*this); }
void Program::VisitVoid(NodeVisitor<void>* visitor) const { visitor->VisitProgram(*this); }
void VariableDecl::VisitVoid(NodeVisitor<void>* visitor) const {
  visitor->VisitVariableDecl(*this);
}
void Integer::VisitVoid(NodeVisitor<void>* visitor) const { visitor->VisitInteger(*this); }
void String::VisitVoid(NodeVisitor<void>* visitor) const { visitor->VisitString(*this); }
void Identifier::VisitVoid(NodeVisitor<void>* visitor) const { visitor->VisitIdentifier(*this); }
void Object::VisitVoid(NodeVisitor<void>* visitor) const { visitor->VisitObject(*this); }
void Field::VisitVoid(NodeVisitor<void>* visitor) const { visitor->VisitField(*this); }
void Path::VisitVoid(NodeVisitor<void>* visitor) const { visitor->VisitPath(*this); }
void AddSub::VisitVoid(NodeVisitor<void>* visitor) const { visitor->VisitAddSub(*this); }
void Expression::VisitVoid(NodeVisitor<void>* visitor) const { visitor->VisitExpression(*this); }

}  // namespace shell::parser::ast
