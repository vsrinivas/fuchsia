// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.examples/cpp/fidl.h>

#include <type_traits>

#include <gtest/gtest.h>

namespace {

// [START wire-external-object]
TEST(WireTypes, BorrowExternalObject) {
  fidl::StringView str("hello");
  // |object_view| is a view that borrows the string view.
  // Destroying |str| will invalidate |object_view|.
  fidl::ObjectView object_view = fidl::ObjectView<fidl::StringView>::FromExternal(&str);
  // |object_view| may be dereferenced to access the pointee.
  ASSERT_EQ(object_view->begin(), str.begin());
}
// [END wire-external-object]

// [START wire-external-vector]
TEST(WireTypes, BorrowExternalVector) {
  std::vector<uint32_t> vec = {1, 2, 3, 4};
  // |vv| is a view that borrows the vector contents of |vec|.
  // Destroying the contents in |vec| will invalidate |vv|.
  fidl::VectorView<uint32_t> vv = fidl::VectorView<uint32_t>::FromExternal(vec);
  ASSERT_EQ(vv.count(), 4UL);
}
// [END wire-external-vector]

// [START wire-external-string]
TEST(WireTypes, BorrowExternalString) {
  std::string string = "hello";
  // |sv| is a view that borrows the string contents of |string|.
  // Destroying the contents in |string| will invalidate |sv|.
  fidl::StringView sv = fidl::StringView::FromExternal(string);
  ASSERT_EQ(sv.size(), 5UL);
}
// [END wire-external-string]

// [START wire-external-string-literal]
TEST(WireTypes, BorrowExternalStringLiteral) {
  fidl::StringView sv1 = "hello world";
  fidl::StringView sv2("Hello");
  ASSERT_EQ(sv1.size(), 11UL);
  ASSERT_EQ(sv2.size(), 5UL);
}
// [END wire-external-string-literal]

// [START wire-union-external-member]
TEST(WireUnion, BorrowExternalMember) {
  fidl::StringView sv = "hello world";
  fuchsia_examples::wire::JsonValue val = fuchsia_examples::wire::JsonValue::WithStringValue(
      fidl::ObjectView<fidl::StringView>::FromExternal(&sv));
  ASSERT_TRUE(val.is_string_value());
}
// [END wire-union-external-member]

// [START wire-table-external-frame-inline]
TEST(WireTables, BorrowExternalFrameInline) {
  fidl::WireTableFrame<fuchsia_examples::wire::User> frame;
  // Construct a table creating a builder borrowing the |frame|.
  auto builder = fuchsia_examples::wire::User::ExternalBuilder(
      fidl::ObjectView<fidl::WireTableFrame<fuchsia_examples::wire::User>>::FromExternal(&frame));
  // Small values <= 4 bytes are inlined inside the frame of the table.
  builder.age(30);
  // The builder is turned into an actual instance by calling |Build|.
  auto user = builder.Build();
  ASSERT_FALSE(user.IsEmpty());
  ASSERT_EQ(user.age(), 30);
}
// [END wire-table-external-frame-inline]

// [START wire-table-external-frame-out-of-line]
TEST(WireTables, BorrowExternalFrameOutOfLine) {
  fidl::WireTableFrame<fuchsia_examples::wire::User> frame;
  // Construct a table creating a builder borrowing the |frame|.
  auto builder = fuchsia_examples::wire::User::ExternalBuilder(
      fidl::ObjectView<fidl::WireTableFrame<fuchsia_examples::wire::User>>::FromExternal(&frame));
  // Larger values > 4 bytes are still stored out of line, i.e. outside the
  // frame of the table. One needs to make an |ObjectView| pointing to larger
  // fields separately, using an arena or with unsafe borrowing here.
  fidl::StringView str("hello");
  fidl::ObjectView object_view = fidl::ObjectView<fidl::StringView>::FromExternal(&str);
  builder.name(object_view);
  // The builder is turned into an actual instance by calling |Build|.
  auto user = builder.Build();
  ASSERT_FALSE(user.IsEmpty());
  ASSERT_TRUE(user.has_name());
  ASSERT_EQ(user.name().get(), "hello");
}
// [END wire-table-external-frame-out-of-line]

}  // namespace
