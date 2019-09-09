// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/flatland.h"

#include <gtest/gtest.h>

#include "src/lib/fxl/logging.h"

using flatland::Flatland;
using fuchsia::ui::scenic::internal::Flatland_Present_Result;

namespace {

#define PRESENT(expect_success)                                                       \
  {                                                                                   \
    bool processed_callback = false;                                                  \
    flatland.Present([&](Flatland_Present_Result result) {                            \
      ASSERT_EQ(!expect_success, result.is_err());                                    \
      if (expect_success)                                                             \
        EXPECT_EQ(1u, result.response().num_presents_remaining);                      \
      else                                                                            \
        EXPECT_EQ(fuchsia::ui::scenic::internal::Error::BAD_OPERATION, result.err()); \
      processed_callback = true;                                                      \
    });                                                                               \
    EXPECT_TRUE(processed_callback);                                                  \
  }

}  // namespace

namespace scenic_impl {
namespace test {

TEST(FlatlandTest, PresentShouldReturnOne) {
  Flatland flatland;
  PRESENT(true);
}

TEST(FlatlandTest, CreateAndReleaseTransformValidCases) {
  Flatland flatland;

  const uint64_t kId1 = 1;
  const uint64_t kId2 = 2;

  // Create two transforms.
  flatland.CreateTransform(kId1);
  flatland.CreateTransform(kId2);
  PRESENT(true);

  // Clear, then create two transforms in the other order.
  flatland.ClearGraph();
  flatland.CreateTransform(kId2);
  flatland.CreateTransform(kId1);
  PRESENT(true);

  // Clear, create and release transforms, non-overlapping.
  flatland.ClearGraph();
  flatland.CreateTransform(kId1);
  flatland.ReleaseTransform(kId1);
  flatland.CreateTransform(kId2);
  flatland.ReleaseTransform(kId2);
  PRESENT(true);

  // Clear, create and release transforms, nested.
  flatland.ClearGraph();
  flatland.CreateTransform(kId2);
  flatland.CreateTransform(kId1);
  flatland.ReleaseTransform(kId1);
  flatland.ReleaseTransform(kId2);
  PRESENT(true);

  // Reuse the same id, legally, in a single present call.
  flatland.CreateTransform(kId1);
  flatland.ReleaseTransform(kId1);
  flatland.CreateTransform(kId1);
  flatland.ClearGraph();
  flatland.CreateTransform(kId1);
  PRESENT(true);

  // Create and clear, overlapping, with multiple present calls.
  flatland.ClearGraph();
  flatland.CreateTransform(kId2);
  PRESENT(true);
  flatland.CreateTransform(kId1);
  flatland.ReleaseTransform(kId2);
  PRESENT(true);
  flatland.ReleaseTransform(kId1);
  PRESENT(true);
}

TEST(FlatlandTest, CreateAndReleaseTransformErrorCases) {
  Flatland flatland;

  const uint64_t kId1 = 1;
  const uint64_t kId2 = 2;

  // Zero is not a valid transform id.
  flatland.CreateTransform(0);
  PRESENT(false);
  flatland.ReleaseTransform(0);
  PRESENT(false);

  // Double creation is an error.
  flatland.CreateTransform(kId1);
  flatland.CreateTransform(kId1);
  PRESENT(false);

  // Releasing a non-existent transform is an error.
  flatland.ReleaseTransform(kId2);
  PRESENT(false);
}

TEST(FlatlandTest, AddAndRemoveChildValidCases) {
  Flatland flatland;

  const uint64_t kIdParent = 1;
  const uint64_t kIdChild1 = 2;
  const uint64_t kIdChild2 = 3;
  const uint64_t kIdGrandchild = 4;

  flatland.CreateTransform(kIdParent);
  flatland.CreateTransform(kIdChild1);
  flatland.CreateTransform(kIdChild2);
  flatland.CreateTransform(kIdGrandchild);
  PRESENT(true);

  // Add and remove.
  flatland.AddChild(kIdParent, kIdChild1);
  flatland.RemoveChild(kIdParent, kIdChild1);
  PRESENT(true);

  // Add two children.
  flatland.AddChild(kIdParent, kIdChild1);
  flatland.AddChild(kIdParent, kIdChild2);
  PRESENT(true);

  // Remove two children.
  flatland.RemoveChild(kIdParent, kIdChild1);
  flatland.RemoveChild(kIdParent, kIdChild2);
  PRESENT(true);

  // Add two-deep hierarchy.
  flatland.AddChild(kIdParent, kIdChild1);
  flatland.AddChild(kIdChild1, kIdGrandchild);
  PRESENT(true);

  // Add sibling.
  flatland.AddChild(kIdParent, kIdChild2);
  PRESENT(true);

  // Add shared grandchild (deadly diamond dependency).
  flatland.AddChild(kIdChild2, kIdGrandchild);
  PRESENT(true);

  // Remove original deep-hierarchy.
  flatland.RemoveChild(kIdChild1, kIdGrandchild);
  PRESENT(true);
}

TEST(FlatlandTest, AddAndRemoveChildErrorCases) {
  Flatland flatland;

  const uint64_t kIdParent = 1;
  const uint64_t kIdChild = 2;
  const uint64_t kIdNotCreated = 3;

  // Setup.
  flatland.CreateTransform(kIdParent);
  flatland.CreateTransform(kIdChild);
  flatland.AddChild(kIdParent, kIdChild);
  PRESENT(true);

  // Zero is not a valid transform id.
  flatland.AddChild(0, 0);
  PRESENT(false);
  flatland.AddChild(kIdParent, 0);
  PRESENT(false);
  flatland.AddChild(0, kIdChild);
  PRESENT(false);

  // Child does not exist.
  flatland.AddChild(kIdParent, kIdNotCreated);
  PRESENT(false);
  flatland.RemoveChild(kIdParent, kIdNotCreated);
  PRESENT(false);

  // Parent does not exist.
  flatland.AddChild(kIdNotCreated, kIdChild);
  PRESENT(false);
  flatland.RemoveChild(kIdNotCreated, kIdChild);
  PRESENT(false);

  // Child is already a child of parent.
  flatland.AddChild(kIdParent, kIdChild);
  PRESENT(false);

  // Both nodes exist, but not in the correct relationship.
  flatland.RemoveChild(kIdChild, kIdParent);
  PRESENT(false);
}

TEST(FlatlandTest, MultichildUsecase) {
  Flatland flatland;

  const uint64_t kIdParent1 = 1;
  const uint64_t kIdParent2 = 2;
  const uint64_t kIdChild1 = 3;
  const uint64_t kIdChild2 = 4;
  const uint64_t kIdChild3 = 5;

  // Setup
  flatland.CreateTransform(kIdParent1);
  flatland.CreateTransform(kIdParent2);
  flatland.CreateTransform(kIdChild1);
  flatland.CreateTransform(kIdChild2);
  flatland.CreateTransform(kIdChild3);
  PRESENT(true);

  // Add all children to first parent.
  flatland.AddChild(kIdParent1, kIdChild1);
  flatland.AddChild(kIdParent1, kIdChild2);
  flatland.AddChild(kIdParent1, kIdChild3);
  PRESENT(true);

  // Add all children to second parent.
  flatland.AddChild(kIdParent2, kIdChild1);
  flatland.AddChild(kIdParent2, kIdChild2);
  flatland.AddChild(kIdParent2, kIdChild3);
  PRESENT(true);
}

TEST(FlatlandTest, CycleDetector) {
  Flatland flatland;

  const uint64_t kId1 = 1;
  const uint64_t kId2 = 2;
  const uint64_t kId3 = 3;
  const uint64_t kId4 = 4;

  // Create an immediate cycle.
  {
    flatland.CreateTransform(kId1);
    flatland.AddChild(kId1, kId1);
    PRESENT(false);
  }

  // Create a legal chain of depth one.
  // Then, create a cycle of length 2.
  {
    flatland.ClearGraph();
    flatland.CreateTransform(kId1);
    flatland.CreateTransform(kId2);
    flatland.AddChild(kId1, kId2);
    PRESENT(true);

    flatland.AddChild(kId2, kId1);
    PRESENT(false);
  }

  // Create two legal chains of length one.
  // Then, connect each chain into a cycle of length four.
  {
    flatland.ClearGraph();
    flatland.CreateTransform(kId1);
    flatland.CreateTransform(kId2);
    flatland.CreateTransform(kId3);
    flatland.CreateTransform(kId4);
    flatland.AddChild(kId1, kId2);
    flatland.AddChild(kId3, kId4);
    PRESENT(true);

    flatland.AddChild(kId2, kId3);
    flatland.AddChild(kId4, kId1);
    PRESENT(false);
  }

  // Create a cycle, where the root is not involved in the cycle.
  {
    flatland.ClearGraph();
    flatland.CreateTransform(kId1);
    flatland.CreateTransform(kId2);
    flatland.CreateTransform(kId3);
    flatland.CreateTransform(kId4);

    flatland.AddChild(kId1, kId2);
    flatland.AddChild(kId2, kId3);
    flatland.AddChild(kId3, kId2);
    flatland.AddChild(kId3, kId4);

    flatland.SetRootTransform(kId1);
    flatland.ReleaseTransform(kId1);
    flatland.ReleaseTransform(kId2);
    flatland.ReleaseTransform(kId3);
    flatland.ReleaseTransform(kId4);
    PRESENT(false);
  }
}

TEST(FlatlandTest, SetRootTransform) {
  Flatland flatland;

  const uint64_t kId1 = 1;
  const uint64_t kIdNotCreated = 2;

  flatland.CreateTransform(kId1);
  PRESENT(true);

  // Even with no root transform, so clearing it is not an error.
  flatland.SetRootTransform(0);
  PRESENT(true);

  // Setting the root to an unknown transform is an error.
  flatland.SetRootTransform(kIdNotCreated);
  PRESENT(false);

  flatland.SetRootTransform(kId1);
  PRESENT(true);

  // Releasing the root is allowed.
  flatland.ReleaseTransform(kId1);
  PRESENT(true);

  // Clearing the root after release is also allowed.
  flatland.SetRootTransform(0);
  PRESENT(true);

  // Setting the root to a released transform is not allowed.
  flatland.SetRootTransform(kId1);
  PRESENT(false);
}

}  // namespace test
}  // namespace scenic_impl
