// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_PRETTY_TYPE_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_PRETTY_TYPE_H_

#include <initializer_list>
#include <map>

#include "lib/fit/defer.h"
#include "lib/fit/function.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/expr/eval_callback.h"
#include "src/developer/debug/zxdb/expr/eval_context.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"

namespace zxdb {

class FormatNode;
struct FormatOptions;

// Base class for a type we can do "pretty" things with.
//
// At the most basic level, a PrettyType provides alternate formatting which can properly
// encapsulate more complex data structures like vectors and arrays.
//
// We also provide expression evaluation support for these types, allowing them to implement
// getters, pointer derefercing, and array access. This allows the debugger to emulate common
// queries on types that may not have an otherwise easy access point. For example, users will often
// want to query the size, capacity, or a single indexed element of a vector. This is difficult to
// do using just the struct information, and we do not allow actually executing code to run the real
// implementations of these functions.
class PrettyType {
 public:
  using EvalFunction = fit::function<void(fxl::RefPtr<EvalContext>, const ExprValue& object_value,
                                          fit::callback<void(const Err&, ExprValue)>)>;
  using EvalArrayFunction = fit::function<void(
      fxl::RefPtr<EvalContext>, const ExprValue& object_value, int64_t index, EvalCallback)>;

  PrettyType() = default;

  // This variant takes a list of getter names and expressions for this type. For example, a vector
  // might pass something like
  //  {
  //    {"size", "end_ - begin_"},
  //    {"capacity", "capacity_" }
  //  }
  // to provide size() and capacity() getters.
  explicit PrettyType(std::initializer_list<std::pair<std::string, std::string>> getters);

  virtual ~PrettyType() = default;

  // Adds a getter expression to the lookup table returned by GetGetter().
  void AddGetterExpression(const std::string& getter_name, const std::string& expression);

  // Fills the given FormatNode. Upon completion, issues the given deferred_callback. If the format
  // node is filled asynchronously the implementation should take a weak pointer to it since the
  // lifetime is not guaranteed.
  virtual void Format(FormatNode* node, const FormatOptions& options,
                      fxl::RefPtr<EvalContext> context, fit::deferred_callback cb) = 0;

  // Returns a function which can be evaluated to execute a getter on an object of this type.
  // If there is no matching getter, a null fit::function is returned.
  //
  // (Implementation note: this design is so the caller can check if there is a getter and then
  // execute it with a callback, which is how most nodes want to run.)
  virtual EvalFunction GetGetter(const std::string& getter_name) const;

  // Returns a function which can be evaluated to execute a unary "*" dereference operator on an
  // object of the given type.
  //
  // This will also be used for "operator->" which is implemented as a dereference followed by
  // a ".".
  //
  // This is used for smart-pointer-like classes without forcing the user to look into the guts of a
  // smart pointer. Returns an empty function if there is no member access operator.
  virtual EvalFunction GetDereferencer() const { return EvalFunction(); }

  // Returns a function which can be executed to perform an array access. This allows the pretty
  // printer to implement "operator[]" on a type. This is important for implementing wrappers around
  // vector types.
  virtual EvalArrayFunction GetArrayAccess() const { return EvalArrayFunction(); }

 protected:
  // Evaluates the given expression in the context of the given object. The object's members will
  // be injected into the active scope.
  static void EvalExpressionOn(fxl::RefPtr<EvalContext> context, const ExprValue& object,
                               const std::string& expression,
                               fit::callback<void(const Err&, ExprValue result)>);

  // Extracts a structure member with the given name. Pass one name to extract a single
  // member, pass a sequence of names to recursively extract values from nested structs.
  static Err ExtractMember(fxl::RefPtr<EvalContext> context, const ExprValue& value,
                           std::initializer_list<std::string> names, ExprValue* extracted);

  // Like ExtractMember but it attempts to convert the result to a 64-bit number.
  static Err Extract64BitMember(fxl::RefPtr<EvalContext> context, const ExprValue& value,
                                std::initializer_list<std::string> names, uint64_t* extracted);

 private:
  // Registered getter functions and the expressions they map to.
  std::map<std::string, std::string> getters_;
};

class PrettyArray : public PrettyType {
 public:
  PrettyArray(const std::string& ptr_expr, const std::string& size_expr,
              std::initializer_list<std::pair<std::string, std::string>> getters = {})
      : PrettyType(getters), ptr_expr_(ptr_expr), size_expr_(size_expr) {}

  void Format(FormatNode* node, const FormatOptions& options, fxl::RefPtr<EvalContext> context,
              fit::deferred_callback cb) override;
  EvalArrayFunction GetArrayAccess() const override;

 private:
  const std::string ptr_expr_;   // Expression to compute array start pointer.
  const std::string size_expr_;  // Expression to compute array size.
};

// For pretty-printing character strings that live on the heap.
//
// This gets a little more complicated for strings that live in an array inline in some type
// because theoretically it could (but normally can't) be in a temporary we can't take the address
// of. Even if we could do that, it would require a fetch of memory from the target that we already
// have locally. So this class is limited to the fetching from the heap case.
class PrettyHeapString : public PrettyType {
 public:
  PrettyHeapString(const std::string& ptr_expr, const std::string& size_expr,
                   std::initializer_list<std::pair<std::string, std::string>> getters = {})
      : PrettyType(getters), ptr_expr_(ptr_expr), size_expr_(size_expr) {}

  void Format(FormatNode* node, const FormatOptions& options, fxl::RefPtr<EvalContext> context,
              fit::deferred_callback cb) override;
  EvalArrayFunction GetArrayAccess() const override;

 private:
  const std::string ptr_expr_;
  const std::string size_expr_;
};

// For pretty-printing smart pointers.
//
// This has an expression that evaluates to a single pointer. This pointer is the result of the
// operation and the object will be formatted like a bare pointer using that value.
class PrettyPointer : public PrettyType {
 public:
  PrettyPointer(const std::string& expr,
                std::initializer_list<std::pair<std::string, std::string>> getters = {})
      : PrettyType(std::move(getters)), expr_(expr) {}

  void Format(FormatNode* node, const FormatOptions& options, fxl::RefPtr<EvalContext> context,
              fit::deferred_callback cb) override;
  EvalFunction GetDereferencer() const override;

 private:
  const std::string expr_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_PRETTY_TYPE_H_
