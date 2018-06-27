// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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

bool decode_null_decode_parameters() {
    BEGIN_TEST;

    zx_handle_t handles[] = { static_cast<zx_handle_t>(23) };

    // Null message type.
    {
        nonnullable_handle_message_layout message = {};
        message.inline_struct.handle = FIDL_HANDLE_PRESENT;
        const char* error = nullptr;
        auto status = fidl_decode(nullptr, &message, sizeof(nonnullable_handle_message_layout),
                                  handles, ArrayCount(handles), &error);
        EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
        EXPECT_NONNULL(error);
    }

    // Null message.
    {
        const char* error = nullptr;
        auto status = fidl_decode(&nonnullable_handle_message_type, nullptr,
                                  sizeof(nonnullable_handle_message_layout), handles,
                                  ArrayCount(handles), &error);
        EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
        EXPECT_NONNULL(error);
    }

    // Null handles, for a message that has a handle.
    {
        nonnullable_handle_message_layout message = {};
        message.inline_struct.handle = FIDL_HANDLE_PRESENT;
        const char* error = nullptr;
        auto status = fidl_decode(&nonnullable_handle_message_type, &message,
                                  sizeof(nonnullable_handle_message_layout), nullptr, 0, &error);
        EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
        EXPECT_NONNULL(error);
    }

    // Null handles but positive handle count.
    {
        nonnullable_handle_message_layout message = {};
        message.inline_struct.handle = FIDL_HANDLE_PRESENT;
        const char* error = nullptr;
        auto status = fidl_decode(&nonnullable_handle_message_type, &message,
                                  sizeof(nonnullable_handle_message_layout), nullptr, 1, &error);
        EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
        EXPECT_NONNULL(error);
    }

    // A null error string pointer is ok, though.
    {
        auto status = fidl_decode(nullptr, nullptr, 0u, nullptr, 0u, nullptr);
        EXPECT_NE(status, ZX_OK);
    }

    // A null error is also ok in success cases.
    {
        nonnullable_handle_message_layout message = {};
        message.inline_struct.handle = FIDL_HANDLE_PRESENT;
        auto status = fidl_decode(&nonnullable_handle_message_type, &message,
                                  sizeof(nonnullable_handle_message_layout), handles,
                                  ArrayCount(handles), nullptr);
        EXPECT_EQ(status, ZX_OK);
    }

    END_TEST;
}

bool decode_single_present_handle() {
    BEGIN_TEST;

    nonnullable_handle_message_layout message = {};
    message.inline_struct.handle = FIDL_HANDLE_PRESENT;

    zx_handle_t handles[] = {
        dummy_handle_0,
    };

    const char* error = nullptr;
    auto status = fidl_decode(&nonnullable_handle_message_type, &message, sizeof(message), handles,
                              ArrayCount(handles), &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);
    EXPECT_EQ(message.inline_struct.handle, dummy_handle_0);

    END_TEST;
}

bool decode_too_many_handles_specified_error() {
    BEGIN_TEST;

    nonnullable_handle_message_layout message = {};
    message.inline_struct.handle = FIDL_HANDLE_PRESENT;

    zx_handle_t handles[] = {
        dummy_handle_0,
        ZX_HANDLE_INVALID,
    };

    const char* error = nullptr;
    auto status = fidl_decode(&nonnullable_handle_message_type, &message, sizeof(message), handles,
                              ArrayCount(handles), &error);

    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_NONNULL(error, error);
    EXPECT_EQ(message.inline_struct.handle, dummy_handle_0);

    END_TEST;
}

bool decode_single_present_handle_unaligned_error() {
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
    message.inline_struct.handle = FIDL_HANDLE_PRESENT;

    zx_handle_t handles[] = {
        dummy_handle_0,
    };

    // Decoding the unaligned version of the struct should fail.
    const char* error = nullptr;
    auto status = fidl_decode(&nonnullable_handle_message_type, &message, sizeof(message), handles,
                              ArrayCount(handles), &error);

    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_NONNULL(error);

    END_TEST;
}

bool decode_multiple_present_handles() {
    BEGIN_TEST;

    multiple_nonnullable_handles_message_layout message = {};
    message.inline_struct.handle_0 = FIDL_HANDLE_PRESENT;
    message.inline_struct.handle_1 = FIDL_HANDLE_PRESENT;
    message.inline_struct.handle_2 = FIDL_HANDLE_PRESENT;

    zx_handle_t handles[] = {
        dummy_handle_0,
        dummy_handle_1,
        dummy_handle_2,
    };

    const char* error = nullptr;
    auto status = fidl_decode(&multiple_nonnullable_handles_message_type, &message, sizeof(message),
                              handles, ArrayCount(handles), &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);
    EXPECT_EQ(message.inline_struct.data_0, 0u);
    EXPECT_EQ(message.inline_struct.handle_0, dummy_handle_0);
    EXPECT_EQ(message.inline_struct.data_1, 0u);
    EXPECT_EQ(message.inline_struct.handle_1, dummy_handle_1);
    EXPECT_EQ(message.inline_struct.handle_2, dummy_handle_2);
    EXPECT_EQ(message.inline_struct.data_2, 0u);

    END_TEST;
}

bool decode_single_absent_handle() {
    BEGIN_TEST;

    nullable_handle_message_layout message = {};
    message.inline_struct.handle = FIDL_HANDLE_ABSENT;

    const char* error = nullptr;
    auto status =
        fidl_decode(&nullable_handle_message_type, &message, sizeof(message), nullptr, 0, &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);
    EXPECT_EQ(message.inline_struct.handle, ZX_HANDLE_INVALID);

    END_TEST;
}

bool decode_multiple_absent_handles() {
    BEGIN_TEST;

    multiple_nullable_handles_message_layout message = {};
    message.inline_struct.handle_0 = FIDL_HANDLE_ABSENT;
    message.inline_struct.handle_1 = FIDL_HANDLE_ABSENT;
    message.inline_struct.handle_2 = FIDL_HANDLE_ABSENT;

    const char* error = nullptr;
    auto status = fidl_decode(&multiple_nullable_handles_message_type, &message, sizeof(message),
                              nullptr, 0, &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);
    EXPECT_EQ(message.inline_struct.data_0, 0u);
    EXPECT_EQ(message.inline_struct.handle_0, ZX_HANDLE_INVALID);
    EXPECT_EQ(message.inline_struct.data_1, 0u);
    EXPECT_EQ(message.inline_struct.handle_1, ZX_HANDLE_INVALID);
    EXPECT_EQ(message.inline_struct.handle_2, ZX_HANDLE_INVALID);
    EXPECT_EQ(message.inline_struct.data_2, 0u);

    END_TEST;
}

bool decode_array_of_present_handles() {
    BEGIN_TEST;

    array_of_nonnullable_handles_message_layout message = {};
    message.inline_struct.handles[0] = FIDL_HANDLE_PRESENT;
    message.inline_struct.handles[1] = FIDL_HANDLE_PRESENT;
    message.inline_struct.handles[2] = FIDL_HANDLE_PRESENT;
    message.inline_struct.handles[3] = FIDL_HANDLE_PRESENT;

    zx_handle_t handles[] = {
        dummy_handle_0,
        dummy_handle_1,
        dummy_handle_2,
        dummy_handle_3,
    };

    const char* error = nullptr;
    auto status = fidl_decode(&array_of_nonnullable_handles_message_type, &message, sizeof(message),
                              handles, ArrayCount(handles), &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);
    EXPECT_EQ(message.inline_struct.handles[0], dummy_handle_0);
    EXPECT_EQ(message.inline_struct.handles[1], dummy_handle_1);
    EXPECT_EQ(message.inline_struct.handles[2], dummy_handle_2);
    EXPECT_EQ(message.inline_struct.handles[3], dummy_handle_3);

    END_TEST;
}

bool decode_array_of_present_handles_error_closes_handles() {
    BEGIN_TEST;

    array_of_nonnullable_handles_message_layout message = {};
    zx_handle_t handle_pairs[4][2];
    // Use eventpairs so that we can know for sure that handles were closed by fidl_decode.
    for (uint32_t i = 0; i < ArrayCount(handle_pairs); ++i) {
        ASSERT_EQ(zx_eventpair_create(0u, &handle_pairs[i][0], &handle_pairs[i][1]), ZX_OK);
    }
    message.inline_struct.handles[0] = FIDL_HANDLE_PRESENT;
    message.inline_struct.handles[1] = FIDL_HANDLE_PRESENT;
    message.inline_struct.handles[2] = FIDL_HANDLE_PRESENT;
    message.inline_struct.handles[3] = FIDL_HANDLE_PRESENT;

    zx_handle_t out_of_line_handles[4] = {
        handle_pairs[0][0], handle_pairs[1][0], handle_pairs[2][0], handle_pairs[3][0],
    };

    const char* error = nullptr;
    auto status = fidl_decode(&array_of_nonnullable_handles_message_type, &message, sizeof(message),
                              out_of_line_handles,
                              // -2 makes this invalid.
                              ArrayCount(out_of_line_handles) - 2, &error);
    // Should fail because we we pass in a max_handles < the actual number of handles.
    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    // All the handles that we told fidl_decode about should be closed.
    uint32_t i;
    for (i = 0; i < ArrayCount(handle_pairs) - 2; ++i) {
        zx_signals_t observed_signals;
        EXPECT_EQ(zx_object_wait_one(handle_pairs[i][1],
                                     ZX_EVENTPAIR_PEER_CLOSED,
                                     1, // deadline shouldn't matter, should return immediately.
                                     &observed_signals),
                   ZX_OK);
        EXPECT_EQ(observed_signals & ZX_EVENTPAIR_PEER_CLOSED, ZX_EVENTPAIR_PEER_CLOSED);
        EXPECT_EQ(zx_handle_close(handle_pairs[i][1]), ZX_OK); // [i][0] was closed by fidl_encode.
    }
    // But the other ones should not be.
    for (; i < ArrayCount(handle_pairs); ++i) {
        zx_signals_t observed_signals;
        EXPECT_EQ(zx_object_wait_one(handle_pairs[i][1],
                                     ZX_EVENTPAIR_PEER_CLOSED,
                                     zx_clock_get_monotonic() + 1,
                                     &observed_signals),
                   ZX_ERR_TIMED_OUT);
        EXPECT_EQ(observed_signals & ZX_EVENTPAIR_PEER_CLOSED, 0);
        EXPECT_EQ(zx_handle_close(handle_pairs[i][0]), ZX_OK);
        EXPECT_EQ(zx_handle_close(handle_pairs[i][1]), ZX_OK);
    }

    END_TEST;
}

