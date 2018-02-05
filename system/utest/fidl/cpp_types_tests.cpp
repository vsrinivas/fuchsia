// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/cpp/array.h>
#include <fidl/cpp/builder.h>
#include <fidl/cpp/string_view.h>
#include <fidl/cpp/vector_view.h>

#include <unittest/unittest.h>

namespace {

bool string_view_test() {
    BEGIN_TEST;

    char buffer[ZX_CHANNEL_MAX_MSG_BYTES];
    fidl::Builder builder(buffer, ZX_CHANNEL_MAX_MSG_BYTES);

    fidl::StringView* view = builder.New<fidl::StringView>();
    EXPECT_TRUE(view->empty());
    EXPECT_TRUE(view->is_null());

    char* data = builder.NewArray<char>(3);
    view->set_data(data);
    view->set_size(3);

    EXPECT_FALSE(view->empty());
    EXPECT_EQ(view->size(), 3);
    EXPECT_EQ(view->data(), data);

    EXPECT_EQ(view->at(1), 0);

    END_TEST;
}

bool vector_view_test() {
    BEGIN_TEST;

    char buffer[ZX_CHANNEL_MAX_MSG_BYTES];
    fidl::Builder builder(buffer, ZX_CHANNEL_MAX_MSG_BYTES);

    fidl::VectorView<int>* view = builder.New<fidl::VectorView<int>>();
    EXPECT_TRUE(view->empty());
    EXPECT_TRUE(view->is_null());

    int* data = builder.NewArray<int>(3);
    view->set_data(data);
    view->set_count(3);

    EXPECT_EQ(view->count(), 3);
    EXPECT_EQ(view->data(), data);

    EXPECT_EQ(view->at(1), 0);

    END_TEST;
}

bool array_test() {
    BEGIN_TEST;

    char buffer[ZX_CHANNEL_MAX_MSG_BYTES];
    fidl::Builder builder(buffer, ZX_CHANNEL_MAX_MSG_BYTES);

    fidl::Array<int, 3>* view = builder.New<fidl::Array<int, 3>>();
    EXPECT_EQ(3, view->count());

    (*view)[0] = 0;
    (*view)[1] = 1;
    (*view)[2] = 2;

    EXPECT_EQ(view->at(0), 0);
    EXPECT_EQ(view->at(1), 1);
    EXPECT_EQ(view->at(2), 2);

    int i = 0;
    for (auto& value : *view) {
        EXPECT_EQ(i, value);
        ++i;
    }

    END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(cpp_types_tests)
RUN_NAMED_TEST("StringView test", string_view_test)
RUN_NAMED_TEST("VectorView test", vector_view_test)
RUN_NAMED_TEST("Array test", array_test)
END_TEST_CASE(cpp_types_tests);
