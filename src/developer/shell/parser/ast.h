// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SHELL_PARSER_AST_H_
#define SRC_DEVELOPER_SHELL_PARSER_AST_H_

#include <lib/syslog/cpp/macros.h>

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
class PathElement;
class PathEscape;
class PathSeparator;
class Operator;
class String;
class Object;
class Field;
class Path;
class AddSub;

template <typename T = void>
class NodeVisitor;

template <typename T>
class WrappingVisitor;

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

  // Visit this node with a visitor.
  template <typename T>
  T Visit(NodeVisitor<T>* visitor) const {
    WrappingVisitor wrapped(visitor);
    Visit(&wrapped);
    return std::move(wrapped.result);
  }
  template <>
  void Visit<void>(NodeVisitor<void>* visitor) const {
    VisitVoid(visitor);
  }

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
  virtual PathElement* AsPathElement() { return nullptr; }
  const PathElement* AsPathElement() const { return const_cast<Node*>(this)->AsPathElement(); }
  virtual PathEscape* AsPathEscape() { return nullptr; }
  const PathEscape* AsPathEscape() const { return const_cast<Node*>(this)->AsPathEscape(); }
  virtual Operator* AsOperator() { return nullptr; }
  const Operator* AsOperator() const { return const_cast<Node*>(this)->AsOperator(); }
  virtual String* AsString() { return nullptr; }
  const String* AsString() const { return const_cast<Node*>(this)->AsString(); }
  virtual Object* AsObject() { return nullptr; }
  const Object* AsObject() const { return const_cast<Node*>(this)->AsObject(); }
  virtual Field* AsField() { return nullptr; }
  const Field* AsField() const { return const_cast<Node*>(this)->AsField(); }
  virtual Path* AsPath() { return nullptr; }
  const Path* AsPath() const { return const_cast<Node*>(this)->AsPath(); }
  virtual AddSub* AsAddSub() { return nullptr; }
  const AddSub* AsAddSub() const { return const_cast<Node*>(this)->AsAddSub(); }

  // ID methods for keywords
  virtual bool IsConst() const { return false; }
  virtual bool IsVar() const { return false; }
  virtual bool IsFieldSeparator() const { return false; }
  virtual bool IsPathSeparator() const { return false; }

 private:
  virtual void VisitVoid(NodeVisitor<void>* visitor) const = 0;

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
  void VisitVoid(NodeVisitor<void>* visitor) const override;

  static const std::vector<std::shared_ptr<Node>> kEmptyChildren;

  const size_t size_;
};

class Error : public Terminal {
 public:
  Error(size_t start, size_t size, std::string_view message)
      : Terminal(start, size, ""), message_(message) {}

  const std::string& message() const { return message_; }

  bool IsError() const override { return true; }

  std::string ToString(std::string_view unit) const override;
  Error* AsError() override { return this; }

 private:
  void VisitVoid(NodeVisitor<void>* visitor) const override;

  const std::string message_;
};

// Terminal representing a ":".
class FieldSeparator : public Terminal {
 public:
  FieldSeparator(size_t start, size_t size, std::string_view content)
      : Terminal(start, size, content) {}

  bool IsFieldSeparator() const override { return true; }

 private:
  void VisitVoid(NodeVisitor<void>* visitor) const override;
};

// Terminal representing the "const" keyword.
class Const : public Terminal {
 public:
  Const(size_t start, size_t size, std::string_view content) : Terminal(start, size, content) {}

  bool IsConst() const override { return true; }

 private:
  void VisitVoid(NodeVisitor<void>* visitor) const override;
};

// Terminal representing the "var" keyword.
class Var : public Terminal {
 public:
  Var(size_t start, size_t size, std::string_view content) : Terminal(start, size, content) {}

  bool IsVar() const override { return true; }

 private:
  void VisitVoid(NodeVisitor<void>* visitor) const override;
};

// Terminal representing a sequence of decimal digits.
class DecimalGroup : public Terminal {
 public:
  DecimalGroup(size_t start, size_t size, std::string_view content);

  size_t digits() const { return digits_; }
  uint64_t value() const { return value_; }

