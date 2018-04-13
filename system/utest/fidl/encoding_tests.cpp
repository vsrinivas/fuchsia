// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>
#include <stddef.h>

#include <fbl/type_support.h>

#include <lib/fidl/coding.h>

#include <unittest/unittest.h>
#include <zircon/syscalls.h>

#include "fidl_coded_types.h"
#include "fidl_structs.h"

namespace fidl {
namespace {

// Some notes:
//
// - All tests of out-of-line bounded allocation overruns need to have
//   another big out-of-line allocation following it. This
//   distinguishes "the buffer is too small" from "the bits on the
//   wire asked for more than the type allowed".

// TODO(kulakowski) Change the tests to check for more specific error
// values, once those are settled.

constexpr zx_handle_t dummy_handle_0 = static_cast<zx_handle_t>(23);
constexpr zx_handle_t dummy_handle_1 = static_cast<zx_handle_t>(24);
constexpr zx_handle_t dummy_handle_2 = static_cast<zx_handle_t>(25);
constexpr zx_handle_t dummy_handle_3 = static_cast<zx_handle_t>(26);
constexpr zx_handle_t dummy_handle_4 = static_cast<zx_handle_t>(27);
constexpr zx_handle_t dummy_handle_5 = static_cast<zx_handle_t>(28);
constexpr zx_handle_t dummy_handle_6 = static_cast<zx_handle_t>(29);
constexpr zx_handle_t dummy_handle_7 = static_cast<zx_handle_t>(30);
constexpr zx_handle_t dummy_handle_8 = static_cast<zx_handle_t>(31);
constexpr zx_handle_t dummy_handle_9 = static_cast<zx_handle_t>(32);
constexpr zx_handle_t dummy_handle_10 = static_cast<zx_handle_t>(33);
constexpr zx_handle_t dummy_handle_11 = static_cast<zx_handle_t>(34);
constexpr zx_handle_t dummy_handle_12 = static_cast<zx_handle_t>(35);
constexpr zx_handle_t dummy_handle_13 = static_cast<zx_handle_t>(36);
constexpr zx_handle_t dummy_handle_14 = static_cast<zx_handle_t>(37);
constexpr zx_handle_t dummy_handle_15 = static_cast<zx_handle_t>(38);
constexpr zx_handle_t dummy_handle_16 = static_cast<zx_handle_t>(39);
constexpr zx_handle_t dummy_handle_17 = static_cast<zx_handle_t>(40);
constexpr zx_handle_t dummy_handle_18 = static_cast<zx_handle_t>(41);
constexpr zx_handle_t dummy_handle_19 = static_cast<zx_handle_t>(42);
constexpr zx_handle_t dummy_handle_20 = static_cast<zx_handle_t>(43);
constexpr zx_handle_t dummy_handle_21 = static_cast<zx_handle_t>(44);
constexpr zx_handle_t dummy_handle_22 = static_cast<zx_handle_t>(45);
constexpr zx_handle_t dummy_handle_23 = static_cast<zx_handle_t>(46);
constexpr zx_handle_t dummy_handle_24 = static_cast<zx_handle_t>(47);
constexpr zx_handle_t dummy_handle_25 = static_cast<zx_handle_t>(48);
constexpr zx_handle_t dummy_handle_26 = static_cast<zx_handle_t>(49);
constexpr zx_handle_t dummy_handle_27 = static_cast<zx_handle_t>(50);
constexpr zx_handle_t dummy_handle_28 = static_cast<zx_handle_t>(51);
constexpr zx_handle_t dummy_handle_29 = static_cast<zx_handle_t>(52);

// All sizes in fidl encoding tables are 32 bits. The fidl compiler
// normally enforces this. Check manually in manual tests.
template <typename T, size_t N> uint32_t ArrayCount(T const (&array)[N]) {
    static_assert(N < UINT32_MAX, "Array is too large!");
    return N;
}

template <typename T, size_t N> uint32_t ArraySize(T const (&array)[N]) {
    static_assert(sizeof(array) < UINT32_MAX, "Array is too large!");
    return sizeof(array);
}

bool encode_null_encode_parameters() {
    BEGIN_TEST;

    zx_handle_t handles[1] = {};

    // Null message type.
    {
        nonnullable_handle_message_layout message;
        const char* error = nullptr;
        uint32_t actual_handles = 0u;
        auto status = fidl_encode(nullptr, &message, sizeof(nonnullable_handle_message_layout),
                                  handles, ArrayCount(handles), &actual_handles, &error);
        EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
        EXPECT_NONNULL(error);
    }

    // Null message.
    {
        const char* error = nullptr;
        uint32_t actual_handles = 0u;
        auto status = fidl_encode(&nonnullable_handle_message_type, nullptr,
                                  sizeof(nonnullable_handle_message_layout), handles,
                                  ArrayCount(handles), &actual_handles, &error);
        EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
        EXPECT_NONNULL(error);
    }

    // Null handles, for a message that has a handle.
    {
        nonnullable_handle_message_layout message;
        const char* error = nullptr;
        uint32_t actual_handles = 0u;
        auto status = fidl_encode(&nonnullable_handle_message_type, &message,
                                  sizeof(nonnullable_handle_message_layout), nullptr, 0,
                                  &actual_handles, &error);
        EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
        EXPECT_NONNULL(error);
    }

    // Null handles but positive handle count.
    {
        nonnullable_handle_message_layout message;
        const char* error = nullptr;
        uint32_t actual_handles = 0u;
        auto status = fidl_encode(&nonnullable_handle_message_type, &message,
                                  sizeof(nonnullable_handle_message_layout), nullptr, 1,
                                  &actual_handles, &error);
        EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
        EXPECT_NONNULL(error);
    }

    // A null actual handle count pointer.
    {
        nonnullable_handle_message_layout message;
        const char* error = nullptr;
        auto status = fidl_encode(&nonnullable_handle_message_type, &message,
                                  sizeof(nonnullable_handle_message_layout), handles,
                                  ArrayCount(handles), nullptr, &error);
        EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
        EXPECT_NONNULL(error);
    }

    // A null error string pointer is ok, though.
    {
        uint32_t actual_handles = 0u;
        auto status = fidl_encode(nullptr, nullptr, 0u, nullptr, 0u, &actual_handles, nullptr);
        EXPECT_NE(status, ZX_OK);
    }

    // A null error is also ok in success cases.
    {
        nonnullable_handle_message_layout message = {};
        message.inline_struct.handle = dummy_handle_0;

        uint32_t actual_handles = 0u;
        auto status = fidl_encode(&nonnullable_handle_message_type, &message,
                                  sizeof(nonnullable_handle_message_layout), handles,
                                  ArrayCount(handles), &actual_handles, nullptr);
        EXPECT_EQ(status, ZX_OK);
        EXPECT_EQ(actual_handles, 1u);
        EXPECT_EQ(handles[0], dummy_handle_0);
        EXPECT_EQ(message.inline_struct.handle, FIDL_HANDLE_PRESENT);
    }

    END_TEST;
}

bool encode_single_present_handle() {
    BEGIN_TEST;

    nonnullable_handle_message_layout message = {};
    message.inline_struct.handle = dummy_handle_0;

    zx_handle_t handles[1] = {};

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&nonnullable_handle_message_type, &message, sizeof(message), handles,
                              ArrayCount(handles), &actual_handles, &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);
    EXPECT_EQ(actual_handles, 1u);
    EXPECT_EQ(handles[0], dummy_handle_0);
    EXPECT_EQ(message.inline_struct.handle, FIDL_HANDLE_PRESENT);

    END_TEST;
}

bool encode_single_present_handle_unaligned_error() {
    BEGIN_TEST;

    // Test a short, unaligned version of nonnullable message
    // handle. All fidl message objects should be 8 byte aligned.
    struct unaligned_nonnullable_handle_inline_data {
        fidl_message_header_t header;
        zx_handle_t handle;
    };
    struct unaligned_nonnullable_handle_message_layout {
        unaligned_nonnullable_handle_inline_data inline_struct;
    };

    unaligned_nonnullable_handle_message_layout message = {};
    message.inline_struct.handle = dummy_handle_0;

    zx_handle_t handles[1] = {};

    // Encoding the unaligned version of the struct should fail.
    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&nonnullable_handle_message_type, &message, sizeof(message), handles,
                              ArrayCount(handles), &actual_handles, &error);

    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_NONNULL(error);

    END_TEST;
}

bool encode_multiple_present_handles() {
    BEGIN_TEST;

    multiple_nonnullable_handles_message_layout message = {};
    message.inline_struct.handle_0 = dummy_handle_0;
    message.inline_struct.handle_1 = dummy_handle_1;
    message.inline_struct.handle_2 = dummy_handle_2;

    zx_handle_t handles[3] = {};

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&multiple_nonnullable_handles_message_type, &message, sizeof(message),
                              handles, ArrayCount(handles), &actual_handles, &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);
    EXPECT_EQ(actual_handles, 3u);
    EXPECT_EQ(message.inline_struct.data_0, 0u);
    EXPECT_EQ(message.inline_struct.handle_0, FIDL_HANDLE_PRESENT);
    EXPECT_EQ(message.inline_struct.data_1, 0u);
    EXPECT_EQ(message.inline_struct.handle_1, FIDL_HANDLE_PRESENT);
    EXPECT_EQ(message.inline_struct.handle_2, FIDL_HANDLE_PRESENT);
    EXPECT_EQ(message.inline_struct.data_2, 0u);
    EXPECT_EQ(handles[0], dummy_handle_0);
    EXPECT_EQ(handles[1], dummy_handle_1);
    EXPECT_EQ(handles[2], dummy_handle_2);

    END_TEST;
}

