// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_EXPR_NODE_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_EXPR_NODE_H_

#include <iosfwd>
#include <memory>

#include "lib/fit/function.h"
#include "src/developer/debug/zxdb/expr/cast.h"
#include "src/developer/debug/zxdb/expr/eval_callback.h"
#include "src/developer/debug/zxdb/expr/expr_token.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"
#include "src/developer/debug/zxdb/expr/parsed_identifier.h"
#include "src/developer/debug/zxdb/expr/vm_op.h"
#include "src/lib/fxl/memory/ref_counted.h"
#include "src/lib/fxl/memory/ref_ptr.h"

namespace zxdb {

class Err;
class EvalContext;
class Type;

// Node subclasses.
class AddressOfExprNode;
class ArrayAccessExprNode;
class BinaryOpExprNode;
class BlockExprNode;
class CastExprNode;
class ConditionExprNode;
class DereferenceExprNode;
class FunctionCallExprNode;
class IdentifierExprNode;
class LiteralExprNode;
class MemberAccessExprNode;
class SizeofExprNode;
class TypeExprNode;
class UnaryOpExprNode;

// Represents one node in the abstract syntax tree.
class ExprNode : public fxl::RefCountedThreadSafe<ExprNode> {
 public:
  virtual const AddressOfExprNode* AsAddressOf() const { return nullptr; }
  virtual const ArrayAccessExprNode* AsArrayAccess() const { return nullptr; }
  virtual const BinaryOpExprNode* AsBinaryOp() const { return nullptr; }
  virtual const BlockExprNode* AsBlock() const { return nullptr; }
  virtual const CastExprNode* AsCast() const { return nullptr; }
  virtual const ConditionExprNode* AsCondition() const { return nullptr; }
  virtual const DereferenceExprNode* AsDereference() const { return nullptr; }
  virtual const FunctionCallExprNode* AsFunctionCall() const { return nullptr; }
  virtual const IdentifierExprNode* AsIdentifier() const { return nullptr; }
  virtual const LiteralExprNode* AsLiteral() const { return nullptr; }
  virtual const MemberAccessExprNode* AsMemberAccess() const { return nullptr; }
  virtual const SizeofExprNode* AsSizeof() const { return nullptr; }
  virtual const TypeExprNode* AsType() const { return nullptr; }
  virtual const UnaryOpExprNode* AsUnaryOp() const { return nullptr; }

  // Appends the bytecode necessary to execute this node.  The bytecode machine is a stack-based
  // machine.
  //
  // Each ExprNode pushes temporary values it needs to the stack (usually by evaluating
  // sub-expressions that will leave these values on the stack). It must consume these values and
  // push exactly one result value on the stack when it is done. This value will be the "result" of
  // the expression. Even expressions with no results (like a loop) must push a value to the stack
  // (typically an empty ExprValue()) since the node calling it always expects one value. If the
  // calling node doesn't want a value, it should "drop" it after running the expression.
  //
  // After the entire program is executed, the result should be a stack containing exactly one
  // ExprValue which is the result of evaluation.
  virtual void EmitBytecode(VmStream& stream) const = 0;

  // Wrapper around EmitBytecode() that automatically expands C++ references to their values.
  // Used when callers know they want the effective value.
  void EmitBytecodeExpandRef(VmStream& stream) const;

  // Dumps the tree to a stream with the given indent. Used for unit testing and debugging.
  virtual void Print(std::ostream& out, int indent) const = 0;

 protected:
  FRIEND_REF_COUNTED_THREAD_SAFE(ExprNode);

  ExprNode() = default;
  virtual ~ExprNode() = default;
};

// Implements taking an address of n expression ("&" in C).
class AddressOfExprNode : public ExprNode {
 public:
  const AddressOfExprNode* AsAddressOf() const override { return this; }
  void EmitBytecode(VmStream& stream) const override;
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
  void EmitBytecode(VmStream& stream) const override;
  void Print(std::ostream& out, int indent) const override;

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(ArrayAccessExprNode);
  FRIEND_MAKE_REF_COUNTED(ArrayAccessExprNode);

