// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_PRETTY_TREE_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_PRETTY_TREE_H_

#include "src/developer/debug/zxdb/expr/eval_callback.h"
#include "src/developer/debug/zxdb/expr/pretty_type.h"

namespace zxdb {

// Pretty printers for C++ STL "trees" (sets and maps).
//
//
// SET ITERATORS
// -------------
// In our STL a tree iterator for std::set<int> looks like this:
//
//   std::__2::__tree_const_iterator<int, std::__2::__tree_node<int, void *> *, long> {
//      __iter_pointer __ptr_;
//   }
//
// That pointer has a "left" member but no values. It's nonspecific to the tree type. To get the
// value you have to cast that pointer to a "__node_pointer" which looks like this:
//
//   std::__2::__tree_node<int, void *> {
//     std::__2::__tree_node_base<void *> {                              // BASE CLASS
//       std::__2::__tree_node_base<void *>::pointer __right_;
//       std::__2::__tree_node_base<void *>::__parent_pointer __parent_;
//       bool __is_black_;
//     }
//     std::__2::__tree_node<int, void *>::__node_value_type __value_;  // ACTUAL DATA
//   }
//
//
// MAP ITERATORS
// -------------
// A std::map::iterator is the same except the above structure is enclosed in another layer of
// indirection. The tree "value" is a "std::__2::__value_type<Key, Value>".
//
//   std::__2::__map_iterator<std::__2::__tree_iterator<std::__2::__value_type<Key, Value>, ... {
//     <std::__2::__tree_iterator<std::__2::__value_type<Key, Value>, ...> __i_;
//   }
//
//
// IMPLEMENTATION
// --------------
// This could be replaced with a PrettyPointer type if we have the ability to express "the type
// name of the type being pretty-printed" and "the name of a template parameter". Then the "pointer"
// of this class would then be
//
//   (MY_TYPE::__node_pointer)__ptr_                // for set
//   (TEMPLATE_PARAM_0::__node_pointer)__i_.__ptr_  // for map
//
// For now, this provides a dynamic expression to the PrettyPointer base class that we compute from
// the type name.

// std::set<*>::iterator
// A "tree" is the backing store for a set, which in turn "map" wraps.
class PrettyTreeIterator : public PrettyType {
 public:
  PrettyTreeIterator() : PrettyType() {}

  void Format(FormatNode* node, const FormatOptions& options, fxl::RefPtr<EvalContext> context,
              fit::deferred_callback cb) override;
  EvalFunction GetDereferencer() const override;

  // Computes the pointed-to value for the given iterator and calls the callback with it.
  static void GetIteratorValue(fxl::RefPtr<EvalContext> context, const ExprValue& iter,
                               fit::callback<void(const Err&, ExprValue)> cb);
};

// std::map<*>::iterator
class PrettyMapIterator : public PrettyType {
 public:
  PrettyMapIterator() : PrettyType() {}

  void Format(FormatNode* node, const FormatOptions& options, fxl::RefPtr<EvalContext> context,
              fit::deferred_callback cb) override;
  EvalFunction GetDereferencer() const override;

  // Computes the pointed-to value for the given iterator and calls the callback with it.
  static void GetIteratorValue(fxl::RefPtr<EvalContext> context, const ExprValue& iter,
                               fit::callback<void(const Err&, ExprValue)> cb);
};

// std::set and std::map.
class PrettyTree : public PrettyType {
 public:
  // The container name should be "std::set" or "std::map".
  PrettyTree(const std::string& container_name);

  void Format(FormatNode* node, const FormatOptions& options, fxl::RefPtr<EvalContext> context,
              fit::deferred_callback cb) override;

 private:
  std::string container_name_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_PRETTY_TREE_H_
