// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>
#include <iosfwd>
#include <memory>

#include "garnet/bin/zxdb/expr/expr_token.h"
#include "garnet/bin/zxdb/expr/expr_value.h"
#include "garnet/bin/zxdb/expr/identifier.h"
#include "lib/fxl/memory/ref_counted.h"
#include "lib/fxl/memory/ref_ptr.h"

namespace zxdb {

class AddressOfExprNode;
class ArrayAccessExprNode;
class BinaryOpExprNode;
class DereferenceExprNode;
class Err;
class ExprEvalContext;
class IdentifierExprNode;
class IntegerExprNode;
class MemberAccessExprNode;
class UnaryOpExprNode;

// Represents one node in the abstract syntax tree.
class ExprNode : public fxl::RefCountedThreadSafe<ExprNode> {
 public:
  using EvalCallback = std::function<void(const Err& err, ExprValue value)>;

  ExprNode() = default;
  virtual ~ExprNode() = default;

  virtual const AddressOfExprNode* AsAddressOf() const { return nullptr; }
  virtual const ArrayAccessExprNode* AsArrayAccess() const { return nullptr; }
  virtual const BinaryOpExprNode* AsBinaryOp() const { return nullptr; }
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
  // from the remote system. Many are not. Since we expect relatively few
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
  // The caller is responsible for ensuring the tree of nodes is in scope for
  // the duration of this call until the callback is executed. This would
  // normally be done by having the tree be owned by the callback itself. If
  // this is causing memory lifetime problems, we should switch nodes to be
  // reference counted.
  //
  // See also EvalFollowReferences below.
  virtual void Eval(fxl::RefPtr<ExprEvalContext> context,
                    EvalCallback cb) const = 0;

  // Like "Eval" but expands all references to the values they point to. When
  // evaluating a subexpression this is the variant you want because without
  // it the ExprValue in the callback will be the reference, which just
  // contains the address of the value you want.
  //
  // The time you wouldn't want this is when calling externally where the
  // caller wants to know the actual type the expression evaluated to.
  void EvalFollowReferences(fxl::RefPtr<ExprEvalContext> context,
                            EvalCallback cb) const;

  // Dumps the tree to a stream with the given indent. Used for unit testing
  // and debugging.
  virtual void Print(std::ostream& out, int indent) const = 0;
};

// Implements taking an address of n expression ("&" in C).
class AddressOfExprNode : public ExprNode {
 public:
  const AddressOfExprNode* AsAddressOf() const override { return this; }
  void Eval(fxl::RefPtr<ExprEvalContext> context,
            EvalCallback cb) const override;
  void Print(std::ostream& out, int indent) const override;

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(AddressOfExprNode);
  FRIEND_MAKE_REF_COUNTED(AddressOfExprNode);

  AddressOfExprNode();
  AddressOfExprNode(fxl::RefPtr<ExprNode> expr) : expr_(std::move(expr)) {}
  ~AddressOfExprNode() override = default;

  fxl::RefPtr<ExprNode> expr_;
};

// Implements an array access: foo[bar].
class ArrayAccessExprNode : public ExprNode {
 public:
  const ArrayAccessExprNode* AsArrayAccess() const override { return this; }
  void Eval(fxl::RefPtr<ExprEvalContext> context,
            EvalCallback cb) const override;
  void Print(std::ostream& out, int indent) const override;

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(ArrayAccessExprNode);
  FRIEND_MAKE_REF_COUNTED(ArrayAccessExprNode);

  ArrayAccessExprNode();
  ArrayAccessExprNode(fxl::RefPtr<ExprNode> left, fxl::RefPtr<ExprNode> inner)
      : left_(std::move(left)), inner_(std::move(inner)) {}
  ~ArrayAccessExprNode() override = default;

  // Converts the given value which is the result of executing the "inner"
  // expression and converts it to an integer if possible.
  static Err InnerValueToOffset(const ExprValue& inner, int64_t* offset);

  static void DoAccess(fxl::RefPtr<ExprEvalContext> context, ExprValue left,
                       int64_t offset, EvalCallback cb);

  fxl::RefPtr<ExprNode> left_;
  fxl::RefPtr<ExprNode> inner_;
};

// Implements all binary operators.
class BinaryOpExprNode : public ExprNode {
 public:
  const BinaryOpExprNode* AsBinaryOp() const override { return this; }
  void Eval(fxl::RefPtr<ExprEvalContext> context,
            EvalCallback cb) const override;
  void Print(std::ostream& out, int indent) const override;

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(BinaryOpExprNode);
  FRIEND_MAKE_REF_COUNTED(BinaryOpExprNode);