bool decode_array_of_nonnullable_handles_some_absent_error() {
    BEGIN_TEST;

    array_of_nonnullable_handles_message_layout message = {};
    message.inline_struct.handles[0] = FIDL_HANDLE_PRESENT;
    message.inline_struct.handles[1] = FIDL_HANDLE_PRESENT;
    message.inline_struct.handles[2] = FIDL_HANDLE_PRESENT;
    message.inline_struct.handles[3] = FIDL_HANDLE_ABSENT;

    zx_handle_t handles[] = {
        dummy_handle_0,
        dummy_handle_1,
        dummy_handle_2,
        dummy_handle_3,
    };

    const char* error = nullptr;
    auto status = fidl_decode(&array_of_nonnullable_handles_message_type, &message, sizeof(message),
                              handles, ArrayCount(handles), &error);

    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_NONNULL(error);

    END_TEST;
}

bool decode_array_of_nullable_handles() {
    BEGIN_TEST;

    array_of_nullable_handles_message_layout message = {};
    message.inline_struct.handles[0] = FIDL_HANDLE_PRESENT;
    message.inline_struct.handles[1] = FIDL_HANDLE_ABSENT;
    message.inline_struct.handles[2] = FIDL_HANDLE_PRESENT;
    message.inline_struct.handles[3] = FIDL_HANDLE_ABSENT;
    message.inline_struct.handles[4] = FIDL_HANDLE_PRESENT;

    zx_handle_t handles[] = {
        dummy_handle_0,
        dummy_handle_1,
        dummy_handle_2,
    };

    const char* error = nullptr;
    auto status = fidl_decode(&array_of_nullable_handles_message_type, &message, sizeof(message),
                              handles, ArrayCount(handles), &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);
    EXPECT_EQ(message.inline_struct.handles[0], dummy_handle_0);
    EXPECT_EQ(message.inline_struct.handles[1], ZX_HANDLE_INVALID);
    EXPECT_EQ(message.inline_struct.handles[2], dummy_handle_1);
    EXPECT_EQ(message.inline_struct.handles[3], ZX_HANDLE_INVALID);
    EXPECT_EQ(message.inline_struct.handles[4], dummy_handle_2);

    END_TEST;
}

bool decode_array_of_nullable_handles_with_insufficient_handles_error() {
    BEGIN_TEST;

    array_of_nullable_handles_message_layout message = {};
    message.inline_struct.handles[0] = FIDL_HANDLE_PRESENT;
    message.inline_struct.handles[1] = FIDL_HANDLE_ABSENT;
    message.inline_struct.handles[2] = FIDL_HANDLE_PRESENT;
    message.inline_struct.handles[3] = FIDL_HANDLE_ABSENT;
    message.inline_struct.handles[4] = FIDL_HANDLE_PRESENT;

    zx_handle_t handles[] = {
        dummy_handle_0,
        dummy_handle_1,
    };

    const char* error = nullptr;
    auto status = fidl_decode(&array_of_nullable_handles_message_type, &message, sizeof(message),
                              handles, ArrayCount(handles), &error);

    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_NONNULL(error);

    END_TEST;
}

bool decode_array_of_array_of_present_handles() {
    BEGIN_TEST;

    array_of_array_of_nonnullable_handles_message_layout message = {};
    message.inline_struct.handles[0][0] = FIDL_HANDLE_PRESENT;
    message.inline_struct.handles[0][1] = FIDL_HANDLE_PRESENT;
    message.inline_struct.handles[0][2] = FIDL_HANDLE_PRESENT;
    message.inline_struct.handles[0][3] = FIDL_HANDLE_PRESENT;
    message.inline_struct.handles[1][0] = FIDL_HANDLE_PRESENT;
    message.inline_struct.handles[1][1] = FIDL_HANDLE_PRESENT;
    message.inline_struct.handles[1][2] = FIDL_HANDLE_PRESENT;
    message.inline_struct.handles[1][3] = FIDL_HANDLE_PRESENT;
    message.inline_struct.handles[2][0] = FIDL_HANDLE_PRESENT;
    message.inline_struct.handles[2][1] = FIDL_HANDLE_PRESENT;
    message.inline_struct.handles[2][2] = FIDL_HANDLE_PRESENT;
    message.inline_struct.handles[2][3] = FIDL_HANDLE_PRESENT;

    zx_handle_t handles[] = {
        dummy_handle_0, dummy_handle_1, dummy_handle_2,  dummy_handle_3,
        dummy_handle_4, dummy_handle_5, dummy_handle_6,  dummy_handle_7,
        dummy_handle_8, dummy_handle_9, dummy_handle_10, dummy_handle_11,
    };

    const char* error = nullptr;
    auto status = fidl_decode(&array_of_array_of_nonnullable_handles_message_type, &message,
                              sizeof(message), handles, ArrayCount(handles), &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);
    EXPECT_EQ(message.inline_struct.handles[0][0], dummy_handle_0);
    EXPECT_EQ(message.inline_struct.handles[0][1], dummy_handle_1);
    EXPECT_EQ(message.inline_struct.handles[0][2], dummy_handle_2);
    EXPECT_EQ(message.inline_struct.handles[0][3], dummy_handle_3);
    EXPECT_EQ(message.inline_struct.handles[1][0], dummy_handle_4);
    EXPECT_EQ(message.inline_struct.handles[1][1], dummy_handle_5);
    EXPECT_EQ(message.inline_struct.handles[1][2], dummy_handle_6);
    EXPECT_EQ(message.inline_struct.handles[1][3], dummy_handle_7);
    EXPECT_EQ(message.inline_struct.handles[2][0], dummy_handle_8);
    EXPECT_EQ(message.inline_struct.handles[2][1], dummy_handle_9);
    EXPECT_EQ(message.inline_struct.handles[2][2], dummy_handle_10);
    EXPECT_EQ(message.inline_struct.handles[2][3], dummy_handle_11);

    END_TEST;
}

bool decode_out_of_line_array() {
    BEGIN_TEST;

    out_of_line_array_of_nonnullable_handles_message_layout message = {};
    message.inline_struct.maybe_array =
        reinterpret_cast<array_of_nonnullable_handles*>(FIDL_ALLOC_PRESENT);
    message.data.handles[0] = FIDL_HANDLE_PRESENT;
    message.data.handles[1] = FIDL_HANDLE_PRESENT;
    message.data.handles[2] = FIDL_HANDLE_PRESENT;
    message.data.handles[3] = FIDL_HANDLE_PRESENT;

    zx_handle_t handles[] = {
        dummy_handle_0,
        dummy_handle_1,
        dummy_handle_2,
        dummy_handle_3,
    };

    const char* error = nullptr;
    auto status = fidl_decode(&out_of_line_array_of_nonnullable_handles_message_type, &message,
                              sizeof(message), handles, ArrayCount(handles), &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);

    auto array_ptr = message.inline_struct.maybe_array;
    EXPECT_NONNULL(array_ptr);
    EXPECT_EQ(array_ptr->handles[0], dummy_handle_0);
    EXPECT_EQ(array_ptr->handles[1], dummy_handle_1);
    EXPECT_EQ(array_ptr->handles[2], dummy_handle_2);
    EXPECT_EQ(array_ptr->handles[3], dummy_handle_3);

    END_TEST;
}

bool decode_present_nonnullable_string() {
    BEGIN_TEST;

    unbounded_nonnullable_string_message_layout message = {};
    message.inline_struct.string = fidl_string_t{6, reinterpret_cast<char*>(FIDL_ALLOC_PRESENT)};
    memcpy(message.data, "hello!", 6);

    const char* error = nullptr;
    auto status = fidl_decode(&unbounded_nonnullable_string_message_type, &message, sizeof(message),
                              nullptr, 0, &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);
    EXPECT_EQ(message.inline_struct.string.size, 6);
    EXPECT_EQ(message.inline_struct.string.data[0], 'h');
    EXPECT_EQ(message.inline_struct.string.data[1], 'e');
    EXPECT_EQ(message.inline_struct.string.data[2], 'l');
    EXPECT_EQ(message.inline_struct.string.data[3], 'l');
    EXPECT_EQ(message.inline_struct.string.data[4], 'o');
    EXPECT_EQ(message.inline_struct.string.data[5], '!');

    END_TEST;
}

bool decode_present_nullable_string() {
    BEGIN_TEST;

    unbounded_nullable_string_message_layout message = {};
    message.inline_struct.string = fidl_string_t{6, reinterpret_cast<char*>(FIDL_ALLOC_PRESENT)};
    memcpy(message.data, "hello!", 6);

    const char* error = nullptr;
    auto status = fidl_decode(&unbounded_nullable_string_message_type, &message, sizeof(message),
                              nullptr, 0, &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);
    EXPECT_EQ(message.inline_struct.string.size, 6);
    EXPECT_EQ(message.inline_struct.string.data[0], 'h');
    EXPECT_EQ(message.inline_struct.string.data[1], 'e');
    EXPECT_EQ(message.inline_struct.string.data[2], 'l');
    EXPECT_EQ(message.inline_struct.string.data[3], 'l');
    EXPECT_EQ(message.inline_struct.string.data[4], 'o');
    EXPECT_EQ(message.inline_struct.string.data[5], '!');

    END_TEST;
}

