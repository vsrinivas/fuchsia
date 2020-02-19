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
  std::string_view name() const { return name_; }
  void set_name(std::string_view name) { name_ = name; }
  bool is_whitespace() const { return is_whitespace_; }
  void set_whitespace(bool is_whitespace = true) { is_whitespace_ = is_whitespace; }

  // Child nodes of this node. Always empty for terminals, may be empty for non-terminals.
  virtual const std::vector<std::shared_ptr<Node>>& Children() const = 0;

  // Create an s-expression-like string representation of this node. We don't store the parsed text
  // in the node itself so we must be passed the original parsed string.
  virtual std::string ToString(std::string_view unit) const = 0;

  // Number of characters this node corresponds to in the original text.
  virtual size_t Size() const = 0;

  // A node is considered a Meta node if it has no assigned name and is not a terminal.
  virtual bool IsMeta() const { return name_.empty() && !is_whitespace(); }

  virtual bool IsError() const { return false; }

 private:
  // Offset into the original text where the text this node corresponds to starts.
  const size_t start_;

  // Whether this node is considered whitespace.
  bool is_whitespace_ = false;

  // Name of this node. Indicates the type of production it represents.
  std::string_view name_ = "";
};

// Superclass of all terminal nodes in our AST.
class Terminal : public Node {
 public:
  Terminal(size_t start, size_t size) : Node(start), size_(size) {}

  const std::vector<std::shared_ptr<Node>>& Children() const override { return kEmptyChildren; }
  size_t Size() const override { return size_; }

  std::string ToString(std::string_view unit) const override;
  bool IsMeta() const override { return false; }

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

}  // namespace shell::parser::ast

#endif  // SRC_DEVELOPER_SHELL_PARSER_AST_H_