bool encode_single_absent_handle() {
    BEGIN_TEST;

    nullable_handle_message_layout message = {};
    message.inline_struct.handle = ZX_HANDLE_INVALID;

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&nullable_handle_message_type, &message, sizeof(message), nullptr, 0,
                              &actual_handles, &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);
    EXPECT_EQ(actual_handles, 0u);
    EXPECT_EQ(message.inline_struct.handle, FIDL_HANDLE_ABSENT);

    END_TEST;
}

bool encode_multiple_absent_handles() {
    BEGIN_TEST;

    multiple_nullable_handles_message_layout message = {};
    message.inline_struct.handle_0 = ZX_HANDLE_INVALID;
    message.inline_struct.handle_1 = ZX_HANDLE_INVALID;
    message.inline_struct.handle_2 = ZX_HANDLE_INVALID;

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&multiple_nullable_handles_message_type, &message, sizeof(message),
                              nullptr, 0, &actual_handles, &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);
    EXPECT_EQ(actual_handles, 0u);
    EXPECT_EQ(message.inline_struct.data_0, 0u);
    EXPECT_EQ(message.inline_struct.handle_0, FIDL_HANDLE_ABSENT);
    EXPECT_EQ(message.inline_struct.data_1, 0u);
    EXPECT_EQ(message.inline_struct.handle_1, FIDL_HANDLE_ABSENT);
    EXPECT_EQ(message.inline_struct.handle_2, FIDL_HANDLE_ABSENT);
    EXPECT_EQ(message.inline_struct.data_2, 0u);

    END_TEST;
}

bool encode_array_of_present_handles() {
    BEGIN_TEST;

    array_of_nonnullable_handles_message_layout message = {};
    message.inline_struct.handles[0] = dummy_handle_0;
    message.inline_struct.handles[1] = dummy_handle_1;
    message.inline_struct.handles[2] = dummy_handle_2;
    message.inline_struct.handles[3] = dummy_handle_3;

    zx_handle_t handles[4] = {};

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&array_of_nonnullable_handles_message_type, &message, sizeof(message),
                              handles, ArrayCount(handles), &actual_handles, &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);
    EXPECT_EQ(actual_handles, 4u);
    EXPECT_EQ(message.inline_struct.handles[0], FIDL_HANDLE_PRESENT);
    EXPECT_EQ(message.inline_struct.handles[1], FIDL_HANDLE_PRESENT);
    EXPECT_EQ(message.inline_struct.handles[2], FIDL_HANDLE_PRESENT);
    EXPECT_EQ(message.inline_struct.handles[3], FIDL_HANDLE_PRESENT);
    EXPECT_EQ(handles[0], dummy_handle_0);
    EXPECT_EQ(handles[1], dummy_handle_1);
    EXPECT_EQ(handles[2], dummy_handle_2);
    EXPECT_EQ(handles[3], dummy_handle_3);

    END_TEST;
}

bool encode_array_of_present_handles_error_closes_handles() {
    BEGIN_TEST;

    array_of_nonnullable_handles_message_layout message = {};
    zx_handle_t handle_pairs[4][2];
    // Use eventpairs so that we can know for sure that handles were closed by fidl_encode.
    for (uint32_t i = 0; i < ArrayCount(handle_pairs); ++i) {
        ASSERT_EQ(zx_eventpair_create(0u, &handle_pairs[i][0], &handle_pairs[i][1]), ZX_OK);
    }
    message.inline_struct.handles[0] = handle_pairs[0][0];
    message.inline_struct.handles[1] = handle_pairs[1][0];
    message.inline_struct.handles[2] = handle_pairs[2][0];
    message.inline_struct.handles[3] = handle_pairs[3][0];

    zx_handle_t output_handles[4] = {};

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&array_of_nonnullable_handles_message_type, &message, sizeof(message),
                              output_handles,
                              // -2 makes this invalid.
                              ArrayCount(message.inline_struct.handles) - 2,
                              &actual_handles, &error);
    // Should fail because we we pass in a max_handles < the actual number of handles.
    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_EQ(actual_handles, 0);
    // All handles should be closed, those before the error was encountered and those after.
    for (uint32_t i = 0; i < ArrayCount(handle_pairs); ++i) {
        zx_signals_t observed_signals;
        EXPECT_EQ(zx_object_wait_one(handle_pairs[i][1],
                                     ZX_EPAIR_PEER_CLOSED,
                                     1, // deadline shouldn't matter, should return immediately.
                                     &observed_signals),
                   ZX_OK);
        EXPECT_EQ(observed_signals & ZX_EPAIR_PEER_CLOSED, ZX_EPAIR_PEER_CLOSED);
        EXPECT_EQ(zx_handle_close(handle_pairs[i][1]), ZX_OK); // [i][0] was closed by fidl_encode.
    }

    END_TEST;
}

bool encode_array_of_nullable_handles() {
    BEGIN_TEST;

    array_of_nullable_handles_message_layout message = {};
    message.inline_struct.handles[0] = dummy_handle_0;
    message.inline_struct.handles[1] = ZX_HANDLE_INVALID;
    message.inline_struct.handles[2] = dummy_handle_1;
    message.inline_struct.handles[3] = ZX_HANDLE_INVALID;
    message.inline_struct.handles[4] = dummy_handle_2;

    zx_handle_t handles[3] = {};

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&array_of_nullable_handles_message_type, &message, sizeof(message),
                              handles, ArrayCount(handles), &actual_handles, &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);
    EXPECT_EQ(actual_handles, 3u);
    EXPECT_EQ(message.inline_struct.handles[0], FIDL_HANDLE_PRESENT);
    EXPECT_EQ(message.inline_struct.handles[1], FIDL_HANDLE_ABSENT);
    EXPECT_EQ(message.inline_struct.handles[2], FIDL_HANDLE_PRESENT);
    EXPECT_EQ(message.inline_struct.handles[3], FIDL_HANDLE_ABSENT);
    EXPECT_EQ(message.inline_struct.handles[4], FIDL_HANDLE_PRESENT);
    EXPECT_EQ(handles[0], dummy_handle_0);
    EXPECT_EQ(handles[1], dummy_handle_1);
    EXPECT_EQ(handles[2], dummy_handle_2);

    END_TEST;
}

bool encode_array_of_nullable_handles_with_insufficient_handles_error() {
    BEGIN_TEST;

    array_of_nullable_handles_message_layout message = {};
    message.inline_struct.handles[0] = dummy_handle_0;
    message.inline_struct.handles[1] = ZX_HANDLE_INVALID;
    message.inline_struct.handles[2] = dummy_handle_1;
    message.inline_struct.handles[3] = ZX_HANDLE_INVALID;
    message.inline_struct.handles[4] = dummy_handle_2;

    zx_handle_t handles[2] = {};

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&array_of_nullable_handles_message_type, &message, sizeof(message),
                              handles, ArrayCount(handles), &actual_handles, &error);

    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_NONNULL(error);

    END_TEST;
}

bool encode_array_of_array_of_present_handles() {
    BEGIN_TEST;

    array_of_array_of_nonnullable_handles_message_layout message = {};
    message.inline_struct.handles[0][0] = dummy_handle_0;
    message.inline_struct.handles[0][1] = dummy_handle_1;
    message.inline_struct.handles[0][2] = dummy_handle_2;
    message.inline_struct.handles[0][3] = dummy_handle_3;
    message.inline_struct.handles[1][0] = dummy_handle_4;
    message.inline_struct.handles[1][1] = dummy_handle_5;
    message.inline_struct.handles[1][2] = dummy_handle_6;
    message.inline_struct.handles[1][3] = dummy_handle_7;
    message.inline_struct.handles[2][0] = dummy_handle_8;
    message.inline_struct.handles[2][1] = dummy_handle_9;
    message.inline_struct.handles[2][2] = dummy_handle_10;
    message.inline_struct.handles[2][3] = dummy_handle_11;

    zx_handle_t handles[12] = {};

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status =
        fidl_encode(&array_of_array_of_nonnullable_handles_message_type, &message, sizeof(message),
                    handles, ArrayCount(handles), &actual_handles, &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);
    EXPECT_EQ(actual_handles, 12u);
    EXPECT_EQ(message.inline_struct.handles[0][0], FIDL_HANDLE_PRESENT);
    EXPECT_EQ(message.inline_struct.handles[0][1], FIDL_HANDLE_PRESENT);
    EXPECT_EQ(message.inline_struct.handles[0][2], FIDL_HANDLE_PRESENT);
    EXPECT_EQ(message.inline_struct.handles[0][3], FIDL_HANDLE_PRESENT);
    EXPECT_EQ(message.inline_struct.handles[1][0], FIDL_HANDLE_PRESENT);
    EXPECT_EQ(message.inline_struct.handles[1][1], FIDL_HANDLE_PRESENT);
    EXPECT_EQ(message.inline_struct.handles[1][2], FIDL_HANDLE_PRESENT);
    EXPECT_EQ(message.inline_struct.handles[1][3], FIDL_HANDLE_PRESENT);
    EXPECT_EQ(message.inline_struct.handles[2][0], FIDL_HANDLE_PRESENT);
    EXPECT_EQ(message.inline_struct.handles[2][1], FIDL_HANDLE_PRESENT);
    EXPECT_EQ(message.inline_struct.handles[2][2], FIDL_HANDLE_PRESENT);
    EXPECT_EQ(message.inline_struct.handles[2][3], FIDL_HANDLE_PRESENT);
    EXPECT_EQ(handles[0], dummy_handle_0);
    EXPECT_EQ(handles[1], dummy_handle_1);
    EXPECT_EQ(handles[2], dummy_handle_2);
    EXPECT_EQ(handles[3], dummy_handle_3);
    EXPECT_EQ(handles[4], dummy_handle_4);
    EXPECT_EQ(handles[5], dummy_handle_5);
    EXPECT_EQ(handles[6], dummy_handle_6);
    EXPECT_EQ(handles[7], dummy_handle_7);
    EXPECT_EQ(handles[8], dummy_handle_8);
    EXPECT_EQ(handles[9], dummy_handle_9);
    EXPECT_EQ(handles[10], dummy_handle_10);
    EXPECT_EQ(handles[11], dummy_handle_11);

    END_TEST;
}