bool decode_multiple_present_nullable_string() {
    BEGIN_TEST;

    // Among other things, this test ensures we handle out-of-line
    // alignment to FIDL_ALIGNMENT (i.e., 8) bytes correctly.
    multiple_nullable_strings_message_layout message = {};
    message.inline_struct.string = fidl_string_t{6, reinterpret_cast<char*>(FIDL_ALLOC_PRESENT)};
    message.inline_struct.string2 = fidl_string_t{8, reinterpret_cast<char*>(FIDL_ALLOC_PRESENT)};
    memcpy(message.data, "hello ", 6);
    memcpy(message.data2, "world!!! ", 8);

    const char* error = nullptr;
    auto status = fidl_decode(&multiple_nullable_strings_message_type, &message, sizeof(message),
                              nullptr, 0, &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);
    EXPECT_EQ(message.inline_struct.string.size, 6);
    EXPECT_EQ(message.inline_struct.string.data[0], 'h');
    EXPECT_EQ(message.inline_struct.string.data[1], 'e');
    EXPECT_EQ(message.inline_struct.string.data[2], 'l');
    EXPECT_EQ(message.inline_struct.string.data[3], 'l');
    EXPECT_EQ(message.inline_struct.string.data[4], 'o');
    EXPECT_EQ(message.inline_struct.string.data[5], ' ');
    EXPECT_EQ(message.inline_struct.string2.size, 8);
    EXPECT_EQ(message.inline_struct.string2.data[0], 'w');
    EXPECT_EQ(message.inline_struct.string2.data[1], 'o');
    EXPECT_EQ(message.inline_struct.string2.data[2], 'r');
    EXPECT_EQ(message.inline_struct.string2.data[3], 'l');
    EXPECT_EQ(message.inline_struct.string2.data[4], 'd');
    EXPECT_EQ(message.inline_struct.string2.data[5], '!');
    EXPECT_EQ(message.inline_struct.string2.data[6], '!');
    EXPECT_EQ(message.inline_struct.string2.data[7], '!');
    EXPECT_EQ(message.inline_struct.string2.data[7], '!');

    END_TEST;
}

bool decode_absent_nonnullable_string_error() {
    BEGIN_TEST;

    unbounded_nonnullable_string_message_layout message = {};
    message.inline_struct.string = fidl_string_t{6, reinterpret_cast<char*>(FIDL_ALLOC_ABSENT)};

    const char* error = nullptr;
    auto status = fidl_decode(&unbounded_nonnullable_string_message_type, &message, sizeof(message),
                              nullptr, 0, &error);

    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_NONNULL(error, error);

    END_TEST;
}

bool decode_absent_nullable_string() {
    BEGIN_TEST;

    unbounded_nullable_string_message_layout message = {};
    message.inline_struct.string = fidl_string_t{0, reinterpret_cast<char*>(FIDL_ALLOC_ABSENT)};

    const char* error = nullptr;
    auto status = fidl_decode(&unbounded_nullable_string_message_type, &message,
                              sizeof(message.inline_struct), nullptr, 0, &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);

    END_TEST;
}

bool decode_present_nonnullable_bounded_string() {
    BEGIN_TEST;

    bounded_32_nonnullable_string_message_layout message = {};
    message.inline_struct.string = fidl_string_t{6, reinterpret_cast<char*>(FIDL_ALLOC_PRESENT)};
    memcpy(message.data, "hello!", 6);

    const char* error = nullptr;
    auto status = fidl_decode(&bounded_32_nonnullable_string_message_type, &message,
                              sizeof(message), nullptr, 0, &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);
    EXPECT_EQ(message.inline_struct.string.size, 6);
    EXPECT_EQ(message.inline_struct.string.data[0], 'h');
    EXPECT_EQ(message.inline_struct.string.data[1], 'e');
    EXPECT_EQ(message.inline_struct.string.data[2], 'l');
    EXPECT_EQ(message.inline_struct.string.data[3], 'l');
    EXPECT_EQ(message.inline_struct.string.data[4], 'o');
    EXPECT_EQ(message.inline_struct.string.data[5], '!');

    END_TEST;
}

bool decode_present_nullable_bounded_string() {
    BEGIN_TEST;

    bounded_32_nullable_string_message_layout message = {};
    message.inline_struct.string = fidl_string_t{6, reinterpret_cast<char*>(FIDL_ALLOC_PRESENT)};
    memcpy(message.data, "hello!", 6);

    const char* error = nullptr;
    auto status = fidl_decode(&bounded_32_nullable_string_message_type, &message, sizeof(message),
                              nullptr, 0, &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);
    EXPECT_EQ(message.inline_struct.string.size, 6);
    EXPECT_EQ(message.inline_struct.string.data[0], 'h');
    EXPECT_EQ(message.inline_struct.string.data[1], 'e');
    EXPECT_EQ(message.inline_struct.string.data[2], 'l');
    EXPECT_EQ(message.inline_struct.string.data[3], 'l');
    EXPECT_EQ(message.inline_struct.string.data[4], 'o');
    EXPECT_EQ(message.inline_struct.string.data[5], '!');

    END_TEST;
}

bool decode_absent_nonnullable_bounded_string_error() {
    BEGIN_TEST;

    bounded_32_nonnullable_string_message_layout message = {};
    message.inline_struct.string = fidl_string_t{6, reinterpret_cast<char*>(FIDL_ALLOC_ABSENT)};

    const char* error = nullptr;
    auto status = fidl_decode(&bounded_32_nonnullable_string_message_type, &message,
                              sizeof(message), nullptr, 0, &error);

    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_NONNULL(error, error);

    END_TEST;
}

bool decode_absent_nullable_bounded_string() {
    BEGIN_TEST;

    bounded_32_nullable_string_message_layout message = {};
    message.inline_struct.string = fidl_string_t{0, reinterpret_cast<char*>(FIDL_ALLOC_ABSENT)};

    const char* error = nullptr;
    auto status = fidl_decode(&bounded_32_nullable_string_message_type, &message,
                              sizeof(message.inline_struct), nullptr, 0, &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);

    END_TEST;
}

bool decode_present_nonnullable_bounded_string_short_error() {
    BEGIN_TEST;

    multiple_short_nonnullable_strings_message_layout message = {};
    message.inline_struct.string = fidl_string_t{6, reinterpret_cast<char*>(FIDL_ALLOC_PRESENT)};
    message.inline_struct.string2 = fidl_string_t{8, reinterpret_cast<char*>(FIDL_ALLOC_PRESENT)};
    memcpy(message.data, "hello ", 6);
    memcpy(message.data2, "world! ", 6);

    const char* error = nullptr;
    auto status = fidl_decode(&multiple_short_nonnullable_strings_message_type, &message,
                              sizeof(message), nullptr, 0, &error);

    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_NONNULL(error);

    END_TEST;
}

bool decode_present_nullable_bounded_string_short_error() {
    BEGIN_TEST;

    multiple_short_nullable_strings_message_layout message = {};
    message.inline_struct.string = fidl_string_t{6, reinterpret_cast<char*>(FIDL_ALLOC_PRESENT)};
    message.inline_struct.string2 = fidl_string_t{8, reinterpret_cast<char*>(FIDL_ALLOC_PRESENT)};
    memcpy(message.data, "hello ", 6);
    memcpy(message.data2, "world! ", 6);

    const char* error = nullptr;
    auto status = fidl_decode(&multiple_short_nullable_strings_message_type, &message,
                              sizeof(message), nullptr, 0, &error);

    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_NONNULL(error);

    END_TEST;
}

bool decode_vector_with_huge_count() {
    BEGIN_TEST;

    unbounded_nonnullable_vector_of_uint32_message_layout message = {};
    // (2^30 + 4) * 4 (4 == sizeof(uint32_t)) overflows to 16 when stored as uint32_t.
    // We want 16 because it happens to be the actual size of the vector data in the message,
    // so we can trigger the overflow without triggering the "tried to claim too many bytes" or
    // "didn't use all the bytes in the message" errors.
    message.inline_struct.vector = fidl_vector_t{
        (1ull << 30) + 4, reinterpret_cast<void*>(FIDL_ALLOC_PRESENT)};

    const char* error = nullptr;
    auto status = fidl_decode(&unbounded_nonnullable_vector_of_uint32_message_type, &message,
                              sizeof(message), nullptr, 0, &error);

    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_NONNULL(error);
    const char expected_error_msg[] = "integer overflow calculating vector size";
    EXPECT_STR_EQ(expected_error_msg, error, "wrong error msg");

    auto message_uint32 = reinterpret_cast<zx_handle_t*>(message.inline_struct.vector.data);
    EXPECT_NONNULL(message_uint32);

    END_TEST;
}

bool decode_present_nonnullable_vector_of_handles() {
    BEGIN_TEST;

    unbounded_nonnullable_vector_of_handles_message_layout message = {};
    message.inline_struct.vector = fidl_vector_t{4, reinterpret_cast<void*>(FIDL_ALLOC_PRESENT)};
    message.handles[0] = FIDL_HANDLE_PRESENT;
    message.handles[1] = FIDL_HANDLE_PRESENT;
    message.handles[2] = FIDL_HANDLE_PRESENT;
    message.handles[3] = FIDL_HANDLE_PRESENT;

    zx_handle_t handles[] = {
        dummy_handle_0,
        dummy_handle_1,
        dummy_handle_2,
        dummy_handle_3,
    };

    const char* error = nullptr;
    auto status = fidl_decode(&unbounded_nonnullable_vector_of_handles_message_type, &message,
                              sizeof(message), handles, ArrayCount(handles), &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);

    auto message_handles = reinterpret_cast<zx_handle_t*>(message.inline_struct.vector.data);
    EXPECT_EQ(message_handles[0], dummy_handle_0);
    EXPECT_EQ(message_handles[1], dummy_handle_1);
    EXPECT_EQ(message_handles[2], dummy_handle_2);
    EXPECT_EQ(message_handles[3], dummy_handle_3);

    END_TEST;
}

