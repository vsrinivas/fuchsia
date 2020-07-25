// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/builder.h>
#include <lib/fidl/llcpp/memory.h>
#include <lib/fidl/llcpp/string_view.h>
#include <lib/fidl/llcpp/vector_view.h>

#include <zxtest/zxtest.h>

namespace {

TEST(CppTypesTests, string_view_test) {
  char buffer[ZX_CHANNEL_MAX_MSG_BYTES];
  fidl::Builder builder(buffer, ZX_CHANNEL_MAX_MSG_BYTES);

  fidl::StringView* view = builder.New<fidl::StringView>();
  EXPECT_TRUE(view->empty());
  EXPECT_TRUE(view->is_null());

  char* data = builder.NewArray<char>(3);
  view->set_data(fidl::unowned_ptr(data));
  view->set_size(3);

  EXPECT_FALSE(view->empty());
  EXPECT_EQ(view->size(), 3);
  EXPECT_EQ(view->data(), data);

  EXPECT_EQ(view->at(1), 0);
}

TEST(CppTypesTests, vector_view_test) {
  char buffer[ZX_CHANNEL_MAX_MSG_BYTES];
  fidl::Builder builder(buffer, ZX_CHANNEL_MAX_MSG_BYTES);

  fidl::VectorView<int>* view = builder.New<fidl::VectorView<int>>();
  EXPECT_TRUE(view->empty());
  EXPECT_TRUE(view->data() == nullptr);

  int* data = builder.NewArray<int>(3);
  view->set_data(fidl::unowned_ptr(data));
  view->set_count(3);

  EXPECT_EQ(view->count(), 3);
  EXPECT_EQ(view->data(), data);

  EXPECT_EQ(view->at(1), 0);
}

}  // namespace
