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

class Node;
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
class StringEntity;
class EscapeSequence;
class String;
class Object;
class Field;
class SimpleExpression;

class NodeVisitor;

// A node in our AST.
class Node {
  friend class NodeVisitor;

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

  // Visit this node with a visitor.
  virtual void Visit(NodeVisitor* visitor) const;

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
  virtual StringEntity* AsStringEntity() { return nullptr; }
  const StringEntity* AsStringEntity() const { return const_cast<Node*>(this)->AsStringEntity(); }
  virtual EscapeSequence* AsEscapeSequence() { return nullptr; }
  const EscapeSequence* AsEscapeSequence() const {
    return const_cast<Node*>(this)->AsEscapeSequence();
  }
  virtual String* AsString() { return nullptr; }
  const String* AsString() const { return const_cast<Node*>(this)->AsString(); }
  virtual Object* AsObject() { return nullptr; }
  const Object* AsObject() const { return const_cast<Node*>(this)->AsObject(); }
  virtual Field* AsField() { return nullptr; }
  const Field* AsField() const { return const_cast<Node*>(this)->AsField(); }
  virtual SimpleExpression* AsSimpleExpression() { return nullptr; }
  const SimpleExpression* AsSimpleExpression() const {
    return const_cast<Node*>(this)->AsSimpleExpression();
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
  void Visit(NodeVisitor* visitor) const override;

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
  void Visit(NodeVisitor* visitor) const override;

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
  void Visit(NodeVisitor* visitor) const override;

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
  void Visit(NodeVisitor* visitor) const override;

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
  void Visit(NodeVisitor* visitor) const override;

 private:
  std::string identifier_ = "";
};

// Terminal representing a piece of a string literal.
class StringEntity : public Terminal {
 public:
  StringEntity(size_t start, size_t size, std::string_view content)
      : Terminal(start, size, content), content_(content) {}

  const std::string& content() const { return content_; }

  StringEntity* AsStringEntity() override { return this; }
  void Visit(NodeVisitor* visitor) const override;

 private:
  std::string content_ = "";
};

// Terminal representing a piece of an escape sequence in a string literal.
class EscapeSequence : public StringEntity {
 public:
  EscapeSequence(size_t start, size_t size, std::string_view content)
      : StringEntity(start, size, Decode(content)) {}

  EscapeSequence* AsEscapeSequence() override { return this; }
  void Visit(NodeVisitor* visitor) const override;

 private:
  static std::string Decode(std::string_view sequence);
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

  void Visit(NodeVisitor* visitor) const override;

 private:
  bool has_errors_ = false;
  std::vector<std::shared_ptr<Node>> children_;
};

class SimpleExpression : public Nonterminal {
 public:
  SimpleExpression(size_t start, std::vector<std::shared_ptr<Node>> children)
      : Nonterminal(start, std::move(children)) {}

  SimpleExpression* AsSimpleExpression() override { return this; }
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
  void Visit(NodeVisitor* visitor) const override;
};

class VariableDecl : public Nonterminal {
 public:
  VariableDecl(size_t start, std::vector<std::shared_ptr<Node>> children);

  const std::string& identifier() const { return identifier_; }
  Expression* expression() const { return expression_; }
  bool is_const() const { return is_const_; }

  std::string_view Name() const override { return "VariableDecl"; }
  VariableDecl* AsVariableDecl() override { return this; }
  void Visit(NodeVisitor* visitor) const override;

 private:
  Expression* expression_ = nullptr;
  bool is_const_ = false;
  std::string identifier_ = "";
};

class Integer : public SimpleExpression {
 public:
  Integer(size_t start, std::vector<std::shared_ptr<Node>> children);

  uint64_t value() const { return value_; }

  std::string_view Name() const override { return "Integer"; }

  Integer* AsInteger() override { return this; }
  void Visit(NodeVisitor* visitor) const override;

 public:
  uint64_t value_ = 0;
};

class String : public SimpleExpression {
 public:
  String(size_t start, std::vector<std::shared_ptr<Node>> children);

  const std::string& value() const { return value_; }

  std::string_view Name() const override { return "String"; }