bool encode_out_of_line_array_of_nonnullable_handles() {
    BEGIN_TEST;

    out_of_line_array_of_nonnullable_handles_message_layout message = {};
    message.inline_struct.maybe_array = &message.data;
    message.data.handles[0] = dummy_handle_0;
    message.data.handles[1] = dummy_handle_1;
    message.data.handles[2] = dummy_handle_2;
    message.data.handles[3] = dummy_handle_3;

    zx_handle_t handles[4] = {};

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status =
        fidl_encode(&out_of_line_array_of_nonnullable_handles_message_type, &message,
                    sizeof(message), handles, ArrayCount(handles), &actual_handles, &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);
    EXPECT_EQ(actual_handles, 4u);

    auto array_ptr = reinterpret_cast<uint64_t>(message.inline_struct.maybe_array);
    EXPECT_EQ(array_ptr, FIDL_ALLOC_PRESENT);
    EXPECT_EQ(message.data.handles[0], FIDL_HANDLE_PRESENT);
    EXPECT_EQ(message.data.handles[1], FIDL_HANDLE_PRESENT);
    EXPECT_EQ(message.data.handles[2], FIDL_HANDLE_PRESENT);
    EXPECT_EQ(message.data.handles[3], FIDL_HANDLE_PRESENT);
    EXPECT_EQ(handles[0], dummy_handle_0);
    EXPECT_EQ(handles[1], dummy_handle_1);
    EXPECT_EQ(handles[2], dummy_handle_2);
    EXPECT_EQ(handles[3], dummy_handle_3);

    END_TEST;
}

bool encode_present_nonnullable_string() {
    BEGIN_TEST;

    unbounded_nonnullable_string_message_layout message = {};
    message.inline_struct.string = fidl_string_t{6, &message.data[0]};
    memcpy(message.data, "hello!", 6);

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&unbounded_nonnullable_string_message_type, &message, sizeof(message),
                              nullptr, 0, &actual_handles, &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);
    EXPECT_EQ(actual_handles, 0u);
    EXPECT_EQ(reinterpret_cast<uint64_t>(message.inline_struct.string.data), FIDL_ALLOC_PRESENT);
    EXPECT_EQ(message.inline_struct.string.size, 6);
    EXPECT_EQ(message.data[0], 'h');
    EXPECT_EQ(message.data[1], 'e');
    EXPECT_EQ(message.data[2], 'l');
    EXPECT_EQ(message.data[3], 'l');
    EXPECT_EQ(message.data[4], 'o');
    EXPECT_EQ(message.data[5], '!');

    END_TEST;
}

bool encode_present_nullable_string() {
    BEGIN_TEST;

    unbounded_nullable_string_message_layout message = {};
    message.inline_struct.string = fidl_string_t{6, &message.data[0]};
    memcpy(message.data, "hello!", 6);

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&unbounded_nullable_string_message_type, &message, sizeof(message),
                              nullptr, 0, &actual_handles, &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);
    EXPECT_EQ(actual_handles, 0u);
    EXPECT_EQ(message.inline_struct.string.size, 6);
    EXPECT_EQ(message.data[0], 'h');
    EXPECT_EQ(message.data[1], 'e');
    EXPECT_EQ(message.data[2], 'l');
    EXPECT_EQ(message.data[3], 'l');
    EXPECT_EQ(message.data[4], 'o');
    EXPECT_EQ(message.data[5], '!');

    END_TEST;
}

bool encode_multiple_present_nullable_string() {
    BEGIN_TEST;

    // Among other things, this test ensures we handle out-of-line
    // alignment to FIDL_ALIGNMENT (i.e., 8) bytes correctly.
    multiple_nullable_strings_message_layout message;
    message.inline_struct.string = fidl_string_t{6, &message.data[0]};
    message.inline_struct.string2 = fidl_string_t{8, &message.data2[0]};
    memcpy(message.data, "hello ", 6);
    memcpy(message.data2, "world!!!", 8);

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&multiple_nullable_strings_message_type, &message, sizeof(message),
                              nullptr, 0, &actual_handles, &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);
    EXPECT_EQ(actual_handles, 0u);
    EXPECT_EQ(message.inline_struct.string.size, 6);
    EXPECT_EQ(reinterpret_cast<uint64_t>(message.inline_struct.string.data), FIDL_ALLOC_PRESENT);
    EXPECT_EQ(message.data[0], 'h');
    EXPECT_EQ(message.data[1], 'e');
    EXPECT_EQ(message.data[2], 'l');
    EXPECT_EQ(message.data[3], 'l');
    EXPECT_EQ(message.data[4], 'o');
    EXPECT_EQ(message.data[5], ' ');
    EXPECT_EQ(message.inline_struct.string2.size, 8);
    EXPECT_EQ(reinterpret_cast<uint64_t>(message.inline_struct.string2.data), FIDL_ALLOC_PRESENT);
    EXPECT_EQ(message.data2[0], 'w');
    EXPECT_EQ(message.data2[1], 'o');
    EXPECT_EQ(message.data2[2], 'r');
    EXPECT_EQ(message.data2[3], 'l');
    EXPECT_EQ(message.data2[4], 'd');
    EXPECT_EQ(message.data2[5], '!');
    EXPECT_EQ(message.data2[6], '!');
    EXPECT_EQ(message.data2[7], '!');

    END_TEST;
}

bool encode_absent_nonnullable_string_error() {
    BEGIN_TEST;

    unbounded_nonnullable_string_message_layout message = {};
    message.inline_struct.string = fidl_string_t{0u, nullptr};

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&unbounded_nonnullable_string_message_type, &message, sizeof(message),
                              nullptr, 0, &actual_handles, &error);

    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_NONNULL(error, error);

    END_TEST;
}

bool encode_absent_nullable_string() {
    BEGIN_TEST;

    unbounded_nullable_string_message_layout message = {};
    message.inline_struct.string = fidl_string_t{0u, nullptr};

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&unbounded_nullable_string_message_type, &message,
                              sizeof(message.inline_struct), nullptr, 0, &actual_handles, &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);
    EXPECT_EQ(actual_handles, 0u);
    EXPECT_EQ(reinterpret_cast<uint64_t>(message.inline_struct.string.data), FIDL_ALLOC_ABSENT);

    END_TEST;
}

bool encode_present_nonnullable_bounded_string() {
    BEGIN_TEST;

    bounded_32_nonnullable_string_message_layout message = {};
    message.inline_struct.string = fidl_string_t{6, &message.data[0]};
    memcpy(message.data, "hello!", 6);

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&bounded_32_nonnullable_string_message_type, &message,
                              sizeof(message), nullptr, 0, &actual_handles, &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);
    EXPECT_EQ(actual_handles, 0u);
    EXPECT_EQ(message.inline_struct.string.size, 6);
    EXPECT_EQ(reinterpret_cast<uint64_t>(message.inline_struct.string.data), FIDL_ALLOC_PRESENT);
    EXPECT_EQ(message.data[0], 'h');
    EXPECT_EQ(message.data[1], 'e');
    EXPECT_EQ(message.data[2], 'l');
    EXPECT_EQ(message.data[3], 'l');
    EXPECT_EQ(message.data[4], 'o');
    EXPECT_EQ(message.data[5], '!');

    END_TEST;
}

bool encode_present_nullable_bounded_string() {
    BEGIN_TEST;

    bounded_32_nullable_string_message_layout message = {};
    message.inline_struct.string = fidl_string_t{6, &message.data[0]};
    memcpy(message.data, "hello!", 6);

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&bounded_32_nullable_string_message_type, &message, sizeof(message),
                              nullptr, 0, &actual_handles, &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);
    EXPECT_EQ(actual_handles, 0u);
    EXPECT_EQ(message.inline_struct.string.size, 6);
    EXPECT_EQ(reinterpret_cast<uint64_t>(message.inline_struct.string.data), FIDL_ALLOC_PRESENT);
    EXPECT_EQ(message.data[0], 'h');
    EXPECT_EQ(message.data[1], 'e');
    EXPECT_EQ(message.data[2], 'l');
    EXPECT_EQ(message.data[3], 'l');
    EXPECT_EQ(message.data[4], 'o');
    EXPECT_EQ(message.data[5], '!');

    END_TEST;
}