  ArrayAccessExprNode();
  ArrayAccessExprNode(fxl::RefPtr<ExprNode> left, fxl::RefPtr<ExprNode> inner)
      : left_(std::move(left)), inner_(std::move(inner)) {}
  ~ArrayAccessExprNode() override = default;

  // Converts the given value which is the result of executing the "inner" expression and converts
  // it to an integer if possible.
  static Err InnerValueToOffset(const fxl::RefPtr<EvalContext>& context, const ExprValue& inner,
                                int64_t* offset);

  fxl::RefPtr<ExprNode> left_;
  fxl::RefPtr<ExprNode> inner_;
};

// Implements all binary operators.
class BinaryOpExprNode : public ExprNode {
 public:
  const BinaryOpExprNode* AsBinaryOp() const override { return this; }
  void EmitBytecode(VmStream& stream) const override;
  void Print(std::ostream& out, int indent) const override;

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(BinaryOpExprNode);
  FRIEND_MAKE_REF_COUNTED(BinaryOpExprNode);

  BinaryOpExprNode();
  BinaryOpExprNode(fxl::RefPtr<ExprNode> left, ExprToken op, fxl::RefPtr<ExprNode> right)
      : left_(std::move(left)), op_(std::move(op)), right_(std::move(right)) {}
  ~BinaryOpExprNode() override = default;

  fxl::RefPtr<ExprNode> left_;
  ExprToken op_;
  fxl::RefPtr<ExprNode> right_;
};

class BlockExprNode : public ExprNode {
 public:
  const BlockExprNode* AsBlock() const override { return this; }
  void EmitBytecode(VmStream& stream) const override;
  void Print(std::ostream& out, int indent) const override;

  const std::vector<fxl::RefPtr<ExprNode>>& statements() const { return statements_; }

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(BlockExprNode);
  FRIEND_MAKE_REF_COUNTED(BlockExprNode);

  BlockExprNode();
  BlockExprNode(std::vector<fxl::RefPtr<ExprNode>> statements, uint32_t entry_local_var_count)
      : statements_(std::move(statements)), entry_local_var_count_(entry_local_var_count) {}
  ~BlockExprNode() override = default;

  std::vector<fxl::RefPtr<ExprNode>> statements_;

  // The number of local variables in scope at the entry of this block. The block uses this to
  // emit bytecode at the exit of the block to clean up local variables back to this number.
  uint32_t entry_local_var_count_ = 0;
};

// Implements all types of casts.
class CastExprNode : public ExprNode {
 public:
  const CastExprNode* AsCast() const override { return this; }
  void EmitBytecode(VmStream& stream) const override;
  void Print(std::ostream& out, int indent) const override;

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(CastExprNode);
  FRIEND_MAKE_REF_COUNTED(CastExprNode);

  CastExprNode();
  CastExprNode(CastType cast_type, fxl::RefPtr<TypeExprNode> to_type, fxl::RefPtr<ExprNode> from)
      : cast_type_(cast_type), to_type_(std::move(to_type)), from_(std::move(from)) {}
  ~CastExprNode() override = default;

  CastType cast_type_;
  fxl::RefPtr<TypeExprNode> to_type_;
  fxl::RefPtr<ExprNode> from_;
};

// Implements all types of if and if/else
class ConditionExprNode : public ExprNode {
 public:
  struct Pair {
    Pair(fxl::RefPtr<ExprNode> c, fxl::RefPtr<ExprNode> t)
        : cond(std::move(c)), then(std::move(t)) {}

    fxl::RefPtr<ExprNode> cond;  // Conditional expression to evaluate.
    fxl::RefPtr<ExprNode> then;  // Code to execute when condition is satisfied. Possibly null.
  };