bool decode_present_nullable_vector_of_handles() {
    BEGIN_TEST;

    unbounded_nullable_vector_of_handles_message_layout message = {};
    message.inline_struct.vector = fidl_vector_t{4, reinterpret_cast<void*>(FIDL_ALLOC_PRESENT)};
    message.handles[0] = FIDL_HANDLE_PRESENT;
    message.handles[1] = FIDL_HANDLE_PRESENT;
    message.handles[2] = FIDL_HANDLE_PRESENT;
    message.handles[3] = FIDL_HANDLE_PRESENT;

    zx_handle_t handles[] = {
        dummy_handle_0,
        dummy_handle_1,
        dummy_handle_2,
        dummy_handle_3,
    };

    const char* error = nullptr;
    auto status = fidl_decode(&unbounded_nullable_vector_of_handles_message_type, &message,
                              sizeof(message), handles, ArrayCount(handles), &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);

    auto message_handles = reinterpret_cast<zx_handle_t*>(message.inline_struct.vector.data);
    EXPECT_EQ(message_handles[0], dummy_handle_0);
    EXPECT_EQ(message_handles[1], dummy_handle_1);
    EXPECT_EQ(message_handles[2], dummy_handle_2);
    EXPECT_EQ(message_handles[3], dummy_handle_3);

    END_TEST;
}

bool decode_absent_nonnullable_vector_of_handles_error() {
    BEGIN_TEST;

    unbounded_nonnullable_vector_of_handles_message_layout message = {};
    message.inline_struct.vector = fidl_vector_t{4, reinterpret_cast<void*>(FIDL_ALLOC_ABSENT)};

    zx_handle_t handles[] = {
        dummy_handle_0,
        dummy_handle_1,
        dummy_handle_2,
        dummy_handle_3,
    };

    const char* error = nullptr;
    auto status = fidl_decode(&unbounded_nonnullable_vector_of_handles_message_type, &message,
                              sizeof(message), handles, ArrayCount(handles), &error);

    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_NONNULL(error, error);

    END_TEST;
}

bool decode_absent_nullable_vector_of_handles() {
    BEGIN_TEST;

    unbounded_nullable_vector_of_handles_message_layout message = {};
    message.inline_struct.vector = fidl_vector_t{0, reinterpret_cast<void*>(FIDL_ALLOC_ABSENT)};

    const char* error = nullptr;
    auto status = fidl_decode(&unbounded_nullable_vector_of_handles_message_type, &message,
                              sizeof(message.inline_struct), nullptr, 0u, &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);

    auto message_handles = reinterpret_cast<zx_handle_t*>(message.inline_struct.vector.data);
    EXPECT_NULL(message_handles);

    END_TEST;
}

bool decode_present_nonnullable_bounded_vector_of_handles() {
    BEGIN_TEST;

    bounded_32_nonnullable_vector_of_handles_message_layout message = {};
    message.inline_struct.vector = fidl_vector_t{4, reinterpret_cast<void*>(FIDL_ALLOC_PRESENT)};
    message.handles[0] = FIDL_HANDLE_PRESENT;
    message.handles[1] = FIDL_HANDLE_PRESENT;
    message.handles[2] = FIDL_HANDLE_PRESENT;
    message.handles[3] = FIDL_HANDLE_PRESENT;

    zx_handle_t handles[] = {
        dummy_handle_0,
        dummy_handle_1,
        dummy_handle_2,
        dummy_handle_3,
    };

    const char* error = nullptr;
    auto status = fidl_decode(&bounded_32_nonnullable_vector_of_handles_message_type, &message,
                              sizeof(message), handles, ArrayCount(handles), &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);

    auto message_handles = reinterpret_cast<zx_handle_t*>(message.inline_struct.vector.data);
    EXPECT_EQ(message_handles[0], dummy_handle_0);
    EXPECT_EQ(message_handles[1], dummy_handle_1);
    EXPECT_EQ(message_handles[2], dummy_handle_2);
    EXPECT_EQ(message_handles[3], dummy_handle_3);

    END_TEST;
}

bool decode_present_nullable_bounded_vector_of_handles() {
    BEGIN_TEST;

    bounded_32_nullable_vector_of_handles_message_layout message = {};
    message.inline_struct.vector = fidl_vector_t{4, reinterpret_cast<void*>(FIDL_ALLOC_PRESENT)};
    message.handles[0] = FIDL_HANDLE_PRESENT;
    message.handles[1] = FIDL_HANDLE_PRESENT;
    message.handles[2] = FIDL_HANDLE_PRESENT;
    message.handles[3] = FIDL_HANDLE_PRESENT;

    zx_handle_t handles[] = {
        dummy_handle_0,
        dummy_handle_1,
        dummy_handle_2,
        dummy_handle_3,
    };

    const char* error = nullptr;
    auto status = fidl_decode(&bounded_32_nullable_vector_of_handles_message_type, &message,
                              sizeof(message), handles, ArrayCount(handles), &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);

    auto message_handles = reinterpret_cast<zx_handle_t*>(message.inline_struct.vector.data);
    EXPECT_EQ(message_handles[0], dummy_handle_0);
    EXPECT_EQ(message_handles[1], dummy_handle_1);
    EXPECT_EQ(message_handles[2], dummy_handle_2);
    EXPECT_EQ(message_handles[3], dummy_handle_3);

    END_TEST;
}

bool decode_absent_nonnullable_bounded_vector_of_handles() {
    BEGIN_TEST;

    bounded_32_nonnullable_vector_of_handles_message_layout message = {};
    message.inline_struct.vector = fidl_vector_t{4, reinterpret_cast<void*>(FIDL_ALLOC_ABSENT)};

    const char* error = nullptr;
    auto status = fidl_decode(&bounded_32_nonnullable_vector_of_handles_message_type, &message,
                              sizeof(message.inline_struct), nullptr, 0u, &error);

    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_NONNULL(error);

    auto message_handles = reinterpret_cast<zx_handle_t*>(message.inline_struct.vector.data);
    EXPECT_NULL(message_handles);

    END_TEST;
}

bool decode_absent_nullable_bounded_vector_of_handles() {
    BEGIN_TEST;

    bounded_32_nullable_vector_of_handles_message_layout message = {};
    message.inline_struct.vector = fidl_vector_t{0, reinterpret_cast<void*>(FIDL_ALLOC_ABSENT)};

    const char* error = nullptr;
    auto status = fidl_decode(&bounded_32_nullable_vector_of_handles_message_type, &message,
                              sizeof(message.inline_struct), nullptr, 0u, &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);

    auto message_handles = reinterpret_cast<zx_handle_t*>(message.inline_struct.vector.data);
    EXPECT_NULL(message_handles);

    END_TEST;
}

bool decode_present_nonnullable_bounded_vector_of_handles_short_error() {
    BEGIN_TEST;

    multiple_nonnullable_vectors_of_handles_message_layout message = {};
    message.inline_struct.vector = fidl_vector_t{4, reinterpret_cast<void*>(FIDL_ALLOC_PRESENT)};
    message.inline_struct.vector2 = fidl_vector_t{4, reinterpret_cast<void*>(FIDL_ALLOC_PRESENT)};
    message.handles[0] = FIDL_HANDLE_PRESENT;
    message.handles[1] = FIDL_HANDLE_PRESENT;
    message.handles[2] = FIDL_HANDLE_PRESENT;
    message.handles[3] = FIDL_HANDLE_PRESENT;
    message.handles2[0] = FIDL_HANDLE_PRESENT;
    message.handles2[1] = FIDL_HANDLE_PRESENT;
    message.handles2[2] = FIDL_HANDLE_PRESENT;
    message.handles2[3] = FIDL_HANDLE_PRESENT;

    zx_handle_t handles[] = {
        dummy_handle_0, dummy_handle_1, dummy_handle_2, dummy_handle_3,
        dummy_handle_4, dummy_handle_5, dummy_handle_6, dummy_handle_7,
    };

    const char* error = nullptr;
    auto status = fidl_decode(&multiple_nonnullable_vectors_of_handles_message_type, &message,
                              sizeof(message), handles, ArrayCount(handles), &error);

    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_NONNULL(error);

    END_TEST;
}

bool decode_present_nullable_bounded_vector_of_handles_short_error() {
    BEGIN_TEST;

    multiple_nullable_vectors_of_handles_message_layout message = {};
    message.inline_struct.vector = fidl_vector_t{4, reinterpret_cast<void*>(FIDL_ALLOC_PRESENT)};
    message.inline_struct.vector2 = fidl_vector_t{4, reinterpret_cast<void*>(FIDL_ALLOC_PRESENT)};
    message.handles[0] = FIDL_HANDLE_PRESENT;
    message.handles[1] = FIDL_HANDLE_PRESENT;
    message.handles[2] = FIDL_HANDLE_PRESENT;
    message.handles[3] = FIDL_HANDLE_PRESENT;
    message.handles2[0] = FIDL_HANDLE_PRESENT;
    message.handles2[1] = FIDL_HANDLE_PRESENT;
    message.handles2[2] = FIDL_HANDLE_PRESENT;
    message.handles2[3] = FIDL_HANDLE_PRESENT;

    zx_handle_t handles[] = {
        dummy_handle_0, dummy_handle_1, dummy_handle_2, dummy_handle_3,
        dummy_handle_4, dummy_handle_5, dummy_handle_6, dummy_handle_7,
    };

    const char* error = nullptr;
    auto status = fidl_decode(&multiple_nullable_vectors_of_handles_message_type, &message,
                              sizeof(message), handles, ArrayCount(handles), &error);

    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_NONNULL(error);

    END_TEST;
}

bool decode_present_nonnullable_vector_of_uint32() {
    BEGIN_TEST;

    unbounded_nonnullable_vector_of_uint32_message_layout message = {};
    message.inline_struct.vector = fidl_vector_t{4, reinterpret_cast<void*>(FIDL_ALLOC_PRESENT)};

    const char* error = nullptr;
    auto status = fidl_decode(&unbounded_nonnullable_vector_of_uint32_message_type, &message,
                              sizeof(message), nullptr, 0, &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);

    auto message_uint32 = reinterpret_cast<zx_handle_t*>(message.inline_struct.vector.data);
    EXPECT_NONNULL(message_uint32);

    END_TEST;
}

bool decode_present_nullable_vector_of_uint32() {
    BEGIN_TEST;

    unbounded_nullable_vector_of_uint32_message_layout message = {};
    message.inline_struct.vector = fidl_vector_t{4, reinterpret_cast<void*>(FIDL_ALLOC_PRESENT)};

    const char* error = nullptr;
    auto status = fidl_decode(&unbounded_nullable_vector_of_uint32_message_type, &message,
                              sizeof(message), nullptr, 0, &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);

    auto message_uint32 = reinterpret_cast<zx_handle_t*>(message.inline_struct.vector.data);
    EXPECT_NONNULL(message_uint32);

    END_TEST;
}

