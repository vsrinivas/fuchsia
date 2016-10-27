// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/bindings/binding_set.h"

#include "gtest/gtest.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/ftl/macros.h"
#include "mojo/public/cpp/utility/run_loop.h"
#include "mojo/public/interfaces/bindings/tests/minimal_interface.mojom.h"

namespace fidl {
namespace {

class MinimalInterfaceImpl : public test::MinimalInterface {
 public:
  MinimalInterfaceImpl() {}

  void Message() override { call_count_++; }

  int call_count() const { return call_count_; }

 private:
  int call_count_ = 0;

  FTL_DISALLOW_COPY_AND_ASSIGN(MinimalInterfaceImpl);
};

// Tests all of the functionality of BindingSet.
TEST(BindingSetTest, FullLifeCycle) {
  RunLoop loop;

  // Create 10 InterfacePtrs and MinimalInterfaceImpls.
  const size_t kNumObjects = 10;
  InterfacePtr<test::MinimalInterface> interface_pointers[kNumObjects];
  MinimalInterfaceImpl impls[kNumObjects];

  // Create 10 message pipes, bind everything together, and add the
  // bindings to binding_set.
  BindingSet<test::MinimalInterface> binding_set;
  EXPECT_EQ(0u, binding_set.size());
  for (size_t i = 0; i < kNumObjects; i++) {
    if (i % 2 == 0)
      binding_set.AddBinding(&impls[i], GetProxy(&interface_pointers[i]));
    else
      interface_pointers[i] =
          fidl::InterfacePtr<test::MinimalInterface>::Create(
              binding_set.AddBinding(&impls[i]).Pass());
  }
  EXPECT_EQ(kNumObjects, binding_set.size());

  // Check that initially all call counts are zero.
  for (const auto& impl : impls) {
    EXPECT_EQ(0, impl.call_count());
  }

  // Invoke method foo() on all 10 InterfacePointers.
  for (InterfacePtr<test::MinimalInterface>& ptr : interface_pointers) {
    ptr->Message();
  }

  // Check that now all call counts are one.
  loop.RunUntilIdle();
  for (const auto& impl : impls) {
    EXPECT_EQ(1, impl.call_count());
  }

  // Close the first 5 message pipes and destroy the first five
  // InterfacePtrs.
  for (size_t i = 0; i < kNumObjects / 2; i++) {
    interface_pointers[i].reset();
  }

  // Check that the set contains only five elements now.
  loop.RunUntilIdle();
  EXPECT_EQ(kNumObjects / 2, binding_set.size());

  // Invoke method foo() on the second five InterfacePointers.
  for (size_t i = kNumObjects / 2; i < kNumObjects; i++) {
    interface_pointers[i]->Message();
  }
  loop.RunUntilIdle();

  // Check that now the first five counts are still 1 but the second five
  // counts are two.
  for (size_t i = 0; i < kNumObjects; i++) {
    int expected = (i < kNumObjects / 2 ? 1 : 2);
    EXPECT_EQ(expected, impls[i].call_count());
  }

  // Invoke CloseAllBindings
  binding_set.CloseAllBindings();
  EXPECT_EQ(0u, binding_set.size());

  // Invoke method foo() on the second five InterfacePointers.
  for (size_t i = kNumObjects / 2; i < kNumObjects; i++) {
    interface_pointers[i]->Message();
  }
  loop.RunUntilIdle();

  // Check that the call counts are the same as before.
  for (size_t i = 0; i < kNumObjects; i++) {
    int expected = (i < kNumObjects / 2 ? 1 : 2);
    EXPECT_EQ(expected, impls[i].call_count());
  }
}

}  // namespace
}  // namespace fidl
