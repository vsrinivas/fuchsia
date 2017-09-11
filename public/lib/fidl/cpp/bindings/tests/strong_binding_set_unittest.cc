// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/bindings/strong_binding_set.h"

#include "gtest/gtest.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fxl/macros.h"
#include "mojo/public/cpp/utility/run_loop.h"
#include "lib/fidl/compiler/interfaces/tests/minimal_interface.fidl.h"

namespace fidl {
namespace {

class MinimalInterfaceImpl : public test::MinimalInterface {
 public:
  explicit MinimalInterfaceImpl(bool* deleted_flag)
      : deleted_flag_(deleted_flag) {}
  ~MinimalInterfaceImpl() override { *deleted_flag_ = true; }

  void Message() override { call_count_++; }

  int call_count() const { return call_count_; }

 private:
  bool* deleted_flag_;
  int call_count_ = 0;

  FXL_DISALLOW_COPY_AND_ASSIGN(MinimalInterfaceImpl);
};

// Tests all of the functionality of StrongBindingSet.
TEST(StrongBindingSetTest, FullLifeCycle) {
  RunLoop loop;

  // Create 10 InterfacePtrs and MinimalInterfaceImpl.
  const size_t kNumObjects = 10;
  InterfacePtr<test::MinimalInterface> intrfc_ptrs[kNumObjects];
  MinimalInterfaceImpl* impls[kNumObjects];
  bool deleted_flags[kNumObjects] = {};

  // Create 10 channels, bind everything together, and add the
  // bindings to binding_set.
  StrongBindingSet<test::MinimalInterface> binding_set;
  EXPECT_EQ(0u, binding_set.size());
  for (size_t i = 0; i < kNumObjects; i++) {
    impls[i] = new MinimalInterfaceImpl(&deleted_flags[i]);
    binding_set.AddBinding(impls[i], intrfc_ptrs[i].NewRequest());
  }
  EXPECT_EQ(kNumObjects, binding_set.size());

  // Check that initially all call counts are zero.
  for (const auto& impl : impls) {
    EXPECT_EQ(0, impl->call_count());
  }

  // Invoke method foo() on all 10 InterfacePointers.
  for (InterfacePtr<test::MinimalInterface>& ptr : intrfc_ptrs) {
    ptr->Message();
  }

  // Check that now all call counts are one.
  loop.RunUntilIdle();
  for (const auto& impl : impls) {
    EXPECT_EQ(1, impl->call_count());
  }

  // Close the first 5 channels and destroy the first five
  // InterfacePtrs.
  for (size_t i = 0; i < kNumObjects / 2; i++) {
    intrfc_ptrs[i].reset();
  }

  // Check that the set contains only five elements now.
  loop.RunUntilIdle();
  EXPECT_EQ(kNumObjects / 2, binding_set.size());

  // Check that the first 5 interfaces have all been deleted.
  for (size_t i = 0; i < kNumObjects; i++) {
    bool expected = (i < kNumObjects / 2);
    EXPECT_EQ(expected, deleted_flags[i]);
  }

  // Invoke method foo() on the second five InterfacePointers.
  for (size_t i = kNumObjects / 2; i < kNumObjects; i++) {
    intrfc_ptrs[i]->Message();
  }
  loop.RunUntilIdle();

  // Check that now the second five counts are two.
  for (size_t i = kNumObjects / 2; i < kNumObjects; i++) {
    EXPECT_EQ(2, impls[i]->call_count());
  }

  // Invoke CloseAllBindings
  binding_set.CloseAllBindings();
  EXPECT_EQ(0u, binding_set.size());

  // Invoke method foo() on the second five InterfacePointers.
  for (size_t i = kNumObjects / 2; i < kNumObjects; i++) {
    intrfc_ptrs[i]->Message();
  }
  loop.RunUntilIdle();

  // Check that all interfaces have all been deleted.
  for (size_t i = 0; i < kNumObjects; i++) {
    EXPECT_TRUE(deleted_flags[i]);
  }
}

}  // namespace
}  // namespace fidl
