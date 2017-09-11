// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/bindings/interface_ptr_set.h"

#include "gtest/gtest.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/tests/util/test_waiter.h"
#include "lib/fxl/macros.h"
#include "lib/fidl/compiler/interfaces/tests/minimal_interface.fidl.h"

namespace fidl {
namespace test {
namespace {

class MinimalInterfaceImpl : public MinimalInterface {
 public:
  explicit MinimalInterfaceImpl(
      InterfaceRequest<MinimalInterface> request)
      : binding_(this, std::move(request)) {}

  void Message() override { call_count_++; }

  void CloseMessagePipe() { binding_.Close(); }

  int call_count() { return call_count_; }

 private:
  Binding<MinimalInterface> binding_;
  int call_count_ = 0;

  FXL_DISALLOW_COPY_AND_ASSIGN(MinimalInterfaceImpl);
};

// Tests all of the functionality of InterfacePtrSet.
TEST(InterfacePtrSetTest, FullLifeCycle) {
  // Create 10 InterfacePtrs.
  const size_t kNumObjects = 10;
  InterfacePtr<MinimalInterface> intrfc_ptrs[kNumObjects];

  // Create 10 MinimalInterfaceImpls and 10 channels and bind them all
  // together.
  std::unique_ptr<MinimalInterfaceImpl> impls[kNumObjects];
  for (size_t i = 0; i < kNumObjects; i++) {
    impls[i].reset(new MinimalInterfaceImpl(intrfc_ptrs[i].NewRequest()));
  }

  // Move all 10 InterfacePtrs into the set.
  InterfacePtrSet<MinimalInterface> intrfc_ptr_set;
  EXPECT_EQ(0u, intrfc_ptr_set.size());
  for (InterfacePtr<MinimalInterface>& ptr : intrfc_ptrs) {
    intrfc_ptr_set.AddInterfacePtr(std::move(ptr));
  }
  EXPECT_EQ(kNumObjects, intrfc_ptr_set.size());

  // Check that initially all call counts are zero.
  for (const std::unique_ptr<MinimalInterfaceImpl>& impl : impls) {
    EXPECT_EQ(0, impl->call_count());
  }

  // Invoke ForAllPtrs().
  size_t num_invocations = 0;
  intrfc_ptr_set.ForAllPtrs([&num_invocations](MinimalInterface* dummy) {
    dummy->Message();
    num_invocations++;
  });
  EXPECT_EQ(kNumObjects, num_invocations);

  // Check that now all call counts are one.
  WaitForAsyncWaiter();
  for (const std::unique_ptr<MinimalInterfaceImpl>& impl : impls) {
    EXPECT_EQ(1, impl->call_count());
  }

  // Close the first 5 channels. This will (after WaitForAsyncWaiter) cause
  // connection errors on the closed pipes which will cause the first five
  // objects to be removed.
  for (size_t i = 0; i < kNumObjects / 2; i++) {
    impls[i]->CloseMessagePipe();
  }
  EXPECT_EQ(kNumObjects, intrfc_ptr_set.size());
  WaitForAsyncWaiter();
  EXPECT_EQ(kNumObjects / 2, intrfc_ptr_set.size());

  // Invoke ForAllPtrs again on the remaining five pointers
  intrfc_ptr_set.ForAllPtrs(
      [](MinimalInterface* dummy) { dummy->Message(); });
  WaitForAsyncWaiter();

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
      [](MinimalInterface* dummy) { dummy->Message(); });
  WaitForAsyncWaiter();

  // Check that the counts are the same as last time.
  for (size_t i = 0; i < kNumObjects; i++) {
    int expected = (i < kNumObjects / 2 ? 1 : 2);
    EXPECT_EQ(expected, impls[i]->call_count());
  }
  EXPECT_EQ(0u, intrfc_ptr_set.size());
}

}  // namespace
}  // namespace test
}  // namespace fidl