  DecimalGroup* AsDecimalGroup() override { return this; }

 private:
  void VisitVoid(NodeVisitor<void>* visitor) const override;

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
  void VisitVoid(NodeVisitor<void>* visitor) const override;

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
  void VisitVoid(NodeVisitor<void>* visitor) const override;

  std::string identifier_ = "";
};

// Terminal representing a piece of a string literal.
class StringEntity : public Terminal {
 public:
  StringEntity(size_t start, size_t size, std::string_view content)
      : Terminal(start, size, content), content_(content) {}

  const std::string& content() const { return content_; }

  StringEntity* AsStringEntity() override { return this; }

 private:
  void VisitVoid(NodeVisitor<void>* visitor) const override;

  std::string content_ = "";
};

// Terminal representing a piece of an escape sequence in a string literal.
class EscapeSequence : public StringEntity {
 public:
  EscapeSequence(size_t start, size_t size, std::string_view content)
      : StringEntity(start, size, Decode(content)) {}

  EscapeSequence* AsEscapeSequence() override { return this; }

 private:
  void VisitVoid(NodeVisitor<void>* visitor) const override;

  static std::string Decode(std::string_view sequence);
};

// Terminal representing a continuous piece of a path.
class PathElement : public Terminal {
 public:
  PathElement(size_t start, size_t size, std::string_view content)
      : Terminal(start, size, content), content_(content) {}

  const std::string& content() const { return content_; }

  PathElement* AsPathElement() override { return this; }

 private:
  void VisitVoid(NodeVisitor<void>* visitor) const override;

  std::string content_ = "";
};

// Terminal representing a piece of an escape sequence in a path.
class PathEscape : public PathElement {
 public:
  PathEscape(size_t start, size_t size, std::string_view content)
      : PathElement(start, size, content.substr(1)) {}

  PathEscape* AsPathEscape() override { return this; }

 private:
  void VisitVoid(NodeVisitor<void>* visitor) const override;
};

// Terminal representing a path separator.
class PathSeparator : public Terminal {
 public:
  PathSeparator(size_t start, size_t size, std::string_view content)
      : Terminal(start, size, content) {}

  bool IsPathSeparator() const override { return true; }

 private:
  void VisitVoid(NodeVisitor<void>* visitor) const override;
};

// Terminal representing an operator.
class Operator : public Terminal {
 public:
  Operator(size_t start, size_t size, std::string_view content)
      : Terminal(start, size, content), operator_(content) {}

  const std::string& op() const { return operator_; }

  Operator* AsOperator() override { return this; }

 private:
  void VisitVoid(NodeVisitor<void>* visitor) const override;

  std::string operator_ = "";
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
  void VisitVoid(NodeVisitor<void>* visitor) const override;

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

 private:
  void VisitVoid(NodeVisitor<void>* visitor) const override;
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
  void VisitVoid(NodeVisitor<void>* visitor) const override;

  Expression* expression_ = nullptr;
  bool is_const_ = false;
  std::string identifier_ = "";
};

class Integer : public Nonterminal {
 public:
  Integer(size_t start, std::vector<std::shared_ptr<Node>> children);

  uint64_t value() const { return value_; }

  std::string_view Name() const override { return "Integer"; }

  Integer* AsInteger() override { return this; }

 private:
  void VisitVoid(NodeVisitor<void>* visitor) const override;

  uint64_t value_ = 0;
};

class String : public Nonterminal {
 public:
  String(size_t start, std::vector<std::shared_ptr<Node>> children);

  const std::string& value() const { return value_; }

  std::string_view Name() const override { return "String"; }

  String* AsString() override { return this; }

 private:
  void VisitVoid(NodeVisitor<void>* visitor) const override;

  std::string value_;
};

class Identifier : public Nonterminal {
 public:
  Identifier(size_t start, std::vector<std::shared_ptr<Node>> children);

  const std::string& identifier() const { return identifier_; }

  std::string_view Name() const override { return "Identifier"; }

  Identifier* AsIdentifier() override { return this; }

 private:
  void VisitVoid(NodeVisitor<void>* visitor) const override;

  std::string identifier_;
};