  const ConditionExprNode* AsCondition() const override { return this; }
  void EmitBytecode(VmStream& stream) const override;
  void Print(std::ostream& out, int indent) const override;

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(ConditionExprNode);
  FRIEND_MAKE_REF_COUNTED(ConditionExprNode);

  ConditionExprNode();
  // The conditions are evaluated in-order until one is true. The "else" can be null in which case
  // it will be ignored.
  ConditionExprNode(std::vector<Pair> conds, fxl::RefPtr<ExprNode> else_case)
      : conds_(std::move(conds)), else_(std::move(else_case)) {}
  ~ConditionExprNode() override = default;

  std::vector<Pair> conds_;
  fxl::RefPtr<ExprNode> else_;  // Possibly null.
};

// Implements dereferencing a pointer ("*" in C).
class DereferenceExprNode : public ExprNode {
 public:
  const DereferenceExprNode* AsDereference() const override { return this; }
  void EmitBytecode(VmStream& stream) const override;
  void Print(std::ostream& out, int indent) const override;

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(DereferenceExprNode);
  FRIEND_MAKE_REF_COUNTED(DereferenceExprNode);

  DereferenceExprNode();
  DereferenceExprNode(fxl::RefPtr<ExprNode> expr) : expr_(std::move(expr)) {}
  ~DereferenceExprNode() override = default;

  fxl::RefPtr<ExprNode> expr_;
};

// Function calls include things like: "Foo()", "ns::Foo<int>(6, 5)".
class FunctionCallExprNode : public ExprNode {
 public:
  const FunctionCallExprNode* AsFunctionCall() const override { return this; }
  void EmitBytecode(VmStream& stream) const override;
  void Print(std::ostream& out, int indent) const override;

  const fxl::RefPtr<ExprNode>& call() const { return call_; }
  const std::vector<fxl::RefPtr<ExprNode>>& args() const { return args_; }

  // Returns true if the given ExprNode is valid for the "call" of a function.
  static bool IsValidCall(const fxl::RefPtr<ExprNode>& call);

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(FunctionCallExprNode);
  FRIEND_MAKE_REF_COUNTED(FunctionCallExprNode);

  FunctionCallExprNode();
  explicit FunctionCallExprNode(fxl::RefPtr<ExprNode> call,
                                std::vector<fxl::RefPtr<ExprNode>> args = {})
      : call_(std::move(call)), args_(std::move(args)) {}
  ~FunctionCallExprNode() override = default;

  // Backend to evaluate a member function call on the given base object. For example,
  // "object.fn_name()".
  //
  // This assumes no function parameters (it's currently used for the PrettyType getters only).
  //
  // The second version handle the "->" case where the object should be a pointer.
  static void EvalMemberCall(const fxl::RefPtr<EvalContext>& context, const ExprValue& object,
                             const std::string& fn_name, EvalCallback cb);
  static void EvalMemberPtrCall(const fxl::RefPtr<EvalContext>& context,
                                const ExprValue& object_ptr, const std::string& fn_name,
                                EvalCallback cb);

  // This will either be an IdentifierExprNode which gives the function name, or a
  // MemberAccessExprNode which gives an object and the function name.
  fxl::RefPtr<ExprNode> call_;

  std::vector<fxl::RefPtr<ExprNode>> args_;
};

// Implements a bare identifier.
class IdentifierExprNode : public ExprNode {
 public:
  const IdentifierExprNode* AsIdentifier() const override { return this; }
  void EmitBytecode(VmStream& stream) const override;
  void Print(std::ostream& out, int indent) const override;

  ParsedIdentifier& ident() { return ident_; }
  const ParsedIdentifier& ident() const { return ident_; }

  // Destructively moves the identifier out of this class. This unusual mutating getter is
  // implemented because the expression parser is also used to parse identifiers, and this will hold
  // the result which we would prefer not to copy to get out.
  ParsedIdentifier TakeIdentifier() { return std::move(ident_); }

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(IdentifierExprNode);
  FRIEND_MAKE_REF_COUNTED(IdentifierExprNode);

