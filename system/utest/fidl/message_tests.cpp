// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/builder.h>
#include <lib/fidl/cpp/message_builder.h>
#include <lib/fidl/cpp/message.h>
#include <lib/fidl/cpp/string_view.h>
#include <lib/zx/channel.h>
#include <lib/zx/event.h>

#include <unittest/unittest.h>

#include "fidl_coded_types.h"

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

bool message_builder_test() {
    BEGIN_TEST;

    zx::event e;
    EXPECT_EQ(zx::event::create(0, &e), ZX_OK);
    EXPECT_NE(e.get(), ZX_HANDLE_INVALID);

    fidl::MessageBuilder builder(&nonnullable_handle_message_type);
    builder.header()->txid = 5u;
    builder.header()->ordinal = 42u;

    zx_handle_t* handle = builder.New<zx_handle_t>();
    *handle = e.get();

    fidl::Message message;
    const char* error_msg;
    EXPECT_EQ(builder.Encode(&message, &error_msg), ZX_OK);

    EXPECT_EQ(message.txid(), 5u);
    EXPECT_EQ(message.ordinal(), 42u);
    EXPECT_EQ(message.handles().actual(), 1u);
    EXPECT_EQ(message.handles().data()[0], e.get());

    END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(message_tests)
RUN_NAMED_TEST("Message test", message_test)
RUN_NAMED_TEST("MessageBuilder test", message_builder_test)
END_TEST_CASE(message_tests);
