// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "echo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <magenta/syscalls.h>
#include <unittest/unittest.h>

#include "message.h"
#include "struct.h"

bool wait_for_readable(mx_handle_t handle) {
    unittest_printf("waiting for handle %u to be readable (or closed)\n", handle);
    // Wait for |handle| to become readable or closed.
    mx_signals_t signals = MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED;
    mx_signals_t pending;
    mx_status_t wait_status = mx_object_wait_one(handle, signals, MX_TIME_INFINITE,
                                                       &pending);
    if (wait_status != MX_OK) {
        return false;
    }
    if (!(pending & MX_CHANNEL_READABLE)) {
        return false;
    }
    return true;
}

bool serve_echo_request(mx_handle_t handle) {
    ASSERT_TRUE(wait_for_readable(handle), "handle not readable");

    // Try to read a message from |in_handle|.
    // First, figure out size.
    uint32_t in_msg_size = 0u;
    mx_status_t read_status = mx_channel_read(handle, 0u, NULL, NULL, 0, 0, &in_msg_size, NULL);
    ASSERT_NE(read_status, MX_ERR_NO_MEMORY, "unexpected sizing read status");

    unittest_printf("reading message of size %u\n", in_msg_size);
    void* in_msg_buf = calloc(in_msg_size, 1u);
    read_status = mx_channel_read(handle, 0u, in_msg_buf, NULL, in_msg_size, 0, &in_msg_size, NULL);
    ASSERT_EQ(read_status, MX_OK, "read failed with status");

    // Try to parse message data.
    ASSERT_TRUE(mojo_validate_struct_header(in_msg_buf, in_msg_size),
                "validation failed on read message");

    mojo_struct_header_t* in_struct_header = (mojo_struct_header_t*)in_msg_buf;
    ASSERT_EQ(in_struct_header->version, 1u, "Header verison incorrect");

    mojo_message_header_with_request_id_t* in_msg_header =
        (mojo_message_header_with_request_id_t*)in_struct_header;

    ASSERT_EQ(in_msg_header->message_header.name, 0u, "Name should be null");

    ASSERT_EQ(in_msg_header->message_header.flags,
              (uint32_t)MOJO_MESSAGE_HEADER_FLAGS_EXPECTS_RESPONSE,
              "Invalid header flag");

    uint64_t request_id = in_msg_header->request_id;

    void* in_payload = in_msg_header + 1u;

    uint32_t in_string_header_num_bytes = *(uint32_t*)in_payload;
    uint32_t in_string_header_num_elems = *((uint32_t*)in_payload + 1u);
    void* in_string_data = ((uint32_t*)in_payload) + 2u;
    unittest_printf("got string: ");
    for (uint32_t i = 0u; i < in_string_header_num_elems; ++i) {
        unittest_printf("%c", ((char*)in_string_data)[i]);
    }
    unittest_printf("\n");

    // TODO: Validate array header

    // Incoming message seems fine, form an outgoing message and send it.

    void* out_msg_buf = malloc(in_msg_size);
    uint32_t out_msg_size = in_msg_size;

    // Write header
    mojo_message_header_with_request_id_t* out_msg_header =
        (mojo_message_header_with_request_id_t*)out_msg_buf;

    // Struct header
    out_msg_header->message_header.struct_header.num_bytes =
        sizeof(mojo_message_header_with_request_id_t);
    out_msg_header->message_header.struct_header.version = 1u;

    // Message header
    out_msg_header->message_header.name = 0u;
    out_msg_header->message_header.flags = MOJO_MESSAGE_HEADER_FLAGS_IS_RESPONSE;
    out_msg_header->request_id = request_id;

    uint32_t* out_string_header = (uint32_t*)out_msg_header + 1u;
    *out_string_header = in_string_header_num_bytes;
    *(out_string_header + 1u) = in_string_header_num_elems;

    if (in_string_header_num_bytes != 0u) {
        char* out_string_dest = (char*)(out_string_header + 2u);
        memcpy(out_string_dest, (char*)(in_string_data), in_string_header_num_bytes);
    }
    free(in_msg_buf);

    mx_status_t write_status =
        mx_channel_write(handle, 0u, out_msg_buf, out_msg_size, NULL, 0u);
    free(out_msg_buf);

    ASSERT_EQ(write_status, MX_OK, "Error while message writing");

    unittest_printf("served request!\n\n");
    return true;
}

bool echo_test(void) {
    BEGIN_TEST;
    mx_handle_t handles[2] = {0};
    mx_status_t status = mx_channel_create(0, handles, handles + 1);
    ASSERT_EQ(status, 0, "could not create channel");
    unittest_printf("created channel with handle values %u and %u\n", handles[0], handles[1]);
    for (int i = 0; i < 3; i++) {
        unittest_printf("loop %d\n", i);
        static const uint32_t buf[9] = {
            24,         // struct header, num_bytes
            1,          // struct header: version
            0,          // struct header: flags
            1,          // message header: name
            0, 0,       // message header: request id (8 bytes)
            4,          // array header: num bytes
            4,          // array header: num elems
            0x42424143, // array contents: 'CABB'
        };
        mx_status_t status = mx_channel_write(handles[1], 0u, (void*)buf, sizeof(buf), NULL, 0u);
        ASSERT_EQ(status, MX_OK, "could not write echo request");

        ASSERT_TRUE(serve_echo_request(handles[0]), "serve_echo_request failed");
    }
    mx_handle_close(handles[1]);
    EXPECT_FALSE(wait_for_readable(handles[0]), "handle should not readable");
    mx_handle_close(handles[0]);
    END_TEST;
}

BEGIN_TEST_CASE(echo_tests)
RUN_TEST(echo_test)
END_TEST_CASE(echo_tests)


#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
