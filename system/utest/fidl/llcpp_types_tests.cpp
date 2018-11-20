// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdalign.h>
#include <utility>
#include <memory>

#include <lib/fidl/internal.h>
#include <lib/fidl/llcpp/encoded_message.h>
#include <lib/fidl/llcpp/decoded_message.h>
#include <lib/zx/channel.h>

#include <unittest/unittest.h>

namespace {

// Manually define the encoding table for a simple FIDL message.
// These will match the llcpp codegen output.

extern const fidl_type_t NonnullableChannelMessageType;

struct NonnullableChannelMessage {
    alignas(FIDL_ALIGNMENT)
    fidl_message_header_t header;
    zx::channel channel;

    static constexpr uint32_t MaxNumHandles = 1;

    static constexpr uint32_t MaxSize =
        FIDL_ALIGN(sizeof(fidl_message_header_t)) + FIDL_ALIGN(sizeof(zx::channel));

    static constexpr const fidl_type_t* type = &NonnullableChannelMessageType;
};

const fidl_type_t NonnullableChannelType =
    fidl_type_t(fidl::FidlCodedHandle(ZX_OBJ_TYPE_CHANNEL, fidl::kNonnullable));
const fidl::FidlField NonnullableChannelMessageFields[] = {
    fidl::FidlField(&NonnullableChannelType,
                    offsetof(NonnullableChannelMessage, channel)),
};
const fidl_type_t NonnullableChannelMessageType = fidl_type_t(fidl::FidlCodedStruct(
    NonnullableChannelMessageFields, /* field_count */ 1,
    sizeof(NonnullableChannelMessage),
    "NonnullableChannelMessage"));
}

namespace fidl {

// Manually specialize the templates.
// These will match the llcpp codegen output.

template<>
struct IsFidlType<NonnullableChannelMessage> : public std::true_type {};

template<>
struct IsFidlMessage<NonnullableChannelMessage> : public std::true_type {};

}