  BinaryOpExprNode();
  BinaryOpExprNode(fxl::RefPtr<ExprNode> left, ExprToken op,
                   fxl::RefPtr<ExprNode> right)
      : left_(std::move(left)), op_(std::move(op)), right_(std::move(right)) {}
  ~BinaryOpExprNode() override = default;

  fxl::RefPtr<ExprNode> left_;
  ExprToken op_;
  fxl::RefPtr<ExprNode> right_;
};

// Implements dereferencing a pointer ("*" in C).
class DereferenceExprNode : public ExprNode {
 public:
  const DereferenceExprNode* AsDereference() const override { return this; }
  void Eval(fxl::RefPtr<ExprEvalContext> context,
            EvalCallback cb) const override;
  void Print(std::ostream& out, int indent) const override;

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(DereferenceExprNode);
  FRIEND_MAKE_REF_COUNTED(DereferenceExprNode);

  DereferenceExprNode();
  DereferenceExprNode(fxl::RefPtr<ExprNode> expr) : expr_(std::move(expr)) {}
  ~DereferenceExprNode() override = default;

  fxl::RefPtr<ExprNode> expr_;
};

// Implements a bare identifier.
class IdentifierExprNode : public ExprNode {
 public:
  const IdentifierExprNode* AsIdentifier() const override { return this; }
  void Eval(fxl::RefPtr<ExprEvalContext> context,
            EvalCallback cb) const override;
  void Print(std::ostream& out, int indent) const override;

  Identifier& ident() { return ident_; }
  const Identifier& ident() const { return ident_; }

  // Destructively moves the identifier out of this class.
  Identifier TakeIdentifier() { return std::move(ident_); }

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(IdentifierExprNode);
  FRIEND_MAKE_REF_COUNTED(IdentifierExprNode);

  IdentifierExprNode() = default;

  // Simple one-name identifier.
  IdentifierExprNode(const ExprToken& name) : ident_(name){};

  IdentifierExprNode(Identifier id) : ident_(std::move(id)) {}
  ~IdentifierExprNode() override = default;

  Identifier ident_;
};

// Implements an integer. If we add more numeric types we may want this to be
// called a "Literal" instead.
class IntegerExprNode : public ExprNode {
 public:
  const IntegerExprNode* AsInteger() const override { return this; }
  void Eval(fxl::RefPtr<ExprEvalContext> context,
            EvalCallback cb) const override;
  void Print(std::ostream& out, int indent) const override;

  // The number token.
  const ExprToken& integer() const { return integer_; }

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(IntegerExprNode);
  FRIEND_MAKE_REF_COUNTED(IntegerExprNode);

  IntegerExprNode() = default;
  explicit IntegerExprNode(const ExprToken& integer) : integer_(integer) {}
  ~IntegerExprNode() override = default;

  ExprToken integer_;
};

// Implements both "." and "->" struct/class/union data member accesses.
class MemberAccessExprNode : public ExprNode {
 public:
  const MemberAccessExprNode* AsMemberAccess() const override { return this; }
  void Eval(fxl::RefPtr<ExprEvalContext> context,
            EvalCallback cb) const override;
  void Print(std::ostream& out, int indent) const override;

  // Expression on the left side of the "." or "->".
  const ExprNode* left() const { return left_.get(); }

  // The "." or "->" token itself.
  const ExprToken& accessor() const { return accessor_; }

  // The name of the data member.
  const Identifier& member() const { return member_; }

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(MemberAccessExprNode);
  FRIEND_MAKE_REF_COUNTED(MemberAccessExprNode);

  MemberAccessExprNode() = default;
  MemberAccessExprNode(fxl::RefPtr<ExprNode> left, const ExprToken& access,
                       const Identifier& member)
      : left_(std::move(left)), accessor_(access), member_(member) {}
  ~MemberAccessExprNode() override = default;

  fxl::RefPtr<ExprNode> left_;
  ExprToken accessor_;
  Identifier member_;
};

// Implements unary mathematical operators (the operation depends on the
// operator token).
class UnaryOpExprNode : public ExprNode {
 public:
  const UnaryOpExprNode* AsUnaryOp() const override { return this; }
  void Eval(fxl::RefPtr<ExprEvalContext> context,
            EvalCallback cb) const override;
  void Print(std::ostream& out, int indent) const override;

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(UnaryOpExprNode);
  FRIEND_MAKE_REF_COUNTED(UnaryOpExprNode);

  UnaryOpExprNode();
  UnaryOpExprNode(const ExprToken& op, fxl::RefPtr<ExprNode> expr)
      : op_(op), expr_(std::move(expr)) {}
  ~UnaryOpExprNode() override = default;

  ExprToken op_;
  fxl::RefPtr<ExprNode> expr_;
};

}  // namespace zxdb
