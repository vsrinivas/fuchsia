// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "echo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <magenta/syscalls.h>

#include "message.h"
#include "struct.h"

bool wait_for_readable(mx_handle_t handle) {
    printf("waiting for handle %u to be readable (or closed)\n", handle);
    // Wait for |handle| to become readable or closed.
    mx_signals_t signals = MX_SIGNAL_READABLE | MX_SIGNAL_PEER_CLOSED;
    mx_signals_t satisfied_signals;
    mx_status_t wait_status = _magenta_handle_wait_one(handle, signals, MX_TIME_INFINITE,
                                                       &satisfied_signals, NULL);
    if (wait_status != NO_ERROR) {
        return false;
    }
    if (!(satisfied_signals & MX_SIGNAL_READABLE)) {
        return false;
    }
    return true;
}

bool serve_echo_request(mx_handle_t handle) {
    if (!wait_for_readable(handle)) {
        printf("handle not readable\n");
        return false;
    }

    // Try to read a message from |in_handle|.
    // First, figure out size.
    uint32_t in_msg_size = 0u;
    mx_status_t read_status = _magenta_message_read(handle, NULL, &in_msg_size, NULL, NULL, 0u);
    if (read_status != ERR_NO_MEMORY) {
        printf("unexpected sizing read status: %u\n", read_status);
        return false;
    }
    printf("reading message of size %u\n", in_msg_size);
    void* in_msg_buf = calloc(in_msg_size, 1u);
    read_status = _magenta_message_read(handle, in_msg_buf, &in_msg_size, NULL, NULL, 0u);
    if (read_status != NO_ERROR) {
        printf("read failed with status %u\n", read_status);
        return false;
    }
    // Try to parse message data.
    if (!mojo_validate_struct_header(in_msg_buf, in_msg_size)) {
        printf("validation failed on read message\n");
        return false;
    }

    mojo_struct_header_t* in_struct_header = (mojo_struct_header_t*)in_msg_buf;
    if (in_struct_header->version != 1u) {
        return false;
    }

    mojo_message_header_with_request_id_t* in_msg_header =
        (mojo_message_header_with_request_id_t*)in_struct_header;

    if (in_msg_header->message_header.name != 0u) {
        return false;
    }

    if (in_msg_header->message_header.flags != MOJO_MESSAGE_HEADER_FLAGS_EXPECTS_RESPONSE) {
        return false;
    }

    uint64_t request_id = in_msg_header->request_id;

    void* in_payload = in_msg_header + 1u;

    uint32_t in_string_header_num_bytes = *(uint32_t*)in_payload;
    uint32_t in_string_header_num_elems = *((uint32_t*)in_payload + 1u);
    void* in_string_data = ((uint32_t*)in_payload) + 2u;
    printf("got string: ");
    for (uint32_t i = 0u; i < in_string_header_num_elems; ++i) {
        printf("%c", ((char*)in_string_data)[i]);
    }
    printf("\n");

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
        _magenta_message_write(handle, out_msg_buf, out_msg_size, NULL, 0u, 0u);
    free(out_msg_buf);

    if (write_status != NO_ERROR) {
        return false;
    }

    printf("served request!\n\n\n");
    return true;
}
