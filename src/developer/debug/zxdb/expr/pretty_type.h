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
  using EvalFunction = fit::function<void(const fxl::RefPtr<EvalContext>&,
                                          const ExprValue& object_value, EvalCallback)>;
  using EvalArrayFunction = fit::function<void(
      const fxl::RefPtr<EvalContext>&, const ExprValue& object_value, int64_t index, EvalCallback)>;

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
                      const fxl::RefPtr<EvalContext>& context, fit::deferred_callback cb) = 0;

  // Returns a function which can be evaluated to execute a getter on an object of this type.
  // If there is no matching getter, a null fit::function is returned.
  //
  // (Implementation note: this design is so the caller can check if there is a getter and then
  // execute it with a callback, which is how most nodes want to run.)
  virtual EvalFunction GetGetter(const std::string& getter_name) const;

  // Returns a function which can be evaluated to fetch a member variable from an object of this
  // type. If there is no matching member, a null fit::function is returned.
  virtual EvalFunction GetMember(const std::string& member_name) const { return EvalFunction(); }

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
  static void EvalExpressionOn(const fxl::RefPtr<EvalContext>& context, const ExprValue& object,
                               const std::string& expression, EvalCallback cb);

 private:
  // Registered getter functions and the expressions they map to.
  std::map<std::string, std::string> getters_;
};

class PrettyArray : public PrettyType {
 public:
  PrettyArray(const std::string& ptr_expr, const std::string& size_expr,
              std::initializer_list<std::pair<std::string, std::string>> getters = {})
      : PrettyType(getters), ptr_expr_(ptr_expr), size_expr_(size_expr) {}

  void Format(FormatNode* node, const FormatOptions& options,
              const fxl::RefPtr<EvalContext>& context, fit::deferred_callback cb) override;
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

  void Format(FormatNode* node, const FormatOptions& options,
              const fxl::RefPtr<EvalContext>& context, fit::deferred_callback cb) override;
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
      : PrettyType(getters), expr_(expr) {}

  void Format(FormatNode* node, const FormatOptions& options,
              const fxl::RefPtr<EvalContext>& context, fit::deferred_callback cb) override;
  EvalFunction GetDereferencer() const override;

 private:
  const std::string expr_;
};

// Implements pretty-printing for "std::optional" and similar classes that can have a value or not.
class PrettyOptional : public PrettyType {
 public:
  // Evaluates the |is_engaged_expr|. If engaged (nonempty), show the result of evaluating the
  // |value_expr| which retrieves the value. If invalid the description of this item will be
  // |name_when_disengaged|.
  //
  // The is_engaged_expr should evaluate to a boolean or integer that's either zero or nonzero.
  PrettyOptional(const std::string& simple_type_name, const std::string& is_engaged_expr,
                 const std::string& value_expr, const std::string& name_when_disengaged,
                 std::initializer_list<std::pair<std::string, std::string>> getters)
      : PrettyType(getters),
        simple_type_name_(simple_type_name),
        is_engaged_expr_(is_engaged_expr),
        value_expr_(value_expr),
        name_when_disengaged_(name_when_disengaged) {}

  void Format(FormatNode* node, const FormatOptions& options,
              const fxl::RefPtr<EvalContext>& context, fit::deferred_callback cb) override;
  EvalFunction GetDereferencer() const override;

 private:
  // Executes the callback for the given optional struct. This takes the expression and executes the
  // callback which can be an error, is_disengaged, or have a value.
  static void EvalOptional(const fxl::RefPtr<EvalContext>& context, ExprValue object,
                           const std::string& is_engaged_expr, const std::string& value_expr,
                           fit::callback<void(ErrOrValue, bool is_disengaged)> cb);

  const std::string simple_type_name_;
  const std::string is_engaged_expr_;
  const std::string value_expr_;
  const std::string name_when_disengaged_;
};

// Represents a simplified structure with a list of members. This is used to map a complicated
// struct (perhaps with non-normally-relevant members or inheritance) to a simpler presentation.
class PrettyStruct : public PrettyType {
 public:
  // Takes a list of struct member names and the expressions that evaluate them.
  PrettyStruct(std::initializer_list<std::pair<std::string, std::string>> members);

  void Format(FormatNode* node, const FormatOptions& options,
              const fxl::RefPtr<EvalContext>& context, fit::deferred_callback cb) override;

 private:
  std::vector<std::pair<std::string, std::string>> members_;
};

// Generic variants in C++ are normally implemented as a nested list of unions. This allows a
// generic number of possibilities for the variant value by using the type system to implement
// recursion.
class PrettyRecursiveVariant : public PrettyType {
 public:
  // To get to the value, this class constructs an expression based on the "index_expr" which is
  // an expression that should evaluate to an integer. The pattern will be:
  //   <base_expr> . ( <next_expr> * index ) . <value_expr>
  // So if index_expr evaluates to 2 and given simple neams for each item, it will produce:
  //   "base.next.next.value"
  //
  // If the index casted to a signed integer is negative, the value will be reported as the
  // "no_value_string".
  PrettyRecursiveVariant(const std::string& simple_type_name, const std::string& base_expr,
                         const std::string& index_expr, const std::string& next_expr,
                         const std::string& value_expr, const std::string& no_value_string,
                         std::initializer_list<std::pair<std::string, std::string>> getters)
      : PrettyType(getters),
        simple_type_name_(simple_type_name),
        base_expr_(base_expr),
        index_expr_(index_expr),
        next_expr_(next_expr),
        value_expr_(value_expr),
        no_value_string_(no_value_string) {}

  void Format(FormatNode* node, const FormatOptions& options,
              const fxl::RefPtr<EvalContext>& context, fit::deferred_callback cb) override;

 private:
  const std::string simple_type_name_;
  const std::string base_expr_;
  const std::string index_expr_;
  const std::string next_expr_;
  const std::string value_expr_;
  const std::string no_value_string_;
};

// Pretty-printer for a value inside some kind of container. This acts like a smart pointer but the
// contained value isn't a pointer. This is for thigs like std::atomic or std::reference_wrapper.
//
// Currently this is formatted like "typename(value)". For some types it might be nice to format
// them just as the value, but the confusing part is that it won't behave exactly like the value in
// expressions.
class PrettyWrappedValue : public PrettyType {
 public:
  PrettyWrappedValue(const std::string& name, const std::string& open_bracket,
                     const std::string& close_bracket, const std::string& expression)
      : name_(name),
        open_bracket_(open_bracket),
        close_bracket_(close_bracket),
        expression_(expression) {}

  void Format(FormatNode* node, const FormatOptions& options,
              const fxl::RefPtr<EvalContext>& context, fit::deferred_callback cb) override;

 private:
  const std::string name_;
  const std::string open_bracket_;
  const std::string close_bracket_;
  const std::string expression_;
};

// Decodes a zx_status_t to the #define value.
class PrettyZxStatusT : public PrettyType {
 public:
  PrettyZxStatusT();

  void Format(FormatNode* node, const FormatOptions& options,
              const fxl::RefPtr<EvalContext>& context, fit::deferred_callback cb) override;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_PRETTY_TYPE_H_
