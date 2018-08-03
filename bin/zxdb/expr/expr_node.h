// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <iosfwd>
#include <memory>

#include "garnet/bin/zxdb/expr/expr_token.h"

namespace zxdb {

class AddressOfExprNode;
class ArrayAccessExprNode;
class DereferenceExprNode;
class IdentifierExprNode;
class IntegerExprNode;
class MemberAccessExprNode;
class UnaryOpExprNode;

// Represents one node in the abstract syntax tree.
class ExprNode {
 public:
  ExprNode() = default;
  virtual ~ExprNode() = default;

  virtual const AddressOfExprNode* AsAddressOf() const { return nullptr; }
  virtual const ArrayAccessExprNode* AsArrayAccess() const { return nullptr; }
  virtual const DereferenceExprNode* AsDereference() const { return nullptr; }
  virtual const IdentifierExprNode* AsIdentifier() const { return nullptr; }
  virtual const IntegerExprNode* AsInteger() const { return nullptr; }
  virtual const MemberAccessExprNode* AsMemberAccess() const { return nullptr; }
  virtual const UnaryOpExprNode* AsUnaryOp() const { return nullptr; }

  // Dumps the tree to a stream with the given indent. Used for unit testing
  // and debugging.
  virtual void Print(std::ostream& out, int indent) const = 0;
};

// Implements taking an address of n expression ("&" in C).
class AddressOfExprNode : public ExprNode {
 public:
  AddressOfExprNode();
  AddressOfExprNode(std::unique_ptr<ExprNode> expr) : expr_(std::move(expr)) {}
  ~AddressOfExprNode() override = default;

  const AddressOfExprNode* AsAddressOf() const override { return this; }
  void Print(std::ostream& out, int indent) const override;

 private:
  std::unique_ptr<ExprNode> expr_;
};

// Implements an array access: foo[bar].
class ArrayAccessExprNode : public ExprNode {
 public:
  ArrayAccessExprNode();
  ArrayAccessExprNode(std::unique_ptr<ExprNode> left,
                      std::unique_ptr<ExprNode> inner)
      : left_(std::move(left)), inner_(std::move(inner)) {}
  ~ArrayAccessExprNode() override = default;

  const ArrayAccessExprNode* AsArrayAccess() const override { return this; }
  void Print(std::ostream& out, int indent) const override;

 private:
  std::unique_ptr<ExprNode> left_;
  std::unique_ptr<ExprNode> inner_;
};

// Implements dereferencing a pointer ("*" in C).
class DereferenceExprNode : public ExprNode {
 public:
  DereferenceExprNode();
  DereferenceExprNode(std::unique_ptr<ExprNode> expr)
      : expr_(std::move(expr)) {}
  ~DereferenceExprNode() override = default;

  const DereferenceExprNode* AsDereference() const override { return this; }
  void Print(std::ostream& out, int indent) const override;

 private:
  std::unique_ptr<ExprNode> expr_;
};

// Implements a bare identifier.
class IdentifierExprNode : public ExprNode {
 public:
  IdentifierExprNode() = default;
  IdentifierExprNode(const ExprToken& name) : name_(name) {}
  ~IdentifierExprNode() override = default;

  const IdentifierExprNode* AsIdentifier() const override { return this; }
  void Print(std::ostream& out, int indent) const override;

  // The name of the identifier.
  const ExprToken& name() const { return name_; }

 private:
  ExprToken name_;
};

// Implements an integer.
class IntegerExprNode : public ExprNode {
 public:
  IntegerExprNode() = default;
  explicit IntegerExprNode(const ExprToken& integer) : integer_(integer) {}
  ~IntegerExprNode() override = default;

  const IntegerExprNode* AsInteger() const override { return this; }
  void Print(std::ostream& out, int indent) const override;

  // The number token.
  const ExprToken& integer() const { return integer_; }

 private:
  ExprToken integer_;
};

// Implements both "." and "->" struct/class/union data member accesses.
class MemberAccessExprNode : public ExprNode {
 public:
  MemberAccessExprNode() = default;
  MemberAccessExprNode(std::unique_ptr<ExprNode> left, const ExprToken& access,
                       const ExprToken& member)
      : left_(std::move(left)), accessor_(access), member_(member) {}
  ~MemberAccessExprNode() override = default;

  const MemberAccessExprNode* AsMemberAccess() const override { return this; }
  void Print(std::ostream& out, int indent) const override;

  // Expression on the left side of the "." or "->".
  const ExprNode* left() const { return left_.get(); }

  // The "." or "->" token itself.
  const ExprToken& accessor() const { return accessor_; }

  // The name of the data member.
  const ExprToken& member() const { return member_; }

 private:
  std::unique_ptr<ExprNode> left_;
  ExprToken accessor_;
  ExprToken member_;
};

// Implements unary mathematical operators (the operation depends on the
// operator token).
class UnaryOpExprNode : public ExprNode {
 public:
  UnaryOpExprNode();
  UnaryOpExprNode(const ExprToken& op, std::unique_ptr<ExprNode> expr)
      : op_(op), expr_(std::move(expr)) {}
  ~UnaryOpExprNode() override = default;

  const UnaryOpExprNode* AsUnaryOp() const override { return this; }
  void Print(std::ostream& out, int indent) const override;

 private:
  ExprToken op_;
  std::unique_ptr<ExprNode> expr_;
};

}  // namespace zxdb
