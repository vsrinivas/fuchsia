// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>
#include <iosfwd>
#include <memory>

#include "garnet/bin/zxdb/expr/expr_token.h"
#include "lib/fxl/memory/ref_ptr.h"

namespace zxdb {

class AddressOfExprNode;
class ArrayAccessExprNode;
class DereferenceExprNode;
class Err;
class ExprEvalContext;
class ExprValue;
class IdentifierExprNode;
class IntegerExprNode;
class MemberAccessExprNode;
class UnaryOpExprNode;

// Represents one node in the abstract syntax tree.
class ExprNode {
 public:
  using EvalCallback = std::function<void(const Err& err, ExprValue value)>;

  ExprNode() = default;
  virtual ~ExprNode() = default;

  virtual const AddressOfExprNode* AsAddressOf() const { return nullptr; }
  virtual const ArrayAccessExprNode* AsArrayAccess() const { return nullptr; }
  virtual const DereferenceExprNode* AsDereference() const { return nullptr; }
  virtual const IdentifierExprNode* AsIdentifier() const { return nullptr; }
  virtual const IntegerExprNode* AsInteger() const { return nullptr; }
  virtual const MemberAccessExprNode* AsMemberAccess() const { return nullptr; }
  virtual const UnaryOpExprNode* AsUnaryOp() const { return nullptr; }

  // Evaluates the expression and calls the callback with the result. The
  // callback may be called reentrantly (meaning from within the callstack
  // of Eval itself).
  //
  // Some eval operations are asynchronous because they require reading data
  // from the remote system. Many are not. Since we expect realtively few
  // evals (from user typing) and that they are quite simple (most are one
  // value or a simple dereference), we opt for simplicity and make all
  // evals require a callback.
  //
  // For larger expressions this can be quite heavyweight because not only
  // will the expression be recursively executed, but returning the result
  // will double the depth of the recursion (not to mention a heavyweight
  // lambda bind for each).
  //
  // One thing that might cause expression eval speed to be an issue is when
  // they are automatically executed as in a conditional breakpoint. If we
  // start using expressions in conditional breakpoints and find that
  // performance is unacceptable, this should be optimized to support evals
  // that do not require callbacks unless necessary.
  //
  // Passing "context" as a reference to a RefPtr is unusual but I feel like
  // it's dangerous to pass as a raw pointer, and don't want to incur a
  // threadsafe ref for every single call.
  virtual void Eval(fxl::RefPtr<ExprEvalContext>& context,
                    EvalCallback cb) const = 0;

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
  void Eval(fxl::RefPtr<ExprEvalContext>& context,
            EvalCallback cb) const override;
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
  void Eval(fxl::RefPtr<ExprEvalContext>& context,
            EvalCallback cb) const override;
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
  void Eval(fxl::RefPtr<ExprEvalContext>& context,
            EvalCallback cb) const override;
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
  void Eval(fxl::RefPtr<ExprEvalContext>& context,
            EvalCallback cb) const override;
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
  void Eval(fxl::RefPtr<ExprEvalContext>& context,
            EvalCallback cb) const override;
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
  void Eval(fxl::RefPtr<ExprEvalContext>& context,
            EvalCallback cb) const override;
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
  void Eval(fxl::RefPtr<ExprEvalContext>& context,
            EvalCallback cb) const override;
  void Print(std::ostream& out, int indent) const override;

 private:
  ExprToken op_;
  std::unique_ptr<ExprNode> expr_;
};

}  // namespace zxdb