bool encode_absent_nonnullable_bounded_string_error() {
    BEGIN_TEST;

    bounded_32_nonnullable_string_message_layout message = {};
    message.inline_struct.string = fidl_string_t{6, nullptr};

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&bounded_32_nonnullable_string_message_type, &message,
                              sizeof(message), nullptr, 0, &actual_handles, &error);

    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_NONNULL(error, error);
    EXPECT_EQ(reinterpret_cast<uint64_t>(message.inline_struct.string.data), FIDL_ALLOC_ABSENT);

    END_TEST;
}

bool encode_absent_nullable_bounded_string() {
    BEGIN_TEST;

    bounded_32_nullable_string_message_layout message = {};
    message.inline_struct.string = fidl_string_t{6, nullptr};

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&bounded_32_nullable_string_message_type, &message,
                              sizeof(message.inline_struct), nullptr, 0, &actual_handles, &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);
    EXPECT_EQ(reinterpret_cast<uint64_t>(message.inline_struct.string.data), FIDL_ALLOC_ABSENT);

    END_TEST;
}

bool encode_present_nonnullable_bounded_string_short_error() {
    BEGIN_TEST;

    multiple_short_nonnullable_strings_message_layout message = {};
    message.inline_struct.string = fidl_string_t{6, &message.data[0]};
    message.inline_struct.string2 = fidl_string_t{6, &message.data2[0]};
    memcpy(message.data, "hello!", 6);
    memcpy(message.data2, "hello!", 6);

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&multiple_short_nonnullable_strings_message_type, &message,
                              sizeof(message), nullptr, 0, &actual_handles, &error);

    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_NONNULL(error);

    END_TEST;
}

bool encode_present_nullable_bounded_string_short_error() {
    BEGIN_TEST;

    multiple_short_nullable_strings_message_layout message = {};
    message.inline_struct.string = fidl_string_t{6, &message.data[0]};
    message.inline_struct.string2 = fidl_string_t{6, &message.data2[0]};
    memcpy(message.data, "hello!", 6);
    memcpy(message.data2, "hello!", 6);

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&multiple_short_nullable_strings_message_type, &message,
                              sizeof(message), nullptr, 0, &actual_handles, &error);

    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_NONNULL(error);

    END_TEST;
}

bool encode_vector_with_huge_count() {
    BEGIN_TEST;

    unbounded_nonnullable_vector_of_uint32_message_layout message = {};
    // (2^30 + 4) * 4 (4 == sizeof(uint32_t)) overflows to 16 when stored as uint32_t.
    // We want 16 because it happens to be the actual size of the vector data in the message,
    // so we can trigger the overflow without triggering the "tried to claim too many bytes" or
    // "didn't use all the bytes in the message" errors.
    message.inline_struct.vector =
        fidl_vector_t{(1ull << 30) + 4, &message.uint32[0]};

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status =
        fidl_encode(&unbounded_nonnullable_vector_of_uint32_message_type, &message,
                    sizeof(message), nullptr, 0, &actual_handles, &error);

    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_NONNULL(error);
    const char expected_error_msg[] = "integer overflow calculating vector size";
    EXPECT_STR_EQ(expected_error_msg, error, "wrong error msg");
    EXPECT_EQ(actual_handles, 0u);

    END_TEST;
}

bool encode_present_nonnullable_vector_of_handles() {
    BEGIN_TEST;

    unbounded_nonnullable_vector_of_handles_message_layout message = {};
    message.inline_struct.vector = fidl_vector_t{4, &message.handles[0]};
    message.handles[0] = dummy_handle_0;
    message.handles[1] = dummy_handle_1;
    message.handles[2] = dummy_handle_2;
    message.handles[3] = dummy_handle_3;

    zx_handle_t handles[4] = {};

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status =
        fidl_encode(&unbounded_nonnullable_vector_of_handles_message_type, &message,
                    sizeof(message), handles, ArrayCount(handles), &actual_handles, &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);
    EXPECT_EQ(actual_handles, 4u);

    auto message_handles = reinterpret_cast<uint64_t>(message.inline_struct.vector.data);
    EXPECT_EQ(message_handles, FIDL_ALLOC_PRESENT);
    EXPECT_EQ(handles[0], dummy_handle_0);
    EXPECT_EQ(handles[1], dummy_handle_1);
    EXPECT_EQ(handles[2], dummy_handle_2);
    EXPECT_EQ(handles[3], dummy_handle_3);
    EXPECT_EQ(message.handles[0], FIDL_HANDLE_PRESENT);
    EXPECT_EQ(message.handles[1], FIDL_HANDLE_PRESENT);
    EXPECT_EQ(message.handles[2], FIDL_HANDLE_PRESENT);
    EXPECT_EQ(message.handles[3], FIDL_HANDLE_PRESENT);

    END_TEST;
}

bool encode_present_nullable_vector_of_handles() {
    BEGIN_TEST;

    unbounded_nullable_vector_of_handles_message_layout message = {};
    message.inline_struct.vector = fidl_vector_t{4, &message.handles[0]};
    message.handles[0] = dummy_handle_0;
    message.handles[1] = dummy_handle_1;
    message.handles[2] = dummy_handle_2;
    message.handles[3] = dummy_handle_3;

    zx_handle_t handles[4] = {};

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status =
        fidl_encode(&unbounded_nullable_vector_of_handles_message_type, &message, sizeof(message),
                    handles, ArrayCount(handles), &actual_handles, &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);
    EXPECT_EQ(actual_handles, 4u);

    auto message_handles = reinterpret_cast<uint64_t>(message.inline_struct.vector.data);
    EXPECT_EQ(message_handles, FIDL_ALLOC_PRESENT);
    EXPECT_EQ(handles[0], dummy_handle_0);
    EXPECT_EQ(handles[1], dummy_handle_1);
    EXPECT_EQ(handles[2], dummy_handle_2);
    EXPECT_EQ(handles[3], dummy_handle_3);
    EXPECT_EQ(message.handles[0], FIDL_HANDLE_PRESENT);
    EXPECT_EQ(message.handles[1], FIDL_HANDLE_PRESENT);
    EXPECT_EQ(message.handles[2], FIDL_HANDLE_PRESENT);
    EXPECT_EQ(message.handles[3], FIDL_HANDLE_PRESENT);

    END_TEST;
}

bool encode_absent_nonnullable_vector_of_handles_error() {
    BEGIN_TEST;

    unbounded_nonnullable_vector_of_handles_message_layout message = {};
    message.inline_struct.vector = fidl_vector_t{4, nullptr};

    zx_handle_t handles[4] = {};

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status =
        fidl_encode(&unbounded_nonnullable_vector_of_handles_message_type, &message,
                    sizeof(message), handles, ArrayCount(handles), &actual_handles, &error);

    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_NONNULL(error, error);

    END_TEST;
}

bool encode_absent_nullable_vector_of_handles() {
    BEGIN_TEST;

    unbounded_nullable_vector_of_handles_message_layout message = {};
    message.inline_struct.vector = fidl_vector_t{4, nullptr};

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&unbounded_nullable_vector_of_handles_message_type, &message,
                              sizeof(message.inline_struct), nullptr, 0u, &actual_handles, &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);
    EXPECT_EQ(actual_handles, 0u);

    auto message_handles = reinterpret_cast<uint64_t>(message.inline_struct.vector.data);
    EXPECT_EQ(message_handles, FIDL_ALLOC_ABSENT);

    END_TEST;
}

bool encode_present_nonnullable_bounded_vector_of_handles() {
    BEGIN_TEST;

    bounded_32_nonnullable_vector_of_handles_message_layout message = {};
    message.inline_struct.vector = fidl_vector_t{4, &message.handles[0]};
    message.handles[0] = dummy_handle_0;
    message.handles[1] = dummy_handle_1;
    message.handles[2] = dummy_handle_2;
    message.handles[3] = dummy_handle_3;

    zx_handle_t handles[4] = {};

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status =
        fidl_encode(&bounded_32_nonnullable_vector_of_handles_message_type, &message,
                    sizeof(message), handles, ArrayCount(handles), &actual_handles, &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);
    EXPECT_EQ(actual_handles, 4u);

    auto message_handles = reinterpret_cast<uint64_t>(message.inline_struct.vector.data);
    EXPECT_EQ(message_handles, FIDL_ALLOC_PRESENT);
    EXPECT_EQ(handles[0], dummy_handle_0);
    EXPECT_EQ(handles[1], dummy_handle_1);
    EXPECT_EQ(handles[2], dummy_handle_2);
    EXPECT_EQ(handles[3], dummy_handle_3);
    EXPECT_EQ(message.handles[0], FIDL_HANDLE_PRESENT);
    EXPECT_EQ(message.handles[1], FIDL_HANDLE_PRESENT);
    EXPECT_EQ(message.handles[2], FIDL_HANDLE_PRESENT);
    EXPECT_EQ(message.handles[3], FIDL_HANDLE_PRESENT);

    END_TEST;
}