bool decode_absent_nonnullable_vector_of_uint32_error() {
    BEGIN_TEST;

    unbounded_nonnullable_vector_of_uint32_message_layout message = {};
    message.inline_struct.vector = fidl_vector_t{4, reinterpret_cast<void*>(FIDL_ALLOC_ABSENT)};

    const char* error = nullptr;
    auto status = fidl_decode(&unbounded_nonnullable_vector_of_uint32_message_type, &message,
                              sizeof(message), nullptr, 0, &error);

    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_NONNULL(error, error);

    END_TEST;
}

bool decode_absent_nullable_vector_of_uint32() {
    BEGIN_TEST;

    unbounded_nullable_vector_of_uint32_message_layout message = {};
    message.inline_struct.vector = fidl_vector_t{0, reinterpret_cast<void*>(FIDL_ALLOC_ABSENT)};

    const char* error = nullptr;
    auto status = fidl_decode(&unbounded_nullable_vector_of_uint32_message_type, &message,
                              sizeof(message.inline_struct), nullptr, 0u, &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);

    auto message_uint32 = reinterpret_cast<zx_handle_t*>(message.inline_struct.vector.data);
    EXPECT_NULL(message_uint32);

    END_TEST;
}

bool decode_present_nonnullable_bounded_vector_of_uint32() {
    BEGIN_TEST;

    bounded_32_nonnullable_vector_of_uint32_message_layout message = {};
    message.inline_struct.vector = fidl_vector_t{4, reinterpret_cast<void*>(FIDL_ALLOC_PRESENT)};

    const char* error = nullptr;
    auto status = fidl_decode(&bounded_32_nonnullable_vector_of_uint32_message_type, &message,
                              sizeof(message), nullptr, 0, &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);

    auto message_uint32 = reinterpret_cast<zx_handle_t*>(message.inline_struct.vector.data);
    EXPECT_NONNULL(message_uint32);

    END_TEST;
}

bool decode_present_nullable_bounded_vector_of_uint32() {
    BEGIN_TEST;

    bounded_32_nullable_vector_of_uint32_message_layout message = {};
    message.inline_struct.vector = fidl_vector_t{4, reinterpret_cast<void*>(FIDL_ALLOC_PRESENT)};

    const char* error = nullptr;
    auto status = fidl_decode(&bounded_32_nullable_vector_of_uint32_message_type, &message,
                              sizeof(message), nullptr, 0, &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);

    auto message_uint32 = reinterpret_cast<zx_handle_t*>(message.inline_struct.vector.data);
    EXPECT_NONNULL(message_uint32);

    END_TEST;
}

bool decode_absent_nonnullable_bounded_vector_of_uint32() {
    BEGIN_TEST;

    bounded_32_nonnullable_vector_of_uint32_message_layout message = {};
    message.inline_struct.vector = fidl_vector_t{4, reinterpret_cast<void*>(FIDL_ALLOC_ABSENT)};

    const char* error = nullptr;
    auto status = fidl_decode(&bounded_32_nonnullable_vector_of_uint32_message_type, &message,
                              sizeof(message.inline_struct), nullptr, 0u, &error);

    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_NONNULL(error);

    auto message_uint32 = reinterpret_cast<zx_handle_t*>(message.inline_struct.vector.data);
    EXPECT_NULL(message_uint32);

    END_TEST;
}

bool decode_absent_nullable_bounded_vector_of_uint32() {
    BEGIN_TEST;

    bounded_32_nullable_vector_of_uint32_message_layout message = {};
    message.inline_struct.vector = fidl_vector_t{0, reinterpret_cast<void*>(FIDL_ALLOC_ABSENT)};

    const char* error = nullptr;
    auto status = fidl_decode(&bounded_32_nullable_vector_of_uint32_message_type, &message,
                              sizeof(message.inline_struct), nullptr, 0u, &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);

    auto message_uint32 = reinterpret_cast<zx_handle_t*>(message.inline_struct.vector.data);
    EXPECT_NULL(message_uint32);

    END_TEST;
}

bool decode_present_nonnullable_bounded_vector_of_uint32_short_error() {
    BEGIN_TEST;

    multiple_nonnullable_vectors_of_uint32_message_layout message = {};
    message.inline_struct.vector = fidl_vector_t{4, reinterpret_cast<void*>(FIDL_ALLOC_PRESENT)};
    message.inline_struct.vector2 = fidl_vector_t{4, reinterpret_cast<void*>(FIDL_ALLOC_PRESENT)};

    const char* error = nullptr;
    auto status = fidl_decode(&multiple_nonnullable_vectors_of_uint32_message_type, &message,
                              sizeof(message), nullptr, 0, &error);

    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_NONNULL(error);

    END_TEST;
}

bool decode_present_nullable_bounded_vector_of_uint32_short_error() {
    BEGIN_TEST;

    multiple_nullable_vectors_of_uint32_message_layout message = {};
    message.inline_struct.vector = fidl_vector_t{4, reinterpret_cast<void*>(FIDL_ALLOC_PRESENT)};
    message.inline_struct.vector2 = fidl_vector_t{4, reinterpret_cast<void*>(FIDL_ALLOC_PRESENT)};

    const char* error = nullptr;
    auto status = fidl_decode(&multiple_nullable_vectors_of_uint32_message_type, &message,
                              sizeof(message), nullptr, 0, &error);

    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_NONNULL(error);

    END_TEST;
}

bool decode_bad_tagged_union_error() {
    BEGIN_TEST;

    nonnullable_handle_union_message_layout message = {};
    message.inline_struct.data.tag = 43u;
    message.inline_struct.data.handle = FIDL_HANDLE_PRESENT;

    zx_handle_t handles[] = {
        dummy_handle_0,
    };

    const char* error = nullptr;
    auto status = fidl_decode(&nonnullable_handle_union_message_type, &message, sizeof(message),
                              handles, ArrayCount(handles), &error);

    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_NONNULL(error);

    END_TEST;
}

bool decode_single_membered_present_nonnullable_union() {
    BEGIN_TEST;

    nonnullable_handle_union_message_layout message = {};
    message.inline_struct.data.tag = nonnullable_handle_union_kHandle;
    message.inline_struct.data.handle = FIDL_HANDLE_PRESENT;

    zx_handle_t handles[] = {
        dummy_handle_0,
    };

    const char* error = nullptr;
    auto status = fidl_decode(&nonnullable_handle_union_message_type, &message, sizeof(message),
                              handles, ArrayCount(handles), &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);
    EXPECT_EQ(message.inline_struct.data.tag, nonnullable_handle_union_kHandle);
    EXPECT_EQ(message.inline_struct.data.handle, dummy_handle_0);

    END_TEST;
}

bool decode_many_membered_present_nonnullable_union() {
    BEGIN_TEST;

    array_of_nonnullable_handles_union_message_layout message = {};
    message.inline_struct.data.tag = array_of_nonnullable_handles_union_kArrayOfArrayOfHandles;
    message.inline_struct.data.array_of_array_of_handles[0][0] = FIDL_HANDLE_PRESENT;
    message.inline_struct.data.array_of_array_of_handles[0][1] = FIDL_HANDLE_PRESENT;
    message.inline_struct.data.array_of_array_of_handles[1][0] = FIDL_HANDLE_PRESENT;
    message.inline_struct.data.array_of_array_of_handles[1][1] = FIDL_HANDLE_PRESENT;

    zx_handle_t handles[] = {
        dummy_handle_0,
        dummy_handle_1,
        dummy_handle_2,
        dummy_handle_3,
    };

    const char* error = nullptr;
    auto status = fidl_decode(&array_of_nonnullable_handles_union_message_type, &message,
                              sizeof(message), handles, ArrayCount(handles), &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);
    EXPECT_EQ(message.inline_struct.data.tag,
              array_of_nonnullable_handles_union_kArrayOfArrayOfHandles);
    EXPECT_EQ(message.inline_struct.data.array_of_array_of_handles[0][0], dummy_handle_0);
    EXPECT_EQ(message.inline_struct.data.array_of_array_of_handles[0][1], dummy_handle_1);
    EXPECT_EQ(message.inline_struct.data.array_of_array_of_handles[1][0], dummy_handle_2);
    EXPECT_EQ(message.inline_struct.data.array_of_array_of_handles[1][1], dummy_handle_3);

    END_TEST;
}

bool decode_single_membered_present_nullable_union() {
    BEGIN_TEST;

    nonnullable_handle_union_ptr_message_layout message = {};
    message.inline_struct.data = reinterpret_cast<nonnullable_handle_union*>(FIDL_ALLOC_PRESENT);
    message.data.tag = nonnullable_handle_union_kHandle;
    message.data.handle = FIDL_HANDLE_PRESENT;

    zx_handle_t handles[] = {
        dummy_handle_0,
    };

    const char* error = nullptr;
    auto status = fidl_decode(&nonnullable_handle_union_ptr_message_type, &message, sizeof(message),
                              handles, ArrayCount(handles), &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);
    EXPECT_EQ(message.inline_struct.data, &message.data);
    EXPECT_EQ(message.inline_struct.data->tag, nonnullable_handle_union_kHandle);
    EXPECT_EQ(message.inline_struct.data->handle, dummy_handle_0);

    END_TEST;
}