class Object : public Nonterminal {
 public:
  Object(size_t start, std::vector<std::shared_ptr<Node>> children);

  const std::vector<Field*> fields() const { return fields_; }

  std::string_view Name() const override { return "Object"; }

  Object* AsObject() override { return this; }

 private:
  void VisitVoid(NodeVisitor<void>* visitor) const override;

  std::vector<Field*> fields_;
};

class Field : public Nonterminal {
 public:
  Field(size_t start, std::vector<std::shared_ptr<Node>> children);

  const std::string& name() const { return name_; }
  Node* value() const { return value_; }

  std::string_view Name() const override { return "Field"; }

  Field* AsField() override { return this; }

 private:
  void VisitVoid(NodeVisitor<void>* visitor) const override;

  std::string name_;
  Node* value_ = nullptr;
};

class Path : public Nonterminal {
 public:
  Path(size_t start, std::vector<std::shared_ptr<Node>> children);

  bool is_local() const { return is_local_; }
  const std::vector<std::string>& elements() const { return elements_; }

  std::string_view Name() const override { return "Path"; }

  Path* AsPath() override { return this; }

 private:
  void VisitVoid(NodeVisitor<void>* visitor) const override;

  bool is_local_;
  std::vector<std::string> elements_;
};

class AddSub : public Nonterminal {
 public:
  enum Type {
    kAdd,
    kSubtract,
  };

  AddSub(size_t start, std::vector<std::shared_ptr<Node>> children);

  Type type() const { return type_; }
  Node* a() const { return a_; }
  Node* b() const { return b_; }

  std::string_view Name() const override { return "AddSub"; }

  AddSub* AsAddSub() override { return this; }

 private:
  void VisitVoid(NodeVisitor<void>* visitor) const override;

  Type type_ = kAdd;
  Node* a_ = nullptr;
  Node* b_ = nullptr;
};

class Expression : public Nonterminal {
 public:
  Expression(size_t start, std::vector<std::shared_ptr<Node>> children)
      : Nonterminal(start, std::move(children)) {}

  std::string_view Name() const override { return "Expression"; }
  Expression* AsExpression() override { return this; }

 private:
  void VisitVoid(NodeVisitor<void>* visitor) const override;
};

// Visitor for AST nodes.
template <typename T>
class NodeVisitor {
  friend class Node;
  friend class Terminal;
  friend class Nonterminal;
  friend class Error;
  friend class Var;
  friend class Const;
  friend class FieldSeparator;
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
  friend class PathElement;
  friend class PathEscape;
  friend class PathSeparator;
  friend class Operator;
  friend class String;
  friend class Object;
  friend class Field;
  friend class Path;
  friend class AddSub;

  friend class WrappingVisitor<T>;

 protected:
  virtual T VisitNode(const Node& node) {
    if constexpr (!std::is_void<T>::value) {
      FX_NOTREACHED();
      std::terminate();
    }
  }

  virtual T VisitTerminal(const Terminal& node) { return VisitNode(node); };
  virtual T VisitNonterminal(const Nonterminal& node) { return VisitNode(node); };
  virtual T VisitError(const Error& node) { return VisitTerminal(node); };
  virtual T VisitConst(const Const& node) { return VisitTerminal(node); };
  virtual T VisitVar(const Var& node) { return VisitTerminal(node); };
  virtual T VisitFieldSeparator(const FieldSeparator& node) { return VisitTerminal(node); };
  virtual T VisitProgram(const Program& node) { return VisitNonterminal(node); };
  virtual T VisitVariableDecl(const VariableDecl& node) { return VisitNonterminal(node); };
  virtual T VisitIdentifier(const Identifier& node) { return VisitNonterminal(node); };
  virtual T VisitInteger(const Integer& node) { return VisitNonterminal(node); };
  virtual T VisitExpression(const Expression& node) { return VisitNonterminal(node); };
  virtual T VisitDecimalGroup(const DecimalGroup& node) { return VisitTerminal(node); };
  virtual T VisitHexGroup(const HexGroup& node) { return VisitTerminal(node); };
  virtual T VisitUnescapedIdentifier(const UnescapedIdentifier& node) {
    return VisitTerminal(node);
  };
  virtual T VisitStringEntity(const StringEntity& node) { return VisitTerminal(node); };
  virtual T VisitEscapeSequence(const EscapeSequence& node) { return VisitStringEntity(node); };
  virtual T VisitString(const String& node) { return VisitNonterminal(node); };
  virtual T VisitObject(const Object& node) { return VisitNonterminal(node); };
  virtual T VisitField(const Field& node) { return VisitNonterminal(node); };
  virtual T VisitPathElement(const PathElement& node) { return VisitTerminal(node); };
  virtual T VisitPathEscape(const PathEscape& node) { return VisitTerminal(node); };
  virtual T VisitPathSeparator(const PathSeparator& node) { return VisitTerminal(node); };
  virtual T VisitOperator(const Operator& node) { return VisitTerminal(node); };
  virtual T VisitPath(const Path& node) { return VisitNonterminal(node); };
  virtual T VisitAddSub(const AddSub& node) { return VisitNonterminal(node); };
};