  IdentifierExprNode() = default;

  // Simple one-name identifier.
  IdentifierExprNode(std::string name) : ident_(ParsedIdentifierComponent(std::move(name))) {}

  IdentifierExprNode(ParsedIdentifier id) : ident_(std::move(id)) {}
  ~IdentifierExprNode() override = default;

  ParsedIdentifier ident_;
};

// Implements a literal like a number, boolean, or string.
class LiteralExprNode : public ExprNode {
 public:
  const LiteralExprNode* AsLiteral() const override { return this; }
  void EmitBytecode(VmStream& stream) const override;
  void Print(std::ostream& out, int indent) const override;

  // The token's value won't have been checked that it's valid, only that it starts like the type of
  // literal it is. This checking will be done at evaluation time.
  const ExprToken& token() const { return token_; }

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(LiteralExprNode);
  FRIEND_MAKE_REF_COUNTED(LiteralExprNode);

  LiteralExprNode() = default;
  LiteralExprNode(ExprLanguage lang, const ExprToken& token) : language_(lang), token_(token) {}
  ~LiteralExprNode() override = default;

  ExprLanguage language_;
  ExprToken token_;
};

// Implements both "." and "->" struct/class/union data member accesses.
class MemberAccessExprNode : public ExprNode {
 public:
  const MemberAccessExprNode* AsMemberAccess() const override { return this; }
  void EmitBytecode(VmStream& stream) const override;
  void Print(std::ostream& out, int indent) const override;

  // Expression on the left side of the "." or "->".
  const ExprNode* left() const { return left_.get(); }

  // The "." or "->" token itself.
  const ExprToken& accessor() const { return accessor_; }

  // The name of the data member.
  const ParsedIdentifier& member() const { return member_; }

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(MemberAccessExprNode);
  FRIEND_MAKE_REF_COUNTED(MemberAccessExprNode);

  MemberAccessExprNode() = default;
  MemberAccessExprNode(fxl::RefPtr<ExprNode> left, const ExprToken& access,
                       const ParsedIdentifier& member)
      : left_(std::move(left)), accessor_(access), member_(member) {}
  ~MemberAccessExprNode() override = default;

  fxl::RefPtr<ExprNode> left_;
  ExprToken accessor_;
  ParsedIdentifier member_;
};

class SizeofExprNode : public ExprNode {
 public:
  const SizeofExprNode* AsSizeof() const override { return this; }
  void EmitBytecode(VmStream& stream) const override;
  void Print(std::ostream& out, int indent) const override;

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(SizeofExprNode);
  FRIEND_MAKE_REF_COUNTED(SizeofExprNode);

  SizeofExprNode();
  SizeofExprNode(fxl::RefPtr<ExprNode> expr) : expr_(std::move(expr)) {}
  ~SizeofExprNode() override = default;

  static ErrOrValue SizeofType(const fxl::RefPtr<EvalContext>& context, const Type* type);

  fxl::RefPtr<ExprNode> expr_;
};

// Implements references to type names. This mostly appears in casts.
class TypeExprNode : public ExprNode {
 public:
  const TypeExprNode* AsType() const override { return this; }
  void EmitBytecode(VmStream& stream) const override;
  void Print(std::ostream& out, int indent) const override;

  fxl::RefPtr<Type>& type() { return type_; }
  const fxl::RefPtr<Type>& type() const { return type_; }

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(TypeExprNode);
  FRIEND_MAKE_REF_COUNTED(TypeExprNode);

  TypeExprNode();
  TypeExprNode(fxl::RefPtr<Type> type) : type_(std::move(type)) {}
  ~TypeExprNode() override = default;

  fxl::RefPtr<Type> type_;
};

// Implements unary mathematical operators (the operation depends on the operator token).
class UnaryOpExprNode : public ExprNode {
 public:
  const UnaryOpExprNode* AsUnaryOp() const override { return this; }
  void EmitBytecode(VmStream& stream) const override;
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

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_EXPR_NODE_H_
