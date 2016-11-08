// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/bindings/interface_ptr_set.h"

#include "gtest/gtest.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/ftl/macros.h"
#include "mojo/public/cpp/utility/run_loop.h"
#include "mojo/public/interfaces/bindings/tests/minimal_interface.mojom.h"

namespace fidl {
namespace {

class MinimalInterfaceImpl : public test::MinimalInterface {
 public:
  explicit MinimalInterfaceImpl(
      InterfaceRequest<test::MinimalInterface> request)
      : binding_(this, request.Pass()) {}

  void Message() override { call_count_++; }

  void CloseMessagePipe() { binding_.Close(); }

  int call_count() { return call_count_; }

 private:
  Binding<test::MinimalInterface> binding_;
  int call_count_ = 0;

  FTL_DISALLOW_COPY_AND_ASSIGN(MinimalInterfaceImpl);
};

// Tests all of the functionality of InterfacePtrSet.
TEST(InterfacePtrSetTest, FullLifeCycle) {
  RunLoop loop;

  // Create 10 InterfacePtrs.
  const size_t kNumObjects = 10;
  InterfacePtr<test::MinimalInterface> intrfc_ptrs[kNumObjects];

  // Create 10 MinimalInterfaceImpls and 10 channels and bind them all
  // together.
  std::unique_ptr<MinimalInterfaceImpl> impls[kNumObjects];
  for (size_t i = 0; i < kNumObjects; i++) {
    impls[i].reset(new MinimalInterfaceImpl(GetProxy(&intrfc_ptrs[i])));
  }

  // Move all 10 InterfacePtrs into the set.
  InterfacePtrSet<test::MinimalInterface> intrfc_ptr_set;
  EXPECT_EQ(0u, intrfc_ptr_set.size());
  for (InterfacePtr<test::MinimalInterface>& ptr : intrfc_ptrs) {
    intrfc_ptr_set.AddInterfacePtr(ptr.Pass());
  }
  EXPECT_EQ(kNumObjects, intrfc_ptr_set.size());

  // Check that initially all call counts are zero.
  for (const std::unique_ptr<MinimalInterfaceImpl>& impl : impls) {
    EXPECT_EQ(0, impl->call_count());
  }

  // Invoke ForAllPtrs().
  size_t num_invocations = 0;
  intrfc_ptr_set.ForAllPtrs([&num_invocations](test::MinimalInterface* dummy) {
    dummy->Message();
    num_invocations++;
  });
  EXPECT_EQ(kNumObjects, num_invocations);

  // Check that now all call counts are one.
  loop.RunUntilIdle();
  for (const std::unique_ptr<MinimalInterfaceImpl>& impl : impls) {
    EXPECT_EQ(1, impl->call_count());
  }

  // Close the first 5 channels. This will (after RunUntilIdle) cause
  // connection errors on the closed pipes which will cause the first five
  // objects to be removed.
  for (size_t i = 0; i < kNumObjects / 2; i++) {
    impls[i]->CloseMessagePipe();
  }
  EXPECT_EQ(kNumObjects, intrfc_ptr_set.size());
  loop.RunUntilIdle();
  EXPECT_EQ(kNumObjects / 2, intrfc_ptr_set.size());

  // Invoke ForAllPtrs again on the remaining five pointers
  intrfc_ptr_set.ForAllPtrs(
      [](test::MinimalInterface* dummy) { dummy->Message(); });
  loop.RunUntilIdle();

  // Check that now the first five counts are still 1 but the second five
  // counts are two.
  for (size_t i = 0; i < kNumObjects; i++) {
    int expected = (i < kNumObjects / 2 ? 1 : 2);
    EXPECT_EQ(expected, impls[i]->call_count());
  }

  // Close all of the MessagePipes and clear the set.
  intrfc_ptr_set.CloseAll();

  // Invoke ForAllPtrs() again.
  intrfc_ptr_set.ForAllPtrs(
      [](test::MinimalInterface* dummy) { dummy->Message(); });
  loop.RunUntilIdle();

  // Check that the counts are the same as last time.
  for (size_t i = 0; i < kNumObjects; i++) {
    int expected = (i < kNumObjects / 2 ? 1 : 2);
    EXPECT_EQ(expected, impls[i]->call_count());
  }
  EXPECT_EQ(0u, intrfc_ptr_set.size());
}

}  // namespace
}  // namespace fidl