  String* AsString() override { return this; }
  void Visit(NodeVisitor* visitor) const override;

 public:
  std::string value_;
};

class Identifier : public Nonterminal {
 public:
  Identifier(size_t start, std::vector<std::shared_ptr<Node>> children);

  const std::string& identifier() const { return identifier_; }

  std::string_view Name() const override { return "Identifier"; }

  Identifier* AsIdentifier() override { return this; }
  void Visit(NodeVisitor* visitor) const override;

 private:
  std::string identifier_;
};

class Object : public SimpleExpression {
 public:
  Object(size_t start, std::vector<std::shared_ptr<Node>> children);

  const std::vector<Field*> fields() const { return fields_; }

  std::string_view Name() const override { return "Object"; }

  Object* AsObject() override { return this; }
  void Visit(NodeVisitor* visitor) const override;

 private:
  std::vector<Field*> fields_;
};

class Field : public Nonterminal {
 public:
  Field(size_t start, std::vector<std::shared_ptr<Node>> children);

  const std::string& name() const { return name_; }
  SimpleExpression* value() const { return value_; }

  std::string_view Name() const override { return "Field"; }

  Field* AsField() override { return this; }
  void Visit(NodeVisitor* visitor) const override;

 private:
  std::string name_;
  SimpleExpression* value_;
};

class Expression : public Nonterminal {
 public:
  Expression(size_t start, std::vector<std::shared_ptr<Node>> children)
      : Nonterminal(start, std::move(children)) {}

  std::string_view Name() const override { return "Expression"; }
  Expression* AsExpression() override { return this; }
  void Visit(NodeVisitor* visitor) const override;
};

// Visitor for AST nodes.
class NodeVisitor {
  friend class Node;
  friend class Terminal;
  friend class Nonterminal;
  friend class Error;
  friend class Var;
  friend class Const;
  friend class Program;
  friend class VariableDecl;
  friend class Identifier;
  friend class Integer;
  friend class Expression;
  friend class DecimalGroup;
  friend class HexGroup;
  friend class UnescapedIdentifier;
  friend class StringEntity;
  friend class EscapeSequence;
  friend class String;
  friend class Object;
  friend class Field;
  friend class SimpleExpression;

 protected:
  virtual void VisitNode(const Node& node){};
  virtual void VisitTerminal(const Terminal& node) { VisitNode(node); };
  virtual void VisitNonterminal(const Nonterminal& node) { VisitNode(node); };
  virtual void VisitError(const Error& node) { VisitTerminal(node); };
  virtual void VisitVar(const Var& node) { VisitTerminal(node); };
  virtual void VisitConst(const Const& node) { VisitTerminal(node); };
  virtual void VisitProgram(const Program& node) { VisitNonterminal(node); };
  virtual void VisitVariableDecl(const VariableDecl& node) { VisitNonterminal(node); };
  virtual void VisitIdentifier(const Identifier& node) { VisitNonterminal(node); };
  virtual void VisitInteger(const Integer& node) { VisitSimpleExpression(node); };
  virtual void VisitExpression(const Expression& node) { VisitNonterminal(node); };
  virtual void VisitDecimalGroup(const DecimalGroup& node) { VisitTerminal(node); };
  virtual void VisitHexGroup(const HexGroup& node) { VisitTerminal(node); };
  virtual void VisitUnescapedIdentifier(const UnescapedIdentifier& node) { VisitTerminal(node); };
  virtual void VisitStringEntity(const StringEntity& node) { VisitTerminal(node); };
  virtual void VisitEscapeSequence(const EscapeSequence& node) { VisitStringEntity(node); };
  virtual void VisitString(const String& node) { VisitSimpleExpression(node); };
  virtual void VisitObject(const Object& node) { VisitSimpleExpression(node); };
  virtual void VisitField(const Field& node) { VisitNonterminal(node); };
  virtual void VisitSimpleExpression(const SimpleExpression& node) { VisitNonterminal(node); };
};

}  // namespace shell::parser::ast

#endif  // SRC_DEVELOPER_SHELL_PARSER_AST_H_
