// Copyright 2014 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_BINDINGS_TESTS_UTIL_CONTAINER_TEST_UTIL_H_
#define LIB_FIDL_CPP_BINDINGS_TESTS_UTIL_CONTAINER_TEST_UTIL_H_

#include "lib/fidl/cpp/bindings/macros.h"
#include "lib/fidl/cpp/bindings/internal/template_util.h"
#include "lib/fxl/macros.h"

namespace fidl {

class CopyableType {
 public:
  CopyableType();
  CopyableType(const CopyableType& other);
  CopyableType& operator=(const CopyableType& other);
  ~CopyableType();

  bool copied() const { return copied_; }
  static size_t num_instances() { return num_instances_; }
  CopyableType* ptr() const { return ptr_; }
  void ResetCopied() { copied_ = false; }

 private:
  bool copied_;
  static size_t num_instances_;
  CopyableType* ptr_;
};

class MoveOnlyType {
 public:
  typedef MoveOnlyType Data_;
  MoveOnlyType();
  MoveOnlyType(MoveOnlyType&& other);
  MoveOnlyType& operator=(MoveOnlyType&& other);
  ~MoveOnlyType();

  bool moved() const { return moved_; }
  static size_t num_instances() { return num_instances_; }
  MoveOnlyType* ptr() const { return ptr_; }
  void ResetMoved() { moved_ = false; }

 private:
  bool moved_;
  static size_t num_instances_;
  MoveOnlyType* ptr_;

  FIDL_MOVE_ONLY_TYPE(MoveOnlyType);
};

}  // namespace fidl

#endif  // LIB_FIDL_CPP_BINDINGS_TESTS_UTIL_CONTAINER_TEST_UTIL_H_