bool decode_many_membered_present_nullable_union() {
    BEGIN_TEST;

    array_of_nonnullable_handles_union_ptr_message_layout message = {};
    message.inline_struct.data =
        reinterpret_cast<array_of_nonnullable_handles_union*>(FIDL_ALLOC_PRESENT);
    message.data.tag = array_of_nonnullable_handles_union_kArrayOfArrayOfHandles;
    message.data.array_of_array_of_handles[0][0] = FIDL_HANDLE_PRESENT;
    message.data.array_of_array_of_handles[0][1] = FIDL_HANDLE_PRESENT;
    message.data.array_of_array_of_handles[1][0] = FIDL_HANDLE_PRESENT;
    message.data.array_of_array_of_handles[1][1] = FIDL_HANDLE_PRESENT;

    zx_handle_t handles[] = {
        dummy_handle_0,
        dummy_handle_1,
        dummy_handle_2,
        dummy_handle_3,
    };

    const char* error = nullptr;
    auto status = fidl_decode(&array_of_nonnullable_handles_union_ptr_message_type, &message,
                              sizeof(message), handles, ArrayCount(handles), &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);
    EXPECT_EQ(message.inline_struct.data, &message.data);
    EXPECT_EQ(message.inline_struct.data->tag,
              array_of_nonnullable_handles_union_kArrayOfArrayOfHandles);
    EXPECT_EQ(message.inline_struct.data->array_of_array_of_handles[0][0], dummy_handle_0);
    EXPECT_EQ(message.inline_struct.data->array_of_array_of_handles[0][1], dummy_handle_1);
    EXPECT_EQ(message.inline_struct.data->array_of_array_of_handles[1][0], dummy_handle_2);
    EXPECT_EQ(message.inline_struct.data->array_of_array_of_handles[1][1], dummy_handle_3);

    END_TEST;
}

bool decode_single_membered_absent_nullable_union() {
    BEGIN_TEST;

    nonnullable_handle_union_ptr_message_layout message = {};
    message.inline_struct.data = reinterpret_cast<nonnullable_handle_union*>(FIDL_ALLOC_ABSENT);

    const char* error = nullptr;
    auto status = fidl_decode(&nonnullable_handle_union_ptr_message_type, &message,
                              sizeof(message.inline_struct), nullptr, 0u, &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);
    EXPECT_NULL(message.inline_struct.data);

    END_TEST;
}

bool decode_many_membered_absent_nullable_union() {
    BEGIN_TEST;

    array_of_nonnullable_handles_union_ptr_message_layout message = {};
    message.inline_struct.data =
        reinterpret_cast<array_of_nonnullable_handles_union*>(FIDL_ALLOC_ABSENT);

    const char* error = nullptr;
    auto status = fidl_decode(&array_of_nonnullable_handles_union_ptr_message_type, &message,
                              sizeof(message.inline_struct), nullptr, 0u, &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);
    EXPECT_NULL(message.inline_struct.data);

    END_TEST;
}

bool decode_nested_nonnullable_structs() {
    BEGIN_TEST;

    nested_structs_message_layout message = {};
    message.inline_struct.l0.handle_0 = FIDL_HANDLE_PRESENT;
    message.inline_struct.l0.l1.handle_1 = FIDL_HANDLE_PRESENT;
    message.inline_struct.l0.l1.l2.handle_2 = FIDL_HANDLE_PRESENT;
    message.inline_struct.l0.l1.l2.l3.handle_3 = FIDL_HANDLE_PRESENT;

    zx_handle_t handles[] = {
        dummy_handle_0,
        dummy_handle_1,
        dummy_handle_2,
        dummy_handle_3,
    };

    const char* error = nullptr;
    auto status = fidl_decode(&nested_structs_message_type, &message, sizeof(message), handles,
                              ArrayCount(handles), &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);
    // Note the traversal order! l1 -> l3 -> l2 -> l0
    EXPECT_EQ(message.inline_struct.l0.l1.handle_1, dummy_handle_0);
    EXPECT_EQ(message.inline_struct.l0.l1.l2.l3.handle_3, dummy_handle_1);
    EXPECT_EQ(message.inline_struct.l0.l1.l2.handle_2, dummy_handle_2);
    EXPECT_EQ(message.inline_struct.l0.handle_0, dummy_handle_3);

    END_TEST;
}