namespace {

// Because the EncodedMessage/DecodedMessage classes close handles using the corresponding
// Zircon system call instead of calling a destructor, we indirectly test for handle closure
// via the ZX_ERR_PEER_CLOSED error message.

bool HelperExpectPeerValid(zx::channel &channel) {
    BEGIN_HELPER;

    const char* foo = "A";
    EXPECT_EQ(channel.write(0, foo, 1, nullptr, 0), ZX_OK);

    END_HELPER;
}

bool HelperExpectPeerInvalid(zx::channel &channel) {
    BEGIN_HELPER;

    const char* foo = "A";
    EXPECT_EQ(channel.write(0, foo, 1, nullptr, 0), ZX_ERR_PEER_CLOSED);

    END_HELPER;
}

bool EncodedMessageTest() {
    BEGIN_TEST;

    // Manually construct an encoded message
    alignas(NonnullableChannelMessage) uint8_t buf[sizeof(NonnullableChannelMessage)] = {};
    auto msg = reinterpret_cast<NonnullableChannelMessage*>(&buf[0]);
    msg->channel.reset(FIDL_HANDLE_PRESENT);

    // Capture the extra handle here; it will not be cleaned by encoded_message
    zx::channel channel_1 = {};

    {
        fidl::EncodedMessage<NonnullableChannelMessage> encoded_message;
        encoded_message.Initialize([&buf, &channel_1] (fidl::BytePart& msg_bytes,
                                                       fidl::HandlePart& msg_handles) {
            msg_bytes = fidl::BytePart(buf, sizeof(buf), sizeof(buf));
            zx_handle_t* handle = msg_handles.data();

            // Unsafely open a channel, which should be closed automatically by encoded_message
            {
                zx::channel out0, out1;
                EXPECT_EQ(zx::channel::create(0, &out0, &out1), ZX_OK);
                *handle = out0.release();
                channel_1 = std::move(out1);
            }

            msg_handles.set_actual(1);
        });

        EXPECT_TRUE(HelperExpectPeerValid(channel_1));
    }

    EXPECT_TRUE(HelperExpectPeerInvalid(channel_1));

    END_TEST;
}

bool DecodedMessageTest() {
    BEGIN_TEST;

    // Manually construct a decoded message
    alignas(NonnullableChannelMessage) uint8_t buf[sizeof(NonnullableChannelMessage)] = {};
    auto msg = reinterpret_cast<NonnullableChannelMessage*>(&buf[0]);

    // Capture the extra handle here; it will not be cleaned by decoded_message
    zx::channel channel_1 = {};

    {
        // Unsafely open a channel, which should be closed automatically by decoded_message
        {
            zx::channel out0, out1;
            EXPECT_EQ(zx::channel::create(0, &out0, &out1), ZX_OK);
            msg->channel = std::move(out0);
            channel_1 = std::move(out1);
        }

        fidl::DecodedMessage<NonnullableChannelMessage> decoded_message(
            fidl::BytePart(buf, sizeof(buf), sizeof(buf)));

        EXPECT_TRUE(HelperExpectPeerValid(channel_1));
    }

    EXPECT_TRUE(HelperExpectPeerInvalid(channel_1));

    END_TEST;
}

// Start with an encoded message, then decode and back.
bool RoundTripTest() {
    BEGIN_TEST;

    alignas(NonnullableChannelMessage) uint8_t buf[sizeof(NonnullableChannelMessage)] = {};
    auto msg = reinterpret_cast<NonnullableChannelMessage*>(&buf[0]);
    msg->header.txid = 10;
    msg->header.ordinal = 42;
    msg->channel.reset(FIDL_HANDLE_PRESENT);

    // Capture the extra handle here; it will not be cleaned by encoded_message
    zx::channel channel_1 = {};

    fidl::EncodedMessage<NonnullableChannelMessage>* encoded_message =
        new fidl::EncodedMessage<NonnullableChannelMessage>();
    zx_handle_t unsafe_handle_backup;

    encoded_message->Initialize([&buf, &channel_1, &unsafe_handle_backup] (
        fidl::BytePart& msg_bytes,
        fidl::HandlePart& msg_handles
    ) {
        msg_bytes = fidl::BytePart(buf, sizeof(buf), sizeof(buf));
        zx_handle_t* handle = msg_handles.data();

        // Unsafely open a channel, which should be closed automatically by encoded_message
        {
            zx::channel out0, out1;
            EXPECT_EQ(zx::channel::create(0, &out0, &out1), ZX_OK);
            *handle = out0.release();
            unsafe_handle_backup = *handle;
            channel_1 = std::move(out1);
        }

        msg_handles.set_actual(1);
    });

    uint8_t golden_encoded[] = {
        10, 0, 0, 0, // txid
        0, 0, 0, 0, // reserved
        0, 0, 0, 0, // flags
        42, 0, 0, 0, // ordinal
        255, 255, 255, 255, // handle present
        0, 0, 0, 0
    };

    // Byte-accurate comparison
    EXPECT_EQ(memcmp(golden_encoded, buf, sizeof(buf)), 0);

    EXPECT_TRUE(HelperExpectPeerValid(channel_1));

    // Decode
    fidl::DecodedMessage<NonnullableChannelMessage> decoded_message;
    const char* decode_error = nullptr;
    zx_status_t status = decoded_message.DecodeFrom(encoded_message, &decode_error);
    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(decode_error, decode_error);
    EXPECT_EQ(decoded_message.message()->header.txid, 10);
    EXPECT_EQ(decoded_message.message()->header.ordinal, 42);
    EXPECT_EQ(decoded_message.message()->channel.get(), unsafe_handle_backup);
    // encoded_message should be consumed
    EXPECT_EQ(encoded_message->handles().actual(), 0);
    EXPECT_EQ(encoded_message->bytes().actual(), 0);
    // If we destroy encoded_message, it should not accidentally close the channel
    delete encoded_message;
    EXPECT_TRUE(HelperExpectPeerValid(channel_1));

    // Encode
    encoded_message = new fidl::EncodedMessage<NonnullableChannelMessage>();
    const char* encode_error = nullptr;
    status = decoded_message.EncodeTo(encoded_message, &encode_error);
    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(encode_error, encode_error);
    // decoded_message should be consumed
    EXPECT_EQ(decoded_message.message(), nullptr);

    // Byte-level comparison
    EXPECT_EQ(encoded_message->bytes().actual(), sizeof(buf));
    EXPECT_EQ(encoded_message->handles().actual(), 1);
    EXPECT_EQ(encoded_message->handles().data()[0], unsafe_handle_backup);
    EXPECT_EQ(memcmp(golden_encoded, encoded_message->bytes().data(), sizeof(buf)), 0);

    EXPECT_TRUE(HelperExpectPeerValid(channel_1));
    delete encoded_message;
    EXPECT_TRUE(HelperExpectPeerInvalid(channel_1));

    END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(llcpp_types_tests)
    RUN_NAMED_TEST("EncodedMessage test", EncodedMessageTest)
    RUN_NAMED_TEST("DecodedMessage test", DecodedMessageTest)
    RUN_NAMED_TEST("Round trip test", RoundTripTest)
END_TEST_CASE(llcpp_types_tests);
