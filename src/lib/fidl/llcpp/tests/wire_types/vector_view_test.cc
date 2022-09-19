// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/wire/vector_view.h>

#include <gtest/gtest.h>

TEST(VectorView, DefaultConstructor) {
  fidl::VectorView<int32_t> vv;
  EXPECT_EQ(vv.count(), 0ULL);
  EXPECT_TRUE(vv.empty());
  EXPECT_EQ(vv.data(), nullptr);
}

struct DestructionState {
  bool destructor_called = false;
};
struct DestructableObject {
  DestructableObject() : ds(nullptr) {}
  DestructableObject(DestructionState* ds) : ds(ds) {}

  ~DestructableObject() { ds->destructor_called = true; }

  DestructionState* ds;
};

TEST(VectorView, PointerConstructor) {
  DestructionState ds[3] = {};
  DestructableObject arr[3] = {&ds[0], &ds[1], &ds[2]};
  {
    auto vv = fidl::VectorView<DestructableObject>::FromExternal(arr, 2);
    EXPECT_EQ(vv.count(), 2ULL);
    EXPECT_FALSE(vv.empty());
    EXPECT_EQ(vv.data(), arr);
  }
  EXPECT_FALSE(ds[0].destructor_called);
  EXPECT_FALSE(ds[1].destructor_called);
  EXPECT_FALSE(ds[1].destructor_called);
}

TEST(VectorView, MoveConstructorUnowned) {
  std::vector<int32_t> vec{1, 2, 3};
  auto vv = fidl::VectorView<int32_t>::FromExternal(vec);
  fidl::VectorView<int32_t> moved_vv(std::move(vv));
  EXPECT_EQ(vv.count(), 3ULL);
  EXPECT_EQ(vv.data(), vec.data());
  EXPECT_EQ(moved_vv.count(), 3ULL);
  EXPECT_EQ(moved_vv.data(), vec.data());
}

TEST(VectorView, MoveAssigmentUnowned) {
  std::vector<int32_t> vec{1, 2, 3};
  auto vv = fidl::VectorView<int32_t>::FromExternal(vec);
  fidl::VectorView<int32_t> moved_vv;
  moved_vv = std::move(vv);
  EXPECT_EQ(vv.count(), 3ULL);
  EXPECT_EQ(vv.data(), vec.data());
  EXPECT_EQ(moved_vv.count(), 3ULL);
  EXPECT_EQ(moved_vv.data(), vec.data());
}

TEST(VectorView, Iteration) {
  std::vector<int32_t> vec{1, 2, 3};
  auto vv = fidl::VectorView<int32_t>::FromExternal(vec);
  int32_t i = 1;
  for (auto& val : vv) {
    EXPECT_EQ(&val, &vec.at(i - 1));
    ++i;
  }
  EXPECT_EQ(i, 4);
}

TEST(VectorView, Indexing) {
  std::vector<int32_t> vec{1, 2, 3};
  auto vv = fidl::VectorView<int32_t>::FromExternal(vec);
  for (uint64_t i = 0; i < vv.count(); i++) {
    EXPECT_EQ(&vv[i], &vec.at(i));
  }
}

TEST(VectorView, Mutations) {
  std::vector<int32_t> vec{1, 2, 3};
  auto vv = fidl::VectorView<int32_t>::FromExternal(vec);
  vv.set_count(2);
  *vv.data() = 4;
  vv[1] = 5;
  EXPECT_EQ(vv.count(), 2ULL);
  EXPECT_EQ(vv.data(), vec.data());
  EXPECT_EQ(vv[0], 4);
  EXPECT_EQ(vv[1], 5);
  EXPECT_EQ(vec[0], 4);
  EXPECT_EQ(vec[1], 5);
}

TEST(VectorView, CopyFromStdVector) {
  fidl::Arena arena;
  std::vector<int32_t> vec{1, 2, 3};
  fidl::VectorView vv{arena, vec};
  vec[0] = 0;
  vec[1] = 0;
  vec[2] = 0;
  EXPECT_EQ(vv.count(), 3ULL);
  EXPECT_EQ(vv[0], 1);
  EXPECT_EQ(vv[1], 2);
  EXPECT_EQ(vv[2], 3);
}

TEST(VectorView, CopyFromStdSpan) {
  fidl::Arena arena;
  std::vector<int32_t> vec{1, 2, 3};
  cpp20::span<int32_t> span{vec};
  fidl::VectorView vv{arena, span};
  vec[0] = 0;
  vec[1] = 0;
  vec[2] = 0;
  EXPECT_EQ(vv.count(), 3ULL);
  EXPECT_EQ(vv[0], 1);
  EXPECT_EQ(vv[1], 2);
  EXPECT_EQ(vv[2], 3);
}

TEST(VectorView, CopyFromConstStdSpan) {
  fidl::Arena arena;
  std::vector<int32_t> vec{1, 2, 3};
  cpp20::span<const int32_t> span{vec};
  fidl::VectorView vv{arena, span};
  vec[0] = 0;
  vec[1] = 0;
  vec[2] = 0;
  EXPECT_EQ(vv.count(), 3ULL);
  EXPECT_EQ(vv[0], 1);
  EXPECT_EQ(vv[1], 2);
  EXPECT_EQ(vv[2], 3);
}

TEST(VectorView, CopyFromIterators) {
  fidl::Arena arena;
  std::vector<int32_t> vec{1, 2, 3};
  cpp20::span<int32_t> span{vec};
  fidl::VectorView<int32_t> vv{arena, span.begin(), span.end()};
  vec[0] = 0;
  vec[1] = 0;
  vec[2] = 0;
  EXPECT_EQ(vv.count(), 3ULL);
  EXPECT_EQ(vv[0], 1);
  EXPECT_EQ(vv[1], 2);
  EXPECT_EQ(vv[2], 3);
}

TEST(VectorView, CopyFromConstIterators) {
  fidl::Arena arena;
  std::vector<int32_t> vec{1, 2, 3};
  cpp20::span<const int32_t> span{vec};
  fidl::VectorView<int32_t> vv{arena, span.begin(), span.end()};
  vec[0] = 0;
  vec[1] = 0;
  vec[2] = 0;
  EXPECT_EQ(vv.count(), 3ULL);
  EXPECT_EQ(vv[0], 1);
  EXPECT_EQ(vv[1], 2);
  EXPECT_EQ(vv[2], 3);
}

#if 0
TEST(VectorView, BadIterators) {
  fidl::Arena arena;
  fidl::VectorView<int32_t> vv{arena, 1, 2};
}
#endif