template <typename T>
class WrappingVisitor : public NodeVisitor<void> {
 public:
  WrappingVisitor(NodeVisitor<T>* wrapped) : wrapped_(wrapped) {}

  T result;

 private:
  void VisitNode(const Node& node) override { result = wrapped_->VisitNode(node); }
  void VisitTerminal(const Terminal& node) override { result = wrapped_->VisitTerminal(node); }
  void VisitNonterminal(const Nonterminal& node) override {
    result = wrapped_->VisitNonterminal(node);
  }
  void VisitError(const Error& node) override { result = wrapped_->VisitError(node); }
  void VisitConst(const Const& node) override { result = wrapped_->VisitConst(node); }
  void VisitVar(const Var& node) override { result = wrapped_->VisitVar(node); }
  void VisitFieldSeparator(const FieldSeparator& node) override {
    result = wrapped_->VisitFieldSeparator(node);
  }
  void VisitProgram(const Program& node) override { result = wrapped_->VisitProgram(node); }
  void VisitVariableDecl(const VariableDecl& node) override {
    result = wrapped_->VisitVariableDecl(node);
  }
  void VisitIdentifier(const Identifier& node) override {
    result = wrapped_->VisitIdentifier(node);
  }
  void VisitInteger(const Integer& node) override { result = wrapped_->VisitInteger(node); }
  void VisitExpression(const Expression& node) override {
    result = wrapped_->VisitExpression(node);
  }
  void VisitDecimalGroup(const DecimalGroup& node) override {
    result = wrapped_->VisitDecimalGroup(node);
  }
  void VisitHexGroup(const HexGroup& node) override { result = wrapped_->VisitHexGroup(node); }
  void VisitUnescapedIdentifier(const UnescapedIdentifier& node) override {
    result = wrapped_->VisitUnescapedIdentifier(node);
  }
  void VisitStringEntity(const StringEntity& node) override {
    result = wrapped_->VisitStringEntity(node);
  }
  void VisitEscapeSequence(const EscapeSequence& node) override {
    result = wrapped_->VisitEscapeSequence(node);
  }
  void VisitString(const String& node) override { result = wrapped_->VisitString(node); }
  void VisitObject(const Object& node) override { result = wrapped_->VisitObject(node); }
  void VisitField(const Field& node) override { result = wrapped_->VisitField(node); }
  void VisitPathElement(const PathElement& node) override {
    result = wrapped_->VisitPathElement(node);
  }
  void VisitPathEscape(const PathEscape& node) override {
    result = wrapped_->VisitPathEscape(node);
  }
  void VisitPathSeparator(const PathSeparator& node) override {
    result = wrapped_->VisitPathSeparator(node);
  }
  void VisitOperator(const Operator& node) override { result = wrapped_->VisitOperator(node); }
  void VisitPath(const Path& node) override { result = wrapped_->VisitPath(node); }
  void VisitAddSub(const AddSub& node) override { result = wrapped_->VisitAddSub(node); }

  NodeVisitor<T>* wrapped_;
};

}  // namespace shell::parser::ast

#endif  // SRC_DEVELOPER_SHELL_PARSER_AST_H_