bool encode_present_nullable_bounded_vector_of_handles() {
    BEGIN_TEST;

    bounded_32_nullable_vector_of_handles_message_layout message = {};
    message.inline_struct.vector = fidl_vector_t{4, &message.handles[0]};
    message.handles[0] = dummy_handle_0;
    message.handles[1] = dummy_handle_1;
    message.handles[2] = dummy_handle_2;
    message.handles[3] = dummy_handle_3;

    zx_handle_t handles[4] = {};

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status =
        fidl_encode(&bounded_32_nullable_vector_of_handles_message_type, &message, sizeof(message),
                    handles, ArrayCount(handles), &actual_handles, &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);
    EXPECT_EQ(actual_handles, 4u);

    auto message_handles = reinterpret_cast<uint64_t>(message.inline_struct.vector.data);
    EXPECT_EQ(message_handles, FIDL_ALLOC_PRESENT);
    EXPECT_EQ(handles[0], dummy_handle_0);
    EXPECT_EQ(handles[1], dummy_handle_1);
    EXPECT_EQ(handles[2], dummy_handle_2);
    EXPECT_EQ(handles[3], dummy_handle_3);
    EXPECT_EQ(message.handles[0], FIDL_HANDLE_PRESENT);
    EXPECT_EQ(message.handles[1], FIDL_HANDLE_PRESENT);
    EXPECT_EQ(message.handles[2], FIDL_HANDLE_PRESENT);
    EXPECT_EQ(message.handles[3], FIDL_HANDLE_PRESENT);

    END_TEST;
}

bool encode_absent_nonnullable_bounded_vector_of_handles() {
    BEGIN_TEST;

    bounded_32_nonnullable_vector_of_handles_message_layout message = {};
    message.inline_struct.vector = fidl_vector_t{4, nullptr};

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&bounded_32_nonnullable_vector_of_handles_message_type, &message,
                              sizeof(message), nullptr, 0u, &actual_handles, &error);

    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_NONNULL(error);

    END_TEST;
}

bool encode_absent_nullable_bounded_vector_of_handles() {
    BEGIN_TEST;

    bounded_32_nullable_vector_of_handles_message_layout message = {};
    message.inline_struct.vector = fidl_vector_t{4, nullptr};

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&bounded_32_nullable_vector_of_handles_message_type, &message,
                              sizeof(message.inline_struct), nullptr, 0u, &actual_handles, &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);
    EXPECT_EQ(actual_handles, 0u);

    auto message_handles = reinterpret_cast<uint64_t>(message.inline_struct.vector.data);
    EXPECT_EQ(message_handles, FIDL_ALLOC_ABSENT);

    END_TEST;
}

bool encode_present_nonnullable_bounded_vector_of_handles_short_error() {
    BEGIN_TEST;

    multiple_nonnullable_vectors_of_handles_message_layout message = {};
    message.inline_struct.vector = fidl_vector_t{4, &message.handles[0]};
    message.inline_struct.vector2 = fidl_vector_t{4, &message.handles2[0]};
    message.handles[0] = dummy_handle_0;
    message.handles[1] = dummy_handle_1;
    message.handles[2] = dummy_handle_2;
    message.handles[3] = dummy_handle_3;
    message.handles2[0] = dummy_handle_4;
    message.handles2[1] = dummy_handle_5;
    message.handles2[2] = dummy_handle_6;
    message.handles2[3] = dummy_handle_7;

    zx_handle_t handles[8] = {};

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status =
        fidl_encode(&multiple_nonnullable_vectors_of_handles_message_type, &message,
                    sizeof(message), handles, ArrayCount(handles), &actual_handles, &error);

    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_NONNULL(error);

    END_TEST;
}

bool encode_present_nullable_bounded_vector_of_handles_short_error() {
    BEGIN_TEST;

    multiple_nullable_vectors_of_handles_message_layout message = {};
    message.inline_struct.vector = fidl_vector_t{4, &message.handles[0]};
    message.inline_struct.vector2 = fidl_vector_t{4, &message.handles2[0]};
    message.handles[0] = dummy_handle_0;
    message.handles[1] = dummy_handle_1;
    message.handles[2] = dummy_handle_2;
    message.handles[3] = dummy_handle_3;
    message.handles2[0] = dummy_handle_4;
    message.handles2[1] = dummy_handle_5;
    message.handles2[2] = dummy_handle_6;
    message.handles2[3] = dummy_handle_7;

    zx_handle_t handles[8] = {};

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status =
        fidl_encode(&multiple_nullable_vectors_of_handles_message_type, &message, sizeof(message),
                    handles, ArrayCount(handles), &actual_handles, &error);

    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_NONNULL(error);

    END_TEST;
}

bool encode_present_nonnullable_vector_of_uint32() {
    BEGIN_TEST;

    unbounded_nonnullable_vector_of_uint32_message_layout message = {};
    message.inline_struct.vector = fidl_vector_t{4, &message.uint32[0]};

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status =
        fidl_encode(&unbounded_nonnullable_vector_of_uint32_message_type, &message,
                    sizeof(message), nullptr, 0, &actual_handles, &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);
    EXPECT_EQ(actual_handles, 0u);

    auto message_uint32 = reinterpret_cast<uint64_t>(message.inline_struct.vector.data);
    EXPECT_EQ(message_uint32, FIDL_ALLOC_PRESENT);

    END_TEST;
}

bool encode_present_nullable_vector_of_uint32() {
    BEGIN_TEST;

    unbounded_nullable_vector_of_uint32_message_layout message = {};
    message.inline_struct.vector = fidl_vector_t{4, &message.uint32[0]};

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status =
        fidl_encode(&unbounded_nullable_vector_of_uint32_message_type, &message, sizeof(message),
                    nullptr, 0, &actual_handles, &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);
    EXPECT_EQ(actual_handles, 0u);

    auto message_uint32 = reinterpret_cast<uint64_t>(message.inline_struct.vector.data);
    EXPECT_EQ(message_uint32, FIDL_ALLOC_PRESENT);

    END_TEST;
}

bool encode_absent_nonnullable_vector_of_uint32_error() {
    BEGIN_TEST;

    unbounded_nonnullable_vector_of_uint32_message_layout message = {};
    message.inline_struct.vector = fidl_vector_t{4, nullptr};

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status =
        fidl_encode(&unbounded_nonnullable_vector_of_uint32_message_type, &message,
                    sizeof(message), nullptr, 0, &actual_handles, &error);

    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_NONNULL(error, error);

    END_TEST;
}

bool encode_absent_nullable_vector_of_uint32() {
    BEGIN_TEST;

    unbounded_nullable_vector_of_uint32_message_layout message = {};
    message.inline_struct.vector = fidl_vector_t{4, nullptr};

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&unbounded_nullable_vector_of_uint32_message_type, &message,
                              sizeof(message.inline_struct), nullptr, 0u, &actual_handles, &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);
    EXPECT_EQ(actual_handles, 0u);

    auto message_uint32 = reinterpret_cast<uint64_t>(message.inline_struct.vector.data);
    EXPECT_EQ(message_uint32, FIDL_ALLOC_ABSENT);

    END_TEST;
}

bool encode_present_nonnullable_bounded_vector_of_uint32() {
    BEGIN_TEST;

    bounded_32_nonnullable_vector_of_uint32_message_layout message = {};
    message.inline_struct.vector = fidl_vector_t{4, &message.uint32[0]};

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status =
        fidl_encode(&bounded_32_nonnullable_vector_of_uint32_message_type, &message,
                    sizeof(message), nullptr, 0, &actual_handles, &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);
    EXPECT_EQ(actual_handles, 0u);

    auto message_uint32 = reinterpret_cast<uint64_t>(message.inline_struct.vector.data);
    EXPECT_EQ(message_uint32, FIDL_ALLOC_PRESENT);

    END_TEST;
}

bool encode_present_nullable_bounded_vector_of_uint32() {
    BEGIN_TEST;

    bounded_32_nullable_vector_of_uint32_message_layout message = {};
    message.inline_struct.vector = fidl_vector_t{4, &message.uint32[0]};

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status =
        fidl_encode(&bounded_32_nullable_vector_of_uint32_message_type, &message, sizeof(message),
                    nullptr, 0, &actual_handles, &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);
    EXPECT_EQ(actual_handles, 0u);

    auto message_uint32 = reinterpret_cast<uint64_t>(message.inline_struct.vector.data);
    EXPECT_EQ(message_uint32, FIDL_ALLOC_PRESENT);

    END_TEST;
}

bool encode_absent_nonnullable_bounded_vector_of_uint32() {
    BEGIN_TEST;

    bounded_32_nonnullable_vector_of_uint32_message_layout message = {};
    message.inline_struct.vector = fidl_vector_t{4, nullptr};

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&bounded_32_nonnullable_vector_of_uint32_message_type, &message,
                              sizeof(message), nullptr, 0u, &actual_handles, &error);

    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_NONNULL(error);

    END_TEST;
}

bool encode_absent_nullable_bounded_vector_of_uint32() {
    BEGIN_TEST;

    bounded_32_nullable_vector_of_uint32_message_layout message = {};
    message.inline_struct.vector = fidl_vector_t{4, nullptr};

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&bounded_32_nullable_vector_of_uint32_message_type, &message,
                              sizeof(message.inline_struct), nullptr, 0u, &actual_handles, &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);
    EXPECT_EQ(actual_handles, 0u);

    auto message_uint32 = reinterpret_cast<uint64_t>(message.inline_struct.vector.data);
    EXPECT_EQ(message_uint32, FIDL_ALLOC_ABSENT);

    END_TEST;
}

