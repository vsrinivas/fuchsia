// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/cpp/builder.h>
#include <fidl/cpp/message.h>
#include <fidl/cpp/string_view.h>
#include <zx/channel.h>

#include <unittest/unittest.h>

namespace {

bool message_test() {
    BEGIN_TEST;

    uint8_t byte_buffer[ZX_CHANNEL_MAX_MSG_BYTES];
    zx_handle_t handle_buffer[ZX_CHANNEL_MAX_MSG_HANDLES];

    fidl::Builder builder(byte_buffer, ZX_CHANNEL_MAX_MSG_BYTES);

    fidl_message_header_t* header = builder.New<fidl_message_header_t>();
    header->txid = 5u;
    header->ordinal = 42u;

    fidl::StringView* view = builder.New<fidl::StringView>();

    char* data = builder.NewArray<char>(4);
    view->set_data(data);
    view->set_size(4);

    data[0] = 'a';
    data[1] = 'b';
    data[2] = 'c';

    fidl::Message message(builder.Finalize(),
        fidl::HandlePart(handle_buffer, ZX_CHANNEL_MAX_MSG_HANDLES));

    EXPECT_EQ(message.txid(), 5u);
    EXPECT_EQ(message.ordinal(), 42u);

    fidl::BytePart payload = message.payload();
    EXPECT_EQ(reinterpret_cast<fidl::StringView*>(payload.data()), view);

    zx::channel h1, h2;
    EXPECT_EQ(zx::channel::create(0, &h1, &h2), ZX_OK);

    EXPECT_EQ(ZX_OK, message.Write(h1.get(), 0u));

    memset(byte_buffer, 0, ZX_CHANNEL_MAX_MSG_BYTES);

    EXPECT_EQ(message.txid(), 0u);
    EXPECT_EQ(message.ordinal(), 0u);

    EXPECT_EQ(ZX_OK, message.Read(h2.get(), 0u));

    EXPECT_EQ(message.txid(), 5u);
    EXPECT_EQ(message.ordinal(), 42u);

    END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(message_tests)
RUN_NAMED_TEST("Message test", message_test)
END_TEST_CASE(message_tests);
