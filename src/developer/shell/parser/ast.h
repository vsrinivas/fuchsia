// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SHELL_PARSER_AST_H_
#define SRC_DEVELOPER_SHELL_PARSER_AST_H_

#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace shell::parser::ast {

class Terminal;
class Nonterminal;

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

 private:
  // Offset into the original text where the text this node corresponds to starts.
  const size_t start_;
};

// Superclass of all terminal nodes in our AST.
class Terminal : public Node {
 public:
  Terminal(size_t start, size_t size) : Node(start), size_(size) {}

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
      : Terminal(start, size), message_(message) {}

  bool IsError() const override { return true; }

  std::string ToString(std::string_view unit) const override;

 private:
  const std::string message_;
};

// Superclass of all non-terminal nodes in our AST.
class Nonterminal : public Node {
 public:
  Nonterminal(size_t start, std::vector<std::shared_ptr<Node>> children)
      : Node(start), children_(std::move(children)) {}

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

 private:
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
};

class VariableDecl : public Nonterminal {
 public:
  VariableDecl(size_t start, std::vector<std::shared_ptr<Node>> children)
      : Nonterminal(start, std::move(children)) {}

  std::string_view Name() const override { return "VariableDecl"; }
};

}  // namespace shell::parser::ast

#endif  // SRC_DEVELOPER_SHELL_PARSER_AST_H_