bool encode_present_nonnullable_bounded_vector_of_uint32_short_error() {
    BEGIN_TEST;

    multiple_nonnullable_vectors_of_uint32_message_layout message = {};
    message.inline_struct.vector = fidl_vector_t{4, &message.uint32[0]};
    message.inline_struct.vector2 = fidl_vector_t{4, &message.uint32_2[0]};

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status =
        fidl_encode(&multiple_nonnullable_vectors_of_uint32_message_type, &message,
                    sizeof(message), nullptr, 0, &actual_handles, &error);

    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_NONNULL(error);

    END_TEST;
}

bool encode_present_nullable_bounded_vector_of_uint32_short_error() {
    BEGIN_TEST;

    multiple_nullable_vectors_of_uint32_message_layout message = {};
    message.inline_struct.vector = fidl_vector_t{4, &message.uint32[0]};
    message.inline_struct.vector2 = fidl_vector_t{4, &message.uint32_2[0]};

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status =
        fidl_encode(&multiple_nullable_vectors_of_uint32_message_type, &message, sizeof(message),
                    nullptr, 0, &actual_handles, &error);

    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_NONNULL(error);

    END_TEST;
}

bool encode_bad_tagged_union_error() {
    BEGIN_TEST;

    nonnullable_handle_union_message_layout message = {};
    message.inline_struct.data.tag = 52u;
    message.inline_struct.data.handle = dummy_handle_0;

    zx_handle_t handles[1] = {};

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&nonnullable_handle_union_message_type, &message, sizeof(message),
                              handles, ArrayCount(handles), &actual_handles, &error);

    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_NONNULL(error);

    END_TEST;
}

bool encode_single_armed_present_nonnullable_union() {
    BEGIN_TEST;

    nonnullable_handle_union_message_layout message = {};
    message.inline_struct.data.tag = nonnullable_handle_union_kHandle;
    message.inline_struct.data.handle = dummy_handle_0;

    zx_handle_t handles[1] = {};

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&nonnullable_handle_union_message_type, &message, sizeof(message),
                              handles, ArrayCount(handles), &actual_handles, &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);
    EXPECT_EQ(actual_handles, 1u);
    EXPECT_EQ(message.inline_struct.data.tag, nonnullable_handle_union_kHandle);
    EXPECT_EQ(message.inline_struct.data.handle, FIDL_HANDLE_PRESENT);
    EXPECT_EQ(handles[0], dummy_handle_0);

    END_TEST;
}

bool encode_many_armed_present_nonnullable_union() {
    BEGIN_TEST;

    array_of_nonnullable_handles_union_message_layout message = {};
    message.inline_struct.data.tag = array_of_nonnullable_handles_union_kArrayOfArrayOfHandles;
    message.inline_struct.data.array_of_array_of_handles[0][0] = dummy_handle_0;
    message.inline_struct.data.array_of_array_of_handles[0][1] = dummy_handle_1;
    message.inline_struct.data.array_of_array_of_handles[1][0] = dummy_handle_2;
    message.inline_struct.data.array_of_array_of_handles[1][1] = dummy_handle_3;

    zx_handle_t handles[4] = {};

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status =
        fidl_encode(&array_of_nonnullable_handles_union_message_type, &message, sizeof(message),
                    handles, ArrayCount(handles), &actual_handles, &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);
    EXPECT_EQ(actual_handles, 4u);
    EXPECT_EQ(message.inline_struct.data.tag,
              array_of_nonnullable_handles_union_kArrayOfArrayOfHandles);
    EXPECT_EQ(message.inline_struct.data.array_of_array_of_handles[0][0], FIDL_HANDLE_PRESENT);
    EXPECT_EQ(message.inline_struct.data.array_of_array_of_handles[0][1], FIDL_HANDLE_PRESENT);
    EXPECT_EQ(message.inline_struct.data.array_of_array_of_handles[1][0], FIDL_HANDLE_PRESENT);
    EXPECT_EQ(message.inline_struct.data.array_of_array_of_handles[1][1], FIDL_HANDLE_PRESENT);
    EXPECT_EQ(handles[0], dummy_handle_0);
    EXPECT_EQ(handles[1], dummy_handle_1);
    EXPECT_EQ(handles[2], dummy_handle_2);
    EXPECT_EQ(handles[3], dummy_handle_3);

    END_TEST;
}

bool encode_single_armed_present_nullable_union() {
    BEGIN_TEST;

    nonnullable_handle_union_ptr_message_layout message = {};
    message.inline_struct.data = &message.data;
    message.data.tag = nonnullable_handle_union_kHandle;
    message.data.handle = dummy_handle_0;

    zx_handle_t handles[1] = {};

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&nonnullable_handle_union_ptr_message_type, &message, sizeof(message),
                              handles, ArrayCount(handles), &actual_handles, &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);
    EXPECT_EQ(actual_handles, 1u);
    EXPECT_EQ(reinterpret_cast<uint64_t>(message.inline_struct.data), FIDL_ALLOC_PRESENT);
    EXPECT_EQ(message.data.tag, nonnullable_handle_union_kHandle);
    EXPECT_EQ(message.data.handle, FIDL_HANDLE_PRESENT);
    EXPECT_EQ(handles[0], dummy_handle_0);

    END_TEST;
}

bool encode_many_armed_present_nullable_union() {
    BEGIN_TEST;

    array_of_nonnullable_handles_union_ptr_message_layout message = {};
    message.inline_struct.data = &message.data;
    message.data.tag = array_of_nonnullable_handles_union_kArrayOfArrayOfHandles;
    message.data.array_of_array_of_handles[0][0] = dummy_handle_0;
    message.data.array_of_array_of_handles[0][1] = dummy_handle_1;
    message.data.array_of_array_of_handles[1][0] = dummy_handle_2;
    message.data.array_of_array_of_handles[1][1] = dummy_handle_3;

    zx_handle_t handles[4] = {};

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status =
        fidl_encode(&array_of_nonnullable_handles_union_ptr_message_type, &message, sizeof(message),
                    handles, ArrayCount(handles), &actual_handles, &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);
    EXPECT_EQ(actual_handles, 4u);
    EXPECT_EQ(reinterpret_cast<uint64_t>(message.inline_struct.data), FIDL_ALLOC_PRESENT);
    EXPECT_EQ(message.data.tag, array_of_nonnullable_handles_union_kArrayOfArrayOfHandles);
    EXPECT_EQ(message.data.array_of_array_of_handles[0][0], FIDL_HANDLE_PRESENT);
    EXPECT_EQ(message.data.array_of_array_of_handles[0][1], FIDL_HANDLE_PRESENT);
    EXPECT_EQ(message.data.array_of_array_of_handles[1][0], FIDL_HANDLE_PRESENT);
    EXPECT_EQ(message.data.array_of_array_of_handles[1][1], FIDL_HANDLE_PRESENT);
    EXPECT_EQ(handles[0], dummy_handle_0);
    EXPECT_EQ(handles[1], dummy_handle_1);
    EXPECT_EQ(handles[2], dummy_handle_2);
    EXPECT_EQ(handles[3], dummy_handle_3);

    END_TEST;
}

bool encode_single_armed_absent_nullable_union() {
    BEGIN_TEST;

    nonnullable_handle_union_ptr_message_layout message = {};
    message.inline_struct.data = nullptr;

    zx_handle_t handles[1] = {};

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&nonnullable_handle_union_ptr_message_type, &message,
                              sizeof(message.inline_struct), handles, ArrayCount(handles),
                              &actual_handles, &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);
    EXPECT_EQ(actual_handles, 0u);
    EXPECT_EQ(reinterpret_cast<uint64_t>(message.inline_struct.data), FIDL_ALLOC_ABSENT);

    END_TEST;
}

bool encode_many_armed_absent_nullable_union() {
    BEGIN_TEST;

    array_of_nonnullable_handles_union_ptr_message_layout message = {};
    message.inline_struct.data = nullptr;

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&array_of_nonnullable_handles_union_ptr_message_type, &message,
                              sizeof(message.inline_struct), nullptr, 0u, &actual_handles, &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);
    EXPECT_EQ(actual_handles, 0u);
    EXPECT_EQ(reinterpret_cast<uint64_t>(message.inline_struct.data), FIDL_ALLOC_ABSENT);

    END_TEST;
}

bool encode_nested_nonnullable_structs() {
    BEGIN_TEST;

    // Note the traversal order! l1 -> l3 -> l2 -> l0
    nested_structs_message_layout message = {};
    message.inline_struct.l0.l1.handle_1 = dummy_handle_0;
    message.inline_struct.l0.l1.l2.l3.handle_3 = dummy_handle_1;
    message.inline_struct.l0.l1.l2.handle_2 = dummy_handle_2;
    message.inline_struct.l0.handle_0 = dummy_handle_3;

    zx_handle_t handles[4] = {};

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&nested_structs_message_type, &message, sizeof(message), handles,
                              ArrayCount(handles), &actual_handles, &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);

    EXPECT_EQ(message.inline_struct.l0.l1.handle_1, FIDL_HANDLE_PRESENT);
    EXPECT_EQ(message.inline_struct.l0.l1.l2.l3.handle_3, FIDL_HANDLE_PRESENT);
    EXPECT_EQ(message.inline_struct.l0.l1.l2.handle_2, FIDL_HANDLE_PRESENT);
    EXPECT_EQ(message.inline_struct.l0.handle_0, FIDL_HANDLE_PRESENT);

    EXPECT_EQ(handles[0], dummy_handle_0);
    EXPECT_EQ(handles[1], dummy_handle_1);
    EXPECT_EQ(handles[2], dummy_handle_2);
    EXPECT_EQ(handles[3], dummy_handle_3);

    END_TEST;
}

