// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SHELL_PARSER_AST_H_
#define SRC_DEVELOPER_SHELL_PARSER_AST_H_

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace shell::parser::ast {

class Terminal;
class Nonterminal;
class Error;
class Program;
class VariableDecl;
class Identifier;
class Integer;
class Expression;
class DecimalGroup;
class HexGroup;
class UnescapedIdentifier;

// A node in our AST.
class Node {
 public:
  explicit Node(size_t start) : start_(start) {}
  virtual ~Node() = default;

  size_t start() const { return start_; }

  // Child nodes of this node. Always empty for terminals, may be empty for non-terminals.
  virtual const std::vector<std::shared_ptr<Node>>& Children() const = 0;

  // Create an s-expression-like string representation of this node. We don't store the parsed text
  // in the node itself so we must be passed the original parsed string.
  virtual std::string ToString(std::string_view unit) const = 0;

  // Number of characters this node corresponds to in the original text.
  virtual size_t Size() const = 0;

  // Whether this node marks a parse error.
  virtual bool IsError() const { return false; }

  // Whether this node is a whitespace node.
  virtual bool IsWhitespace() const { return false; }

  // Whether this node or any of its children contains parse errors.
  virtual bool HasErrors() const { return IsError(); }

  // Downcasting methods
  virtual Error* AsError() { return nullptr; }
  const Error* AsError() const { return const_cast<Node*>(this)->AsError(); }
  virtual Program* AsProgram() { return nullptr; }
  const Program* AsProgram() const { return const_cast<Node*>(this)->AsProgram(); }
  virtual VariableDecl* AsVariableDecl() { return nullptr; }
  const VariableDecl* AsVariableDecl() const { return const_cast<Node*>(this)->AsVariableDecl(); }
  virtual Identifier* AsIdentifier() { return nullptr; }
  const Identifier* AsIdentifier() const { return const_cast<Node*>(this)->AsIdentifier(); }
  virtual Integer* AsInteger() { return nullptr; }
  const Integer* AsInteger() const { return const_cast<Node*>(this)->AsInteger(); }
  virtual Expression* AsExpression() { return nullptr; }
  const Expression* AsExpression() const { return const_cast<Node*>(this)->AsExpression(); }
  virtual DecimalGroup* AsDecimalGroup() { return nullptr; }
  const DecimalGroup* AsDecimalGroup() const { return const_cast<Node*>(this)->AsDecimalGroup(); }
  virtual HexGroup* AsHexGroup() { return nullptr; }
  const HexGroup* AsHexGroup() const { return const_cast<Node*>(this)->AsHexGroup(); }
  virtual UnescapedIdentifier* AsUnescapedIdentifier() { return nullptr; }
  const UnescapedIdentifier* AsUnescapedIdentifier() const {
    return const_cast<Node*>(this)->AsUnescapedIdentifier();
  }

  // ID methods for keywords
  virtual bool IsConst() const { return false; }
  virtual bool IsVar() const { return false; }

 private:
  // Offset into the original text where the text this node corresponds to starts.
  const size_t start_;
};

// Superclass of all terminal nodes in our AST.
class Terminal : public Node {
 public:
  Terminal(size_t start, size_t size, std::string_view /*content*/) : Node(start), size_(size) {}

  const std::vector<std::shared_ptr<Node>>& Children() const override { return kEmptyChildren; }
  size_t Size() const override { return size_; }

  std::string ToString(std::string_view unit) const override;

 private:
  static const std::vector<std::shared_ptr<Node>> kEmptyChildren;

  const size_t size_;
};

class Error : public Terminal {
 public:
  Error(size_t start, size_t size, const std::string& message)
      : Terminal(start, size, ""), message_(message) {}

  const std::string& message() const { return message_; }

  bool IsError() const override { return true; }

  std::string ToString(std::string_view unit) const override;
  Error* AsError() override { return this; }

 private:
  const std::string message_;
};

// Terminal representing the "const" keyword.
class Const : public Terminal {
 public:
  Const(size_t start, size_t size, std::string_view content) : Terminal(start, size, content) {}

  bool IsConst() const override { return true; }
};

// Terminal representing the "var" keyword.
class Var : public Terminal {
 public:
  Var(size_t start, size_t size, std::string_view content) : Terminal(start, size, content) {}

  bool IsVar() const override { return true; }
};

// Terminal representing a sequence of decimal digits.
class DecimalGroup : public Terminal {
 public:
  DecimalGroup(size_t start, size_t size, std::string_view content);

  size_t digits() const { return digits_; }
  uint64_t value() const { return value_; }

  DecimalGroup* AsDecimalGroup() override { return this; }

 private:
  size_t digits_;
  uint64_t value_ = 0;
};

// Terminal representing a sequence of hex digits.
class HexGroup : public Terminal {
 public:
  HexGroup(size_t start, size_t size, std::string_view content);

  size_t digits() const { return digits_; }
  uint64_t value() const { return value_; }

  HexGroup* AsHexGroup() override { return this; }

 private:
  size_t digits_;
  uint64_t value_ = 0;
};

// Terminal representing an unescaped identifier
class UnescapedIdentifier : public Terminal {
 public:
  UnescapedIdentifier(size_t start, size_t size, std::string_view content)
      : Terminal(start, size, content), identifier_(content) {}

  const std::string& identifier() const { return identifier_; }

  UnescapedIdentifier* AsUnescapedIdentifier() override { return this; }

 private:
  std::string identifier_ = "";
};

// Superclass of all non-terminal nodes in our AST.
class Nonterminal : public Node {
 public:
  Nonterminal(size_t start, std::vector<std::shared_ptr<Node>> children)
      : Node(start), children_(std::move(children)) {
    for (const auto& child : children_) {
      if (child->HasErrors()) {
        has_errors_ = true;
        break;
      }
    }
  }

  // Name of this node as a string.
  virtual std::string_view Name() const = 0;

  const std::vector<std::shared_ptr<Node>>& Children() const override { return children_; }
  size_t Size() const override {
    if (children_.empty()) {
      return 0;
    } else {
      return children_.back()->start() - start() + children_.back()->Size();
    }
  }

  std::string ToString(std::string_view unit) const override;

  bool HasErrors() const override { return has_errors_; }

 private:
  bool has_errors_ = false;
  std::vector<std::shared_ptr<Node>> children_;
};

// Result of an attempt to parse a single token. Usually that will result in a terminal, but if
// there are errors, we may get one of these instead. Its children will be error nodes and the
// fragments of the token that parsed correctly.
class TokenResult : public Nonterminal {
 public:
  TokenResult(size_t start, std::vector<std::shared_ptr<Node>> children)
      : Nonterminal(start, std::move(children)) {}

  std::string_view Name() const override { return ""; }

  // If one of these ends up in output outside of the Token() combinator, then it's definitely an
  // error.
  bool IsError() const override { return true; }
};

class Whitespace : public Nonterminal {
 public:
  Whitespace(size_t start, std::vector<std::shared_ptr<Node>> children)
      : Nonterminal(start, std::move(children)) {}

  std::string_view Name() const override { return "Whitespace"; }
  bool IsWhitespace() const override { return true; }
};

class Program : public Nonterminal {
 public:
  Program(size_t start, std::vector<std::shared_ptr<Node>> children)
      : Nonterminal(start, std::move(children)) {}

  std::string_view Name() const override { return "Program"; }
  Program* AsProgram() override { return this; }
};

class VariableDecl : public Nonterminal {
 public:
  VariableDecl(size_t start, std::vector<std::shared_ptr<Node>> children);

  const std::string& identifier() const { return identifier_; }
  Expression* expression() const { return expression_; }
  bool is_const() const { return is_const_; }

  std::string_view Name() const override { return "VariableDecl"; }
  VariableDecl* AsVariableDecl() override { return this; }

 private:
  Expression* expression_ = nullptr;
  bool is_const_ = false;
  std::string identifier_ = "";
};

class Integer : public Nonterminal {
 public:
  Integer(size_t start, std::vector<std::shared_ptr<Node>> children);

  uint64_t value() const { return value_; }

  std::string_view Name() const override { return "Integer"; }

  std::optional<std::string> GetInteger(std::string_view unit) const;
  Integer* AsInteger() override { return this; }

 public:
  uint64_t value_ = 0;
};

class Identifier : public Nonterminal {
 public:
  Identifier(size_t start, std::vector<std::shared_ptr<Node>> children);

  const std::string& identifier() const { return identifier_; }

  std::string_view Name() const override { return "Identifier"; }

  std::optional<std::string> GetIdentifier(std::string_view unit) const;
  Identifier* AsIdentifier() override { return this; }

 private:
  std::string identifier_;
};

class Expression : public Nonterminal {
 public:
  Expression(size_t start, std::vector<std::shared_ptr<Node>> children)
      : Nonterminal(start, std::move(children)) {}

  std::string_view Name() const override { return "Expression"; }
  Expression* AsExpression() override { return this; }
};

}  // namespace shell::parser::ast

#endif  // SRC_DEVELOPER_SHELL_PARSER_AST_H_