bool decode_nested_nullable_structs() {
    BEGIN_TEST;

    // See below for the handle traversal order.
    nested_struct_ptrs_message_layout message = {};

    message.inline_struct.l0_present = reinterpret_cast<struct_ptr_level_0*>(FIDL_ALLOC_PRESENT);
    message.inline_struct.l0_inline.l1_present =
        reinterpret_cast<struct_ptr_level_1*>(FIDL_ALLOC_PRESENT);
    message.inline_struct.l0_inline.l1_inline.l2_present =
        reinterpret_cast<struct_ptr_level_2*>(FIDL_ALLOC_PRESENT);
    message.inline_struct.l0_inline.l1_inline.l2_inline.l3_present =
        reinterpret_cast<struct_ptr_level_3*>(FIDL_ALLOC_PRESENT);
    message.in_in_out_2.l3_present = reinterpret_cast<struct_ptr_level_3*>(FIDL_ALLOC_PRESENT);
    message.in_out_1.l2_present = reinterpret_cast<struct_ptr_level_2*>(FIDL_ALLOC_PRESENT);
    message.in_out_1.l2_inline.l3_present =
        reinterpret_cast<struct_ptr_level_3*>(FIDL_ALLOC_PRESENT);
    message.in_out_out_2.l3_present = reinterpret_cast<struct_ptr_level_3*>(FIDL_ALLOC_PRESENT);
    message.out_0.l1_present = reinterpret_cast<struct_ptr_level_1*>(FIDL_ALLOC_PRESENT);
    message.out_0.l1_inline.l2_present = reinterpret_cast<struct_ptr_level_2*>(FIDL_ALLOC_PRESENT);
    message.out_0.l1_inline.l2_inline.l3_present =
        reinterpret_cast<struct_ptr_level_3*>(FIDL_ALLOC_PRESENT);
    message.out_in_out_2.l3_present = reinterpret_cast<struct_ptr_level_3*>(FIDL_ALLOC_PRESENT);
    message.out_out_1.l2_present = reinterpret_cast<struct_ptr_level_2*>(FIDL_ALLOC_PRESENT);
    message.out_out_1.l2_inline.l3_present =
        reinterpret_cast<struct_ptr_level_3*>(FIDL_ALLOC_PRESENT);
    message.out_out_out_2.l3_present = reinterpret_cast<struct_ptr_level_3*>(FIDL_ALLOC_PRESENT);

    message.inline_struct.l0_absent = reinterpret_cast<struct_ptr_level_0*>(FIDL_ALLOC_ABSENT);
    message.inline_struct.l0_inline.l1_absent =
        reinterpret_cast<struct_ptr_level_1*>(FIDL_ALLOC_ABSENT);
    message.inline_struct.l0_inline.l1_inline.l2_absent =
        reinterpret_cast<struct_ptr_level_2*>(FIDL_ALLOC_ABSENT);
    message.inline_struct.l0_inline.l1_inline.l2_inline.l3_absent =
        reinterpret_cast<struct_ptr_level_3*>(FIDL_ALLOC_ABSENT);
    message.in_in_out_2.l3_absent = reinterpret_cast<struct_ptr_level_3*>(FIDL_ALLOC_ABSENT);
    message.in_out_1.l2_absent = reinterpret_cast<struct_ptr_level_2*>(FIDL_ALLOC_ABSENT);
    message.in_out_1.l2_inline.l3_absent = reinterpret_cast<struct_ptr_level_3*>(FIDL_ALLOC_ABSENT);
    message.in_out_out_2.l3_absent = reinterpret_cast<struct_ptr_level_3*>(FIDL_ALLOC_ABSENT);
    message.out_0.l1_absent = reinterpret_cast<struct_ptr_level_1*>(FIDL_ALLOC_ABSENT);
    message.out_0.l1_inline.l2_absent = reinterpret_cast<struct_ptr_level_2*>(FIDL_ALLOC_ABSENT);
    message.out_0.l1_inline.l2_inline.l3_absent =
        reinterpret_cast<struct_ptr_level_3*>(FIDL_ALLOC_ABSENT);
    message.out_in_out_2.l3_absent = reinterpret_cast<struct_ptr_level_3*>(FIDL_ALLOC_ABSENT);
    message.out_out_1.l2_absent = reinterpret_cast<struct_ptr_level_2*>(FIDL_ALLOC_ABSENT);
    message.out_out_1.l2_inline.l3_absent =
        reinterpret_cast<struct_ptr_level_3*>(FIDL_ALLOC_ABSENT);
    message.out_out_out_2.l3_absent = reinterpret_cast<struct_ptr_level_3*>(FIDL_ALLOC_ABSENT);

    message.inline_struct.l0_inline.l1_inline.handle_1 = FIDL_HANDLE_PRESENT;
    message.in_in_out_out_3.handle_3 = FIDL_HANDLE_PRESENT;
    message.in_in_out_2.l3_inline.handle_3 = FIDL_HANDLE_PRESENT;
    message.in_in_out_2.handle_2 = FIDL_HANDLE_PRESENT;
    message.in_in_in_out_3.handle_3 = FIDL_HANDLE_PRESENT;
    message.inline_struct.l0_inline.l1_inline.l2_inline.l3_inline.handle_3 = FIDL_HANDLE_PRESENT;
    message.inline_struct.l0_inline.l1_inline.l2_inline.handle_2 = FIDL_HANDLE_PRESENT;
    message.inline_struct.l0_inline.handle_0 = FIDL_HANDLE_PRESENT;
    message.in_out_1.handle_1 = FIDL_HANDLE_PRESENT;
    message.in_out_out_out_3.handle_3 = FIDL_HANDLE_PRESENT;
    message.in_out_out_2.l3_inline.handle_3 = FIDL_HANDLE_PRESENT;
    message.in_out_out_2.handle_2 = FIDL_HANDLE_PRESENT;
    message.in_out_in_out_3.handle_3 = FIDL_HANDLE_PRESENT;
    message.in_out_1.l2_inline.l3_inline.handle_3 = FIDL_HANDLE_PRESENT;
    message.in_out_1.l2_inline.handle_2 = FIDL_HANDLE_PRESENT;
    message.out_0.l1_inline.handle_1 = FIDL_HANDLE_PRESENT;
    message.out_in_out_out_3.handle_3 = FIDL_HANDLE_PRESENT;
    message.out_in_out_2.l3_inline.handle_3 = FIDL_HANDLE_PRESENT;
    message.out_in_out_2.handle_2 = FIDL_HANDLE_PRESENT;
    message.out_in_in_out_3.handle_3 = FIDL_HANDLE_PRESENT;
    message.out_0.l1_inline.l2_inline.l3_inline.handle_3 = FIDL_HANDLE_PRESENT;
    message.out_0.l1_inline.l2_inline.handle_2 = FIDL_HANDLE_PRESENT;
    message.out_0.handle_0 = FIDL_HANDLE_PRESENT;
    message.out_out_1.handle_1 = FIDL_HANDLE_PRESENT;
    message.out_out_out_out_3.handle_3 = FIDL_HANDLE_PRESENT;
    message.out_out_out_2.l3_inline.handle_3 = FIDL_HANDLE_PRESENT;
    message.out_out_out_2.handle_2 = FIDL_HANDLE_PRESENT;
    message.out_out_in_out_3.handle_3 = FIDL_HANDLE_PRESENT;
    message.out_out_1.l2_inline.l3_inline.handle_3 = FIDL_HANDLE_PRESENT;
    message.out_out_1.l2_inline.handle_2 = FIDL_HANDLE_PRESENT;

    zx_handle_t handles[] = {
        dummy_handle_0,  dummy_handle_1,  dummy_handle_2,  dummy_handle_3,  dummy_handle_4,
        dummy_handle_5,  dummy_handle_6,  dummy_handle_7,  dummy_handle_8,  dummy_handle_9,
        dummy_handle_10, dummy_handle_11, dummy_handle_12, dummy_handle_13, dummy_handle_14,
        dummy_handle_15, dummy_handle_16, dummy_handle_17, dummy_handle_18, dummy_handle_19,
        dummy_handle_20, dummy_handle_21, dummy_handle_22, dummy_handle_23, dummy_handle_24,
        dummy_handle_25, dummy_handle_26, dummy_handle_27, dummy_handle_28, dummy_handle_29,
    };

    const char* error = nullptr;
    auto status = fidl_decode(&nested_struct_ptrs_message_type, &message, sizeof(message), handles,
                              ArrayCount(handles), &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);

    // Note the traversal order!

    // 0 inline
    //     1 inline
    //         handle
    EXPECT_EQ(message.inline_struct.l0_inline.l1_inline.handle_1, dummy_handle_0);
    //         2 out of line
    //             3 out of line
    EXPECT_EQ(message.inline_struct.l0_inline.l1_inline.l2_present->l3_present->handle_3,
              dummy_handle_1);
    //             3 inline
    EXPECT_EQ(message.inline_struct.l0_inline.l1_inline.l2_present->l3_inline.handle_3,
              dummy_handle_2);
    //             handle
    EXPECT_EQ(message.inline_struct.l0_inline.l1_inline.l2_present->handle_2, dummy_handle_3);
    //         2 inline
    //             3 out of line
    EXPECT_EQ(message.inline_struct.l0_inline.l1_inline.l2_inline.l3_present->handle_3,
              dummy_handle_4);
    //             3 inline
    EXPECT_EQ(message.inline_struct.l0_inline.l1_inline.l2_inline.l3_inline.handle_3,
              dummy_handle_5);
    //             handle
    EXPECT_EQ(message.inline_struct.l0_inline.l1_inline.l2_inline.handle_2, dummy_handle_6);
    //     handle
    EXPECT_EQ(message.inline_struct.l0_inline.handle_0, dummy_handle_7);
    //     1 out of line
    //         handle
    EXPECT_EQ(message.inline_struct.l0_inline.l1_present->handle_1, dummy_handle_8);
    //         2 out of line
    //             3 out of line
    EXPECT_EQ(message.inline_struct.l0_inline.l1_present->l2_present->l3_present->handle_3,
              dummy_handle_9);
    //             3 inline
    EXPECT_EQ(message.inline_struct.l0_inline.l1_present->l2_present->l3_inline.handle_3,
              dummy_handle_10);
    //             handle
    EXPECT_EQ(message.inline_struct.l0_inline.l1_present->l2_present->handle_2, dummy_handle_11);
    //         2 inline
    //             3 out of line
    EXPECT_EQ(message.inline_struct.l0_inline.l1_present->l2_inline.l3_present->handle_3,
              dummy_handle_12);
    //             3 inline
    EXPECT_EQ(message.inline_struct.l0_inline.l1_present->l2_inline.l3_inline.handle_3,
              dummy_handle_13);
    //             handle
    EXPECT_EQ(message.inline_struct.l0_inline.l1_present->l2_inline.handle_2, dummy_handle_14);
    // 0 out of line
    //     1 inline
    //         handle
    EXPECT_EQ(message.inline_struct.l0_present->l1_inline.handle_1, dummy_handle_15);
    //         2 out of line
    //             3 out of line
    EXPECT_EQ(message.inline_struct.l0_present->l1_inline.l2_present->l3_present->handle_3,
              dummy_handle_16);
    //             3 inline
    EXPECT_EQ(message.inline_struct.l0_present->l1_inline.l2_present->l3_inline.handle_3,
              dummy_handle_17);
    //             handle
    EXPECT_EQ(message.inline_struct.l0_present->l1_inline.l2_present->handle_2, dummy_handle_18);
    //         2 inline
    //             3 out of line
    EXPECT_EQ(message.inline_struct.l0_present->l1_inline.l2_inline.l3_present->handle_3,
              dummy_handle_19);
    //             3 inline
    EXPECT_EQ(message.inline_struct.l0_present->l1_inline.l2_inline.l3_inline.handle_3,
              dummy_handle_20);
    //             handle
    EXPECT_EQ(message.inline_struct.l0_present->l1_inline.l2_inline.handle_2, dummy_handle_21);
    //     handle
    EXPECT_EQ(message.inline_struct.l0_present->handle_0, dummy_handle_22);
    //     1 out of line
    //         handle
    EXPECT_EQ(message.inline_struct.l0_present->l1_present->handle_1, dummy_handle_23);
    //         2 out of line
    //             3 out of line
    EXPECT_EQ(message.inline_struct.l0_present->l1_present->l2_present->l3_present->handle_3,
              dummy_handle_24);
    //             3 inline
    EXPECT_EQ(message.inline_struct.l0_present->l1_present->l2_present->l3_inline.handle_3,
              dummy_handle_25);
    //             handle
    EXPECT_EQ(message.inline_struct.l0_present->l1_present->l2_present->handle_2, dummy_handle_26);
    //         2 inline
    //             3 out of line
    EXPECT_EQ(message.inline_struct.l0_present->l1_present->l2_inline.l3_present->handle_3,
              dummy_handle_27);
    //             3 inline
    EXPECT_EQ(message.inline_struct.l0_present->l1_present->l2_inline.l3_inline.handle_3,
              dummy_handle_28);
    //             handle
    EXPECT_EQ(message.inline_struct.l0_present->l1_present->l2_inline.handle_2, dummy_handle_29);

    // Finally, check that all absent members are nullptr.
    EXPECT_NULL(message.inline_struct.l0_absent);
    EXPECT_NULL(message.inline_struct.l0_inline.l1_absent);
    EXPECT_NULL(message.inline_struct.l0_inline.l1_inline.l2_absent);
    EXPECT_NULL(message.inline_struct.l0_inline.l1_inline.l2_inline.l3_absent);
    EXPECT_NULL(message.inline_struct.l0_inline.l1_inline.l2_present->l3_absent);
    EXPECT_NULL(message.inline_struct.l0_inline.l1_present->l2_absent);
    EXPECT_NULL(message.inline_struct.l0_inline.l1_present->l2_inline.l3_absent);
    EXPECT_NULL(message.inline_struct.l0_inline.l1_present->l2_present->l3_absent);
    EXPECT_NULL(message.inline_struct.l0_present->l1_absent);
    EXPECT_NULL(message.inline_struct.l0_present->l1_inline.l2_absent);
    EXPECT_NULL(message.inline_struct.l0_present->l1_inline.l2_inline.l3_absent);
    EXPECT_NULL(message.inline_struct.l0_present->l1_inline.l2_present->l3_absent);
    EXPECT_NULL(message.inline_struct.l0_present->l1_present->l2_absent);
    EXPECT_NULL(message.inline_struct.l0_present->l1_present->l2_inline.l3_absent);
    EXPECT_NULL(message.inline_struct.l0_present->l1_present->l2_present->l3_absent);

    END_TEST;
}