bool encode_nested_nullable_structs() {
    BEGIN_TEST;

    nested_struct_ptrs_message_layout message = {};
    message.inline_struct.l0_inline.l1_inline.l2_present = &message.in_in_out_2;
    message.inline_struct.l0_inline.l1_inline.l2_present->l3_present = &message.in_in_out_out_3;
    message.inline_struct.l0_inline.l1_inline.l2_inline.l3_present = &message.in_in_in_out_3;
    message.inline_struct.l0_inline.l1_present = &message.in_out_1;
    message.inline_struct.l0_inline.l1_present->l2_present = &message.in_out_out_2;
    message.inline_struct.l0_inline.l1_present->l2_present->l3_present = &message.in_out_out_out_3;
    message.inline_struct.l0_inline.l1_present->l2_inline.l3_present = &message.in_out_in_out_3;
    message.inline_struct.l0_present = &message.out_0;
    message.inline_struct.l0_present->l1_inline.l2_present = &message.out_in_out_2;
    message.inline_struct.l0_present->l1_inline.l2_present->l3_present = &message.out_in_out_out_3;
    message.inline_struct.l0_present->l1_inline.l2_inline.l3_present = &message.out_in_in_out_3;
    message.inline_struct.l0_present->l1_present = &message.out_out_1;
    message.inline_struct.l0_present->l1_present->l2_present = &message.out_out_out_2;
    message.inline_struct.l0_present->l1_present->l2_present->l3_present =
        &message.out_out_out_out_3;
    message.inline_struct.l0_present->l1_present->l2_inline.l3_present = &message.out_out_in_out_3;

    // 0 inline
    //     1 inline
    //         handle
    message.inline_struct.l0_inline.l1_inline.handle_1 = dummy_handle_0;
    //         2 out of line
    //             3 out of line
    message.in_in_out_out_3.handle_3 = dummy_handle_1;
    //             3 inline
    message.in_in_out_2.l3_inline.handle_3 = dummy_handle_2;
    //             handle
    message.in_in_out_2.handle_2 = dummy_handle_3;
    //         2 inline
    //             3 out of line
    message.in_in_in_out_3.handle_3 = dummy_handle_4;
    //             3 inline
    message.inline_struct.l0_inline.l1_inline.l2_inline.l3_inline.handle_3 = dummy_handle_5;
    //             handle
    message.inline_struct.l0_inline.l1_inline.l2_inline.handle_2 = dummy_handle_6;
    //     handle
    message.inline_struct.l0_inline.handle_0 = dummy_handle_7;
    //     1 out of line
    //         handle
    message.in_out_1.handle_1 = dummy_handle_8;
    //         2 out of line
    //             3 out of line
    message.in_out_out_out_3.handle_3 = dummy_handle_9;
    //             3 inline
    message.in_out_out_2.l3_inline.handle_3 = dummy_handle_10;
    //             handle
    message.in_out_out_2.handle_2 = dummy_handle_11;
    //         2 inline
    //             3 out of line
    message.in_out_in_out_3.handle_3 = dummy_handle_12;
    //             3 inline
    message.in_out_1.l2_inline.l3_inline.handle_3 = dummy_handle_13;
    //             handle
    message.in_out_1.l2_inline.handle_2 = dummy_handle_14;
    // 0 out of line
    //     1 inline
    //         handle
    message.out_0.l1_inline.handle_1 = dummy_handle_15;
    //         2 out of line
    //             3 out of line
    message.out_in_out_out_3.handle_3 = dummy_handle_16;
    //             3 inline
    message.out_in_out_2.l3_inline.handle_3 = dummy_handle_17;
    //             handle
    message.out_in_out_2.handle_2 = dummy_handle_18;
    //         2 inline
    //             3 out of line
    message.out_in_in_out_3.handle_3 = dummy_handle_19;
    //             3 inline
    message.out_0.l1_inline.l2_inline.l3_inline.handle_3 = dummy_handle_20;
    //             handle
    message.out_0.l1_inline.l2_inline.handle_2 = dummy_handle_21;
    //     handle
    message.out_0.handle_0 = dummy_handle_22;
    //     1 out of line
    //         handle
    message.out_out_1.handle_1 = dummy_handle_23;
    //         2 out of line
    //             3 out of line
    message.out_out_out_out_3.handle_3 = dummy_handle_24;
    //             3 inline
    message.out_out_out_2.l3_inline.handle_3 = dummy_handle_25;
    //             handle
    message.out_out_out_2.handle_2 = dummy_handle_26;
    //         2 inline
    //             3 out of line
    message.out_out_in_out_3.handle_3 = dummy_handle_27;
    //             3 inline
    message.out_out_1.l2_inline.l3_inline.handle_3 = dummy_handle_28;
    //             handle
    message.out_out_1.l2_inline.handle_2 = dummy_handle_29;

    zx_handle_t handles[30] = {};

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&nested_struct_ptrs_message_type, &message, sizeof(message), handles,
                              ArrayCount(handles), &actual_handles, &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);

    EXPECT_EQ(handles[0], dummy_handle_0);
    EXPECT_EQ(handles[1], dummy_handle_1);
    EXPECT_EQ(handles[2], dummy_handle_2);
    EXPECT_EQ(handles[3], dummy_handle_3);
    EXPECT_EQ(handles[4], dummy_handle_4);
    EXPECT_EQ(handles[5], dummy_handle_5);
    EXPECT_EQ(handles[6], dummy_handle_6);
    EXPECT_EQ(handles[7], dummy_handle_7);
    EXPECT_EQ(handles[8], dummy_handle_8);
    EXPECT_EQ(handles[9], dummy_handle_9);
    EXPECT_EQ(handles[10], dummy_handle_10);
    EXPECT_EQ(handles[11], dummy_handle_11);
    EXPECT_EQ(handles[12], dummy_handle_12);
    EXPECT_EQ(handles[13], dummy_handle_13);
    EXPECT_EQ(handles[14], dummy_handle_14);
    EXPECT_EQ(handles[15], dummy_handle_15);
    EXPECT_EQ(handles[16], dummy_handle_16);
    EXPECT_EQ(handles[17], dummy_handle_17);
    EXPECT_EQ(handles[18], dummy_handle_18);
    EXPECT_EQ(handles[19], dummy_handle_19);
    EXPECT_EQ(handles[20], dummy_handle_20);
    EXPECT_EQ(handles[21], dummy_handle_21);
    EXPECT_EQ(handles[22], dummy_handle_22);
    EXPECT_EQ(handles[23], dummy_handle_23);
    EXPECT_EQ(handles[24], dummy_handle_24);
    EXPECT_EQ(handles[25], dummy_handle_25);
    EXPECT_EQ(handles[26], dummy_handle_26);
    EXPECT_EQ(handles[27], dummy_handle_27);
    EXPECT_EQ(handles[28], dummy_handle_28);
    EXPECT_EQ(handles[29], dummy_handle_29);

    // Finally, check that all absent members are FIDL_ALLOC_ABSENT.
    EXPECT_EQ(reinterpret_cast<uintptr_t>(message.inline_struct.l0_absent), FIDL_ALLOC_ABSENT);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(message.inline_struct.l0_inline.l1_absent),
              FIDL_ALLOC_ABSENT);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(message.inline_struct.l0_inline.l1_inline.l2_absent),
              FIDL_ALLOC_ABSENT);
    EXPECT_EQ(
        reinterpret_cast<uintptr_t>(message.inline_struct.l0_inline.l1_inline.l2_inline.l3_absent),
        FIDL_ALLOC_ABSENT);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(message.in_in_out_2.l3_absent), FIDL_ALLOC_ABSENT);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(message.in_out_1.l2_absent), FIDL_ALLOC_ABSENT);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(message.in_out_1.l2_inline.l3_absent), FIDL_ALLOC_ABSENT);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(message.in_out_out_2.l3_absent), FIDL_ALLOC_ABSENT);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(message.out_0.l1_absent), FIDL_ALLOC_ABSENT);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(message.out_0.l1_inline.l2_absent), FIDL_ALLOC_ABSENT);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(message.out_0.l1_inline.l2_inline.l3_absent),
              FIDL_ALLOC_ABSENT);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(message.out_in_out_2.l3_absent), FIDL_ALLOC_ABSENT);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(message.out_out_1.l2_absent), FIDL_ALLOC_ABSENT);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(message.out_out_1.l2_inline.l3_absent),
              FIDL_ALLOC_ABSENT);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(message.out_out_out_2.l3_absent), FIDL_ALLOC_ABSENT);

    END_TEST;
}