void SetUpRecursionMessage(recursion_message_layout* message) {
    message->inline_struct.inline_union.tag = maybe_recurse_union_kMore;
    message->inline_struct.inline_union.more =
        reinterpret_cast<recursion_inline_data*>(FIDL_ALLOC_PRESENT);
    message->depth_0.inline_union.tag = maybe_recurse_union_kMore;
    message->depth_0.inline_union.more =
        reinterpret_cast<recursion_inline_data*>(FIDL_ALLOC_PRESENT);
    message->depth_1.inline_union.tag = maybe_recurse_union_kMore;
    message->depth_1.inline_union.more =
        reinterpret_cast<recursion_inline_data*>(FIDL_ALLOC_PRESENT);
    message->depth_2.inline_union.tag = maybe_recurse_union_kMore;
    message->depth_2.inline_union.more =
        reinterpret_cast<recursion_inline_data*>(FIDL_ALLOC_PRESENT);
    message->depth_3.inline_union.tag = maybe_recurse_union_kMore;
    message->depth_3.inline_union.more =
        reinterpret_cast<recursion_inline_data*>(FIDL_ALLOC_PRESENT);
    message->depth_4.inline_union.tag = maybe_recurse_union_kMore;
    message->depth_4.inline_union.more =
        reinterpret_cast<recursion_inline_data*>(FIDL_ALLOC_PRESENT);
    message->depth_5.inline_union.tag = maybe_recurse_union_kMore;
    message->depth_5.inline_union.more =
        reinterpret_cast<recursion_inline_data*>(FIDL_ALLOC_PRESENT);
    message->depth_6.inline_union.tag = maybe_recurse_union_kMore;
    message->depth_6.inline_union.more =
        reinterpret_cast<recursion_inline_data*>(FIDL_ALLOC_PRESENT);
    message->depth_7.inline_union.tag = maybe_recurse_union_kMore;
    message->depth_7.inline_union.more =
        reinterpret_cast<recursion_inline_data*>(FIDL_ALLOC_PRESENT);
    message->depth_8.inline_union.tag = maybe_recurse_union_kMore;
    message->depth_8.inline_union.more =
        reinterpret_cast<recursion_inline_data*>(FIDL_ALLOC_PRESENT);
    message->depth_9.inline_union.tag = maybe_recurse_union_kMore;
    message->depth_9.inline_union.more =
        reinterpret_cast<recursion_inline_data*>(FIDL_ALLOC_PRESENT);
    message->depth_10.inline_union.tag = maybe_recurse_union_kMore;
    message->depth_10.inline_union.more =
        reinterpret_cast<recursion_inline_data*>(FIDL_ALLOC_PRESENT);
    message->depth_11.inline_union.tag = maybe_recurse_union_kMore;
    message->depth_11.inline_union.more =
        reinterpret_cast<recursion_inline_data*>(FIDL_ALLOC_PRESENT);
    message->depth_12.inline_union.tag = maybe_recurse_union_kMore;
    message->depth_12.inline_union.more =
        reinterpret_cast<recursion_inline_data*>(FIDL_ALLOC_PRESENT);
    message->depth_13.inline_union.tag = maybe_recurse_union_kMore;
    message->depth_13.inline_union.more =
        reinterpret_cast<recursion_inline_data*>(FIDL_ALLOC_PRESENT);
    message->depth_14.inline_union.tag = maybe_recurse_union_kMore;
    message->depth_14.inline_union.more =
        reinterpret_cast<recursion_inline_data*>(FIDL_ALLOC_PRESENT);
    message->depth_15.inline_union.tag = maybe_recurse_union_kMore;
    message->depth_15.inline_union.more =
        reinterpret_cast<recursion_inline_data*>(FIDL_ALLOC_PRESENT);
    message->depth_16.inline_union.tag = maybe_recurse_union_kMore;
    message->depth_16.inline_union.more =
        reinterpret_cast<recursion_inline_data*>(FIDL_ALLOC_PRESENT);
    message->depth_17.inline_union.tag = maybe_recurse_union_kMore;
    message->depth_17.inline_union.more =
        reinterpret_cast<recursion_inline_data*>(FIDL_ALLOC_PRESENT);
    message->depth_18.inline_union.tag = maybe_recurse_union_kMore;
    message->depth_18.inline_union.more =
        reinterpret_cast<recursion_inline_data*>(FIDL_ALLOC_PRESENT);
    message->depth_19.inline_union.tag = maybe_recurse_union_kMore;
    message->depth_19.inline_union.more =
        reinterpret_cast<recursion_inline_data*>(FIDL_ALLOC_PRESENT);
    message->depth_20.inline_union.tag = maybe_recurse_union_kMore;
    message->depth_20.inline_union.more =
        reinterpret_cast<recursion_inline_data*>(FIDL_ALLOC_PRESENT);
    message->depth_21.inline_union.tag = maybe_recurse_union_kMore;
    message->depth_21.inline_union.more =
        reinterpret_cast<recursion_inline_data*>(FIDL_ALLOC_PRESENT);
    message->depth_22.inline_union.tag = maybe_recurse_union_kMore;
    message->depth_22.inline_union.more =
        reinterpret_cast<recursion_inline_data*>(FIDL_ALLOC_PRESENT);
    message->depth_23.inline_union.tag = maybe_recurse_union_kMore;
    message->depth_23.inline_union.more =
        reinterpret_cast<recursion_inline_data*>(FIDL_ALLOC_PRESENT);
    message->depth_24.inline_union.tag = maybe_recurse_union_kMore;
    message->depth_24.inline_union.more =
        reinterpret_cast<recursion_inline_data*>(FIDL_ALLOC_PRESENT);
    message->depth_25.inline_union.tag = maybe_recurse_union_kMore;
    message->depth_25.inline_union.more =
        reinterpret_cast<recursion_inline_data*>(FIDL_ALLOC_PRESENT);
    message->depth_26.inline_union.tag = maybe_recurse_union_kMore;
    message->depth_26.inline_union.more =
        reinterpret_cast<recursion_inline_data*>(FIDL_ALLOC_PRESENT);
    message->depth_27.inline_union.tag = maybe_recurse_union_kMore;
    message->depth_27.inline_union.more =
        reinterpret_cast<recursion_inline_data*>(FIDL_ALLOC_PRESENT);
}

bool decode_nested_struct_recursion_too_deep_error() {
    BEGIN_TEST;

    recursion_message_layout message = {};

    // First we check that FIDL_RECURSION_DEPTH - 1 levels of recursion is OK.
    SetUpRecursionMessage(&message);
    message.depth_28.inline_union.tag = maybe_recurse_union_kDone;
    message.depth_28.inline_union.handle = FIDL_HANDLE_PRESENT;

    zx_handle_t handles[] = {
        dummy_handle_0,
    };


    const char* error = nullptr;
    auto status = fidl_decode(&recursion_message_type, &message,
        // Tell it to ignore everything after we stop recursion.
        offsetof(recursion_message_layout, depth_29), handles, ArrayCount(handles), &error);
    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);

    // Now add another level of recursion.
    SetUpRecursionMessage(&message);
    message.depth_28.inline_union.tag = maybe_recurse_union_kMore;
    message.depth_28.inline_union.more =
        reinterpret_cast<recursion_inline_data*>(FIDL_ALLOC_PRESENT);
    message.depth_29.inline_union.tag = maybe_recurse_union_kDone;
    message.depth_29.inline_union.handle = FIDL_HANDLE_PRESENT;

    error = nullptr;
    status = fidl_decode(&recursion_message_type, &message, sizeof(message), handles,
                         ArrayCount(handles), &error);
    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_NONNULL(error);
    const char expected_error_msg[] = "recursion depth exceeded decoding struct";
    EXPECT_STR_EQ(expected_error_msg, error, "wrong error msg");

    END_TEST;
}

BEGIN_TEST_CASE(null_parameters)
RUN_TEST(decode_null_decode_parameters)
END_TEST_CASE(null_parameters)

BEGIN_TEST_CASE(handles)
RUN_TEST(decode_single_present_handle)
RUN_TEST(decode_too_many_handles_specified_error)
RUN_TEST(decode_single_present_handle_unaligned_error)
RUN_TEST(decode_multiple_present_handles)
RUN_TEST(decode_single_absent_handle)
RUN_TEST(decode_multiple_absent_handles)
END_TEST_CASE(handles)

BEGIN_TEST_CASE(arrays)
RUN_TEST(decode_array_of_present_handles)
RUN_TEST(decode_array_of_present_handles_error_closes_handles)
RUN_TEST(decode_array_of_nonnullable_handles_some_absent_error)
RUN_TEST(decode_array_of_nullable_handles)
RUN_TEST(decode_array_of_nullable_handles_with_insufficient_handles_error)
RUN_TEST(decode_array_of_array_of_present_handles)
RUN_TEST(decode_out_of_line_array)
END_TEST_CASE(arrays)

BEGIN_TEST_CASE(strings)
RUN_TEST(decode_present_nonnullable_string)
RUN_TEST(decode_multiple_present_nullable_string)
RUN_TEST(decode_present_nullable_string)
RUN_TEST(decode_absent_nonnullable_string_error)
RUN_TEST(decode_absent_nullable_string)
RUN_TEST(decode_present_nonnullable_bounded_string)
RUN_TEST(decode_present_nullable_bounded_string)
RUN_TEST(decode_absent_nonnullable_bounded_string_error)
RUN_TEST(decode_absent_nullable_bounded_string)
RUN_TEST(decode_present_nonnullable_bounded_string_short_error)
RUN_TEST(decode_present_nullable_bounded_string_short_error)
END_TEST_CASE(strings)

BEGIN_TEST_CASE(vectors)
RUN_TEST(decode_vector_with_huge_count)
RUN_TEST(decode_present_nonnullable_vector_of_handles)
RUN_TEST(decode_present_nullable_vector_of_handles)
RUN_TEST(decode_absent_nonnullable_vector_of_handles_error)
RUN_TEST(decode_absent_nullable_vector_of_handles)
RUN_TEST(decode_present_nonnullable_bounded_vector_of_handles)
RUN_TEST(decode_present_nullable_bounded_vector_of_handles)
RUN_TEST(decode_absent_nonnullable_bounded_vector_of_handles)
RUN_TEST(decode_absent_nullable_bounded_vector_of_handles)
RUN_TEST(decode_present_nonnullable_bounded_vector_of_handles_short_error)
RUN_TEST(decode_present_nullable_bounded_vector_of_handles_short_error)
RUN_TEST(decode_present_nonnullable_vector_of_uint32)
RUN_TEST(decode_present_nullable_vector_of_uint32)
RUN_TEST(decode_absent_nonnullable_vector_of_uint32_error)
RUN_TEST(decode_absent_nullable_vector_of_uint32)
RUN_TEST(decode_present_nonnullable_bounded_vector_of_uint32)
RUN_TEST(decode_present_nullable_bounded_vector_of_uint32)
RUN_TEST(decode_absent_nonnullable_bounded_vector_of_uint32)
RUN_TEST(decode_absent_nullable_bounded_vector_of_uint32)
RUN_TEST(decode_present_nonnullable_bounded_vector_of_uint32_short_error)
RUN_TEST(decode_present_nullable_bounded_vector_of_uint32_short_error)
END_TEST_CASE(vectors)

BEGIN_TEST_CASE(unions)
RUN_TEST(decode_bad_tagged_union_error)
RUN_TEST(decode_single_membered_present_nonnullable_union)
RUN_TEST(decode_many_membered_present_nonnullable_union)
RUN_TEST(decode_single_membered_present_nullable_union)
RUN_TEST(decode_many_membered_present_nullable_union)
RUN_TEST(decode_single_membered_absent_nullable_union)
RUN_TEST(decode_many_membered_absent_nullable_union)
END_TEST_CASE(unions)

BEGIN_TEST_CASE(structs)
RUN_TEST(decode_nested_nonnullable_structs)
RUN_TEST(decode_nested_nullable_structs)
RUN_TEST(decode_nested_struct_recursion_too_deep_error)
END_TEST_CASE(structs)

} // namespace
} // namespace fidl