void SetUpRecursionMessage(recursion_message_layout* message) {
    message->inline_struct.inline_union.tag = maybe_recurse_union_kMore;
    message->inline_struct.inline_union.more = &message->depth_0;
    message->depth_0.inline_union.tag = maybe_recurse_union_kMore;
    message->depth_0.inline_union.more = &message->depth_1;
    message->depth_1.inline_union.tag = maybe_recurse_union_kMore;
    message->depth_1.inline_union.more = &message->depth_2;
    message->depth_2.inline_union.tag = maybe_recurse_union_kMore;
    message->depth_2.inline_union.more = &message->depth_3;
    message->depth_3.inline_union.tag = maybe_recurse_union_kMore;
    message->depth_3.inline_union.more = &message->depth_4;
    message->depth_4.inline_union.tag = maybe_recurse_union_kMore;
    message->depth_4.inline_union.more = &message->depth_5;
    message->depth_5.inline_union.tag = maybe_recurse_union_kMore;
    message->depth_5.inline_union.more = &message->depth_6;
    message->depth_6.inline_union.tag = maybe_recurse_union_kMore;
    message->depth_6.inline_union.more = &message->depth_7;
    message->depth_7.inline_union.tag = maybe_recurse_union_kMore;
    message->depth_7.inline_union.more = &message->depth_8;
    message->depth_8.inline_union.tag = maybe_recurse_union_kMore;
    message->depth_8.inline_union.more = &message->depth_9;
    message->depth_9.inline_union.tag = maybe_recurse_union_kMore;
    message->depth_9.inline_union.more = &message->depth_10;
    message->depth_10.inline_union.tag = maybe_recurse_union_kMore;
    message->depth_10.inline_union.more = &message->depth_11;
    message->depth_11.inline_union.tag = maybe_recurse_union_kMore;
    message->depth_11.inline_union.more = &message->depth_12;
    message->depth_12.inline_union.tag = maybe_recurse_union_kMore;
    message->depth_12.inline_union.more = &message->depth_13;
    message->depth_13.inline_union.tag = maybe_recurse_union_kMore;
    message->depth_13.inline_union.more = &message->depth_14;
    message->depth_14.inline_union.tag = maybe_recurse_union_kMore;
    message->depth_14.inline_union.more = &message->depth_15;
    message->depth_15.inline_union.tag = maybe_recurse_union_kMore;
    message->depth_15.inline_union.more = &message->depth_16;
    message->depth_16.inline_union.tag = maybe_recurse_union_kMore;
    message->depth_16.inline_union.more = &message->depth_17;
    message->depth_17.inline_union.tag = maybe_recurse_union_kMore;
    message->depth_17.inline_union.more = &message->depth_18;
    message->depth_18.inline_union.tag = maybe_recurse_union_kMore;
    message->depth_18.inline_union.more = &message->depth_19;
    message->depth_19.inline_union.tag = maybe_recurse_union_kMore;
    message->depth_19.inline_union.more = &message->depth_20;
    message->depth_20.inline_union.tag = maybe_recurse_union_kMore;
    message->depth_20.inline_union.more = &message->depth_21;
    message->depth_21.inline_union.tag = maybe_recurse_union_kMore;
    message->depth_21.inline_union.more = &message->depth_22;
    message->depth_22.inline_union.tag = maybe_recurse_union_kMore;
    message->depth_22.inline_union.more = &message->depth_23;
    message->depth_23.inline_union.tag = maybe_recurse_union_kMore;
    message->depth_23.inline_union.more = &message->depth_24;
    message->depth_24.inline_union.tag = maybe_recurse_union_kMore;
    message->depth_24.inline_union.more = &message->depth_25;
    message->depth_25.inline_union.tag = maybe_recurse_union_kMore;
    message->depth_25.inline_union.more = &message->depth_26;
    message->depth_26.inline_union.tag = maybe_recurse_union_kMore;
    message->depth_26.inline_union.more = &message->depth_27;
    message->depth_27.inline_union.tag = maybe_recurse_union_kMore;
    message->depth_27.inline_union.more = &message->depth_28;
}

bool encode_nested_struct_recursion_too_deep_error() {
    BEGIN_TEST;

    recursion_message_layout message = {};
    // First we check that FIDL_RECURSION_DEPTH - 1 levels of recursion is OK.
    SetUpRecursionMessage(&message);
    message.depth_28.inline_union.tag = maybe_recurse_union_kDone;
    message.depth_28.inline_union.handle = dummy_handle_0;

    zx_handle_t handles[1] = {};

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status =
        fidl_encode(&recursion_message_type, &message,
                    // Tell it to ignore everything after we stop recursion.
                    offsetof(recursion_message_layout, depth_29), handles,
                    ArrayCount(handles), &actual_handles, &error);
    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);

    // Now add another level of recursion.
    SetUpRecursionMessage(&message);
    message.depth_28.inline_union.tag = maybe_recurse_union_kMore;
    message.depth_28.inline_union.more = &message.depth_29;
    message.depth_29.inline_union.tag = maybe_recurse_union_kDone;
    message.depth_29.inline_union.handle = dummy_handle_0;

    error = nullptr;
    actual_handles = 0u;
    status = fidl_encode(&recursion_message_type, &message, sizeof(message), handles,
                         ArrayCount(handles), &actual_handles, &error);
    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_NONNULL(error);
    const char expected_error_msg[] = "recursion depth exceeded encoding struct";
    EXPECT_STR_EQ(expected_error_msg, error, "wrong error msg");

    END_TEST;
}

BEGIN_TEST_CASE(null_parameters)
RUN_TEST(encode_null_encode_parameters)
END_TEST_CASE(null_parameters)

BEGIN_TEST_CASE(handles)
RUN_TEST(encode_single_present_handle)
RUN_TEST(encode_single_present_handle_unaligned_error)
RUN_TEST(encode_multiple_present_handles)
RUN_TEST(encode_single_absent_handle)
RUN_TEST(encode_multiple_absent_handles)
END_TEST_CASE(handles)

BEGIN_TEST_CASE(arrays)
RUN_TEST(encode_array_of_present_handles)
RUN_TEST(encode_array_of_nullable_handles)
RUN_TEST(encode_array_of_nullable_handles_with_insufficient_handles_error)
RUN_TEST(encode_array_of_array_of_present_handles)
RUN_TEST(encode_out_of_line_array_of_nonnullable_handles)
RUN_TEST(encode_array_of_present_handles_error_closes_handles)
END_TEST_CASE(arrays)

BEGIN_TEST_CASE(strings)
RUN_TEST(encode_present_nonnullable_string)
RUN_TEST(encode_multiple_present_nullable_string)
RUN_TEST(encode_present_nullable_string)
RUN_TEST(encode_absent_nonnullable_string_error)
RUN_TEST(encode_absent_nullable_string)
RUN_TEST(encode_present_nonnullable_bounded_string)
RUN_TEST(encode_present_nullable_bounded_string)
RUN_TEST(encode_absent_nonnullable_bounded_string_error)
RUN_TEST(encode_absent_nullable_bounded_string)
RUN_TEST(encode_present_nonnullable_bounded_string_short_error)
RUN_TEST(encode_present_nullable_bounded_string_short_error)
END_TEST_CASE(strings)

BEGIN_TEST_CASE(vectors)
RUN_TEST(encode_vector_with_huge_count)
RUN_TEST(encode_present_nonnullable_vector_of_handles)
RUN_TEST(encode_present_nullable_vector_of_handles)
RUN_TEST(encode_absent_nonnullable_vector_of_handles_error)
RUN_TEST(encode_absent_nullable_vector_of_handles)
RUN_TEST(encode_present_nonnullable_bounded_vector_of_handles)
RUN_TEST(encode_present_nullable_bounded_vector_of_handles)
RUN_TEST(encode_absent_nonnullable_bounded_vector_of_handles)
RUN_TEST(encode_absent_nullable_bounded_vector_of_handles)
RUN_TEST(encode_present_nonnullable_bounded_vector_of_handles_short_error)
RUN_TEST(encode_present_nullable_bounded_vector_of_handles_short_error)
RUN_TEST(encode_present_nonnullable_vector_of_uint32)
RUN_TEST(encode_present_nullable_vector_of_uint32)
RUN_TEST(encode_absent_nonnullable_vector_of_uint32_error)
RUN_TEST(encode_absent_nullable_vector_of_uint32)
RUN_TEST(encode_present_nonnullable_bounded_vector_of_uint32)
RUN_TEST(encode_present_nullable_bounded_vector_of_uint32)
RUN_TEST(encode_absent_nonnullable_bounded_vector_of_uint32)
RUN_TEST(encode_absent_nullable_bounded_vector_of_uint32)
RUN_TEST(encode_present_nonnullable_bounded_vector_of_uint32_short_error)
RUN_TEST(encode_present_nullable_bounded_vector_of_uint32_short_error)
END_TEST_CASE(vectors)

BEGIN_TEST_CASE(unions)
RUN_TEST(encode_bad_tagged_union_error)
RUN_TEST(encode_single_armed_present_nonnullable_union)
RUN_TEST(encode_many_armed_present_nonnullable_union)
RUN_TEST(encode_single_armed_present_nullable_union)
RUN_TEST(encode_many_armed_present_nullable_union)
RUN_TEST(encode_single_armed_absent_nullable_union)
RUN_TEST(encode_many_armed_absent_nullable_union)
END_TEST_CASE(unions)

BEGIN_TEST_CASE(structs)
RUN_TEST(encode_nested_nonnullable_structs)
RUN_TEST(encode_nested_nullable_structs)
RUN_TEST(encode_nested_struct_recursion_too_deep_error)
END_TEST_CASE(structs)

} // namespace
} // namespace fidl
