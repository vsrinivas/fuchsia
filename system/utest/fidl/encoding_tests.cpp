// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>

#include <fbl/type_support.h>

#include <fidl/coding.h>
#include <fidl/internal.h>

#include <unittest/unittest.h>

namespace fidl {
namespace {

// All the data in coding tables should be pure data.
static_assert(fbl::is_standard_layout<fidl_type>::value, "");
static_assert(fbl::is_standard_layout<FidlField>::value, "");
static_assert(fbl::is_standard_layout<FidlTypeTag>::value, "");
static_assert(fbl::is_standard_layout<FidlCodedStruct>::value, "");
static_assert(fbl::is_standard_layout<FidlCodedUnion>::value, "");
static_assert(fbl::is_standard_layout<FidlCodedArray>::value, "");
static_assert(fbl::is_standard_layout<FidlCodedVector>::value, "");
static_assert(fbl::is_standard_layout<FidlCodedString>::value, "");
static_assert(fbl::is_standard_layout<FidlCodedHandle>::value, "");

// Some notes:
//
// - All tests of out-of-line bounded allocation overruns need to have
//   another big out-of-line allocation following it. This
//   distinguishes "the buffer is too small" from "the bits on the
//   wire asked for more than the type allowed".

// TODO(kulakowski) Change the tests to check for more specific error
// values, once those are settled.

const auto kSingleHandleType = fidl_type(FidlCodedHandle(ZX_OBJ_TYPE_NONE, false));
const auto kSingleNullableHandleType = fidl_type(FidlCodedHandle(ZX_OBJ_TYPE_NONE, true));

constexpr zx_handle_t dummy_handle_0 = 23;
constexpr zx_handle_t dummy_handle_1 = 24;
constexpr zx_handle_t dummy_handle_2 = 25;
constexpr zx_handle_t dummy_handle_3 = 26;
constexpr zx_handle_t dummy_handle_4 = 27;
constexpr zx_handle_t dummy_handle_5 = 28;
constexpr zx_handle_t dummy_handle_6 = 29;
constexpr zx_handle_t dummy_handle_7 = 30;
constexpr zx_handle_t dummy_handle_8 = 31;
constexpr zx_handle_t dummy_handle_9 = 32;
constexpr zx_handle_t dummy_handle_10 = 33;
constexpr zx_handle_t dummy_handle_11 = 34;
constexpr zx_handle_t dummy_handle_12 = 35;
constexpr zx_handle_t dummy_handle_13 = 36;
constexpr zx_handle_t dummy_handle_14 = 37;
constexpr zx_handle_t dummy_handle_15 = 38;
constexpr zx_handle_t dummy_handle_16 = 39;
constexpr zx_handle_t dummy_handle_17 = 40;
constexpr zx_handle_t dummy_handle_18 = 41;
constexpr zx_handle_t dummy_handle_19 = 42;
constexpr zx_handle_t dummy_handle_20 = 43;
constexpr zx_handle_t dummy_handle_21 = 44;
constexpr zx_handle_t dummy_handle_22 = 45;
constexpr zx_handle_t dummy_handle_23 = 46;
constexpr zx_handle_t dummy_handle_24 = 47;
constexpr zx_handle_t dummy_handle_25 = 48;
constexpr zx_handle_t dummy_handle_26 = 49;
constexpr zx_handle_t dummy_handle_27 = 50;
constexpr zx_handle_t dummy_handle_28 = 51;
constexpr zx_handle_t dummy_handle_29 = 52;

// All sizes in fidl encoding tables are 32 bits. The fidl compiler
// normally enforces this. Check manually in manual tests.
template <typename T, size_t N>
uint32_t ArrayCount(T const (&array)[N]) {
    static_assert(N < UINT32_MAX, "Array is too large!");
    return N;
}

template <typename T, size_t N>
uint32_t ArraySize(T const (&array)[N]) {
    static_assert(sizeof(array) < UINT32_MAX, "Array is too large!");
    return sizeof(array);
}

bool encode_null_encode_parameters() {
    BEGIN_TEST;

    struct inline_data {
        fidl_message_header_t header = {};
        zx_handle_t handle = dummy_handle_0;
    };
    struct message_layout {
        inline_data inline_struct;
    };
    const FidlField fields[] = {
        FidlField(
            &kSingleHandleType, offsetof(message_layout, inline_struct.handle)),
    };
    const fidl_type message_type =
        fidl_type(FidlCodedStruct(fields,
                                  ArrayCount(fields),
                                  sizeof(inline_data)));
    zx_handle_t handles[1] = {};

    // Null message type.
    {
        message_layout message;
        const char* error = nullptr;
        uint32_t actual_handles = 0u;
        auto status = fidl_encode(nullptr, &message, sizeof(message_layout),
                                  handles, ArrayCount(handles), &actual_handles, &error);
        EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
        EXPECT_NONNULL(error);
    }

    // Null message.
    {
        const char* error = nullptr;
        uint32_t actual_handles = 0u;
        auto status = fidl_encode(&message_type, nullptr, sizeof(message_layout),
                                  handles, ArrayCount(handles), &actual_handles, &error);
        EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
        EXPECT_NONNULL(error);
    }

    // Null handles, for a message that has a handle.
    {
        message_layout message;
        const char* error = nullptr;
        uint32_t actual_handles = 0u;
        auto status = fidl_encode(&message_type, &message, sizeof(message_layout),
                                  nullptr, 0, &actual_handles, &error);
        EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
        EXPECT_NONNULL(error);
    }

    // Null handles but positive handle count.
    {
        message_layout message;
        const char* error = nullptr;
        uint32_t actual_handles = 0u;
        auto status = fidl_encode(&message_type, &message, sizeof(message_layout),
                                  nullptr, 1, &actual_handles, &error);
        EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
        EXPECT_NONNULL(error);
    }

    // A null actual handle count pointer.
    {
        message_layout message;
        const char* error = nullptr;
        auto status = fidl_encode(&message_type, &message, sizeof(message_layout),
                                  handles, ArrayCount(handles), nullptr, &error);
        EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
        EXPECT_NONNULL(error);
    }

    // A null error string pointer is ok, though.
    {
        uint32_t actual_handles = 0u;
        auto status = fidl_encode(nullptr, nullptr, 0u,
                                  nullptr, 0u, &actual_handles, nullptr);
        EXPECT_NE(status, ZX_OK);
    }

    // A null error is also ok in success cases.
    {
        message_layout message;
        uint32_t actual_handles = 0u;
        auto status = fidl_encode(&message_type, &message, sizeof(message_layout),
                                  handles, ArrayCount(handles), &actual_handles, nullptr);
        EXPECT_EQ(status, ZX_OK);
        EXPECT_EQ(actual_handles, 1u);
        EXPECT_EQ(handles[0], dummy_handle_0);
        EXPECT_EQ(message.inline_struct.handle, FIDL_HANDLE_PRESENT);
    }

    END_TEST;
}

bool encode_single_present_handle() {
    BEGIN_TEST;

    struct inline_data {
        fidl_message_header_t header = {};
        zx_handle_t handle = dummy_handle_0;
    };
    struct message_layout {
        inline_data inline_struct;
    } message;

    const FidlField fields[] = {
        FidlField(
            &kSingleHandleType, offsetof(decltype(message), inline_struct.handle)),

    };
    const fidl_type message_type =
        fidl_type(FidlCodedStruct(fields,
                                  ArrayCount(fields),
                                  sizeof(inline_data)));

    zx_handle_t handles[1] = {};

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&message_type, &message, sizeof(message),
                              handles, ArrayCount(handles), &actual_handles, &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);
    EXPECT_EQ(actual_handles, 1u);
    EXPECT_EQ(handles[0], dummy_handle_0);
    EXPECT_EQ(message.inline_struct.handle, FIDL_HANDLE_PRESENT);

    END_TEST;
}

bool encode_multiple_present_handles() {
    BEGIN_TEST;

    struct inline_data {
        fidl_message_header_t header = {};
        uint32_t data_0 = 0u;
        zx_handle_t handle_0 = dummy_handle_0;
        uint64_t data_1 = 0u;
        zx_handle_t handle_1 = dummy_handle_1;
        zx_handle_t handle_2 = dummy_handle_2;
        uint64_t data_2 = 0u;
    };
    struct message_layout {
        inline_data inline_struct;
    } message;
    const auto channel_handle = fidl_type(FidlCodedHandle(ZX_OBJ_TYPE_CHANNEL, false));
    const auto vmo_handle = fidl_type(FidlCodedHandle(ZX_OBJ_TYPE_VMO, false));
    const FidlField fields[] = {
        FidlField(
            &kSingleHandleType,
            offsetof(decltype(message), inline_struct.handle_0)),

        FidlField(
            &channel_handle, offsetof(decltype(message), inline_struct.handle_1)),
        FidlField(
            &vmo_handle, offsetof(decltype(message), inline_struct.handle_2)),
    };
    const fidl_type message_type =
        fidl_type(FidlCodedStruct(fields,
                                  ArrayCount(fields),
                                  sizeof(inline_data)));

    zx_handle_t handles[3] = {};

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&message_type, &message, sizeof(message),
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

    struct inline_data {
        fidl_message_header_t header = {};
        zx_handle_t handle = ZX_HANDLE_INVALID;
    };
    struct message_layout {
        inline_data inline_struct;
    } message;

    const FidlField fields[] = {
        FidlField(
            &kSingleNullableHandleType, offsetof(decltype(message), inline_struct.handle)),

    };
    const fidl_type message_type =
        fidl_type(FidlCodedStruct(fields,
                                  ArrayCount(fields),
                                  sizeof(inline_data)));

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&message_type, &message, sizeof(message),
                              nullptr, 0, &actual_handles, &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);
    EXPECT_EQ(actual_handles, 0u);
    EXPECT_EQ(message.inline_struct.handle, FIDL_HANDLE_ABSENT);

    END_TEST;
}

bool encode_multiple_absent_handles() {
    BEGIN_TEST;

    struct inline_data {
        fidl_message_header_t header = {};
        uint32_t data_0 = 0u;
        zx_handle_t handle_0 = ZX_HANDLE_INVALID;
        uint64_t data_1 = 0u;
        zx_handle_t handle_1 = ZX_HANDLE_INVALID;
        zx_handle_t handle_2 = ZX_HANDLE_INVALID;
        uint64_t data_2 = 0u;
    };
    struct message_layout {
        inline_data inline_struct;
    } message;
    const auto channel_handle = fidl_type(FidlCodedHandle(ZX_OBJ_TYPE_CHANNEL, true));
    const auto vmo_handle = fidl_type(FidlCodedHandle(ZX_OBJ_TYPE_VMO, true));
    const FidlField fields[] = {
        FidlField(
            &kSingleNullableHandleType, offsetof(decltype(message), inline_struct.handle_0)),
        FidlField(
            &channel_handle, offsetof(decltype(message), inline_struct.handle_1)),
        FidlField(
            &vmo_handle, offsetof(decltype(message), inline_struct.handle_2)),
    };
    const fidl_type message_type =
        fidl_type(FidlCodedStruct(fields,
                                  ArrayCount(fields),
                                  sizeof(inline_data)));

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&message_type, &message, sizeof(message),
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

    struct inline_data {
        fidl_message_header_t header = {};
        zx_handle_t handles[4] = {
            dummy_handle_0,
            dummy_handle_1,
            dummy_handle_2,
            dummy_handle_3,
        };
    };
    struct message_layout {
        inline_data inline_struct;
    } message;

    const auto array_of_handles =
        fidl_type(FidlCodedArray(&kSingleHandleType,
                                 ArraySize(message.inline_struct.handles),
                                 sizeof(*message.inline_struct.handles)));
    const FidlField fields[] = {
        FidlField(
            &array_of_handles, offsetof(decltype(message), inline_struct.handles)),
    };
    const fidl_type message_type =
        fidl_type(FidlCodedStruct(fields,
                                  ArrayCount(fields),
                                  sizeof(inline_data)));

    zx_handle_t handles[4] = {};

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&message_type, &message, sizeof(message),
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

bool encode_array_of_nullable_handles() {
    BEGIN_TEST;

    struct inline_data {
        fidl_message_header_t header = {};
        zx_handle_t handles[5] = {
            dummy_handle_0,
            ZX_HANDLE_INVALID,
            dummy_handle_1,
            ZX_HANDLE_INVALID,
            dummy_handle_2,
        };
    };
    struct message_layout {
        inline_data inline_struct;
    } message;

    const auto array_of_handles =
        fidl_type(FidlCodedArray(&kSingleNullableHandleType,
                                 ArraySize(message.inline_struct.handles),
                                 sizeof(*message.inline_struct.handles)));
    const FidlField fields[] = {
        FidlField(
            &array_of_handles, offsetof(decltype(message), inline_struct.handles)),
    };
    const fidl_type message_type =
        fidl_type(FidlCodedStruct(fields,
                                  ArrayCount(fields),
                                  sizeof(inline_data)));

    zx_handle_t handles[3] = {};

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&message_type, &message, sizeof(message),
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

    struct inline_data {
        fidl_message_header_t header = {};
        zx_handle_t handles[5] = {
            dummy_handle_0,
            ZX_HANDLE_INVALID,
            dummy_handle_1,
            ZX_HANDLE_INVALID,
            dummy_handle_2,
        };
    };
    struct message_layout {
        inline_data inline_struct;
    } message;

    const auto array_of_handles =
        fidl_type(FidlCodedArray(&kSingleNullableHandleType,
                                 ArraySize(message.inline_struct.handles),
                                 sizeof(*message.inline_struct.handles)));
    const FidlField fields[] = {
        FidlField(
            &array_of_handles, offsetof(decltype(message), inline_struct.handles)),
    };
    const fidl_type message_type =
        fidl_type(FidlCodedStruct(fields,
                                  ArrayCount(fields),
                                  sizeof(inline_data)));

    zx_handle_t handles[2] = {};

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&message_type, &message, sizeof(message),
                              handles, ArrayCount(handles), &actual_handles, &error);

    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_NONNULL(error);

    END_TEST;
}

bool encode_array_of_array_of_present_handles() {
    BEGIN_TEST;

    struct inline_data {
        fidl_message_header_t header = {};
        zx_handle_t handles[3][4] = {
            {
        dummy_handle_0,
        dummy_handle_1,
        dummy_handle_2,
        dummy_handle_3,
            },
            {
        dummy_handle_4,
        dummy_handle_5,
        dummy_handle_6,
        dummy_handle_7,
            },
            {
        dummy_handle_8,
        dummy_handle_9,
        dummy_handle_10,
        dummy_handle_11,
            },
        };
    };
    struct message_layout {
        inline_data inline_struct;
    } message;

    const fidl_type single_handle =
        fidl_type(FidlCodedHandle(ZX_OBJ_TYPE_NONE, false));
    const fidl_type array_of_handles =
        fidl_type(FidlCodedArray(&single_handle,
                                 ArraySize(*message.inline_struct.handles),
                                 sizeof(**message.inline_struct.handles)));
    const fidl_type array_of_array_of_handles =
        fidl_type(FidlCodedArray(&array_of_handles,
                                 ArraySize(message.inline_struct.handles),
                                 sizeof(*message.inline_struct.handles)));
    const FidlField fields[] = {
        FidlField(
            &array_of_array_of_handles, offsetof(decltype(message), inline_struct.handles)),
    };
    const fidl_type message_type =
        fidl_type(FidlCodedStruct(fields,
                                  ArrayCount(fields),
                                  sizeof(inline_data)));

    zx_handle_t handles[12] = {};

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&message_type, &message, sizeof(message),
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

bool encode_out_of_line_array() {
    BEGIN_TEST;

    struct an_array {
        zx_handle_t handles[4] = {
        dummy_handle_0,
        dummy_handle_1,
        dummy_handle_2,
        dummy_handle_3,
        };
    };
    struct inline_data {
        fidl_message_header_t header = {};
        an_array* maybe_array;
    };
    struct message_layout {
        inline_data inline_struct;
        alignas(FIDL_ALIGNMENT) an_array data;
    } message;
    message.inline_struct.maybe_array = &message.data;

    const auto array_of_handles =
        fidl_type(FidlCodedArray(&kSingleHandleType,
                                 ArraySize(message.data.handles),
                                 sizeof(*message.data.handles)));
    const FidlField out_of_line_fields[] = {
        FidlField(
            &array_of_handles, offsetof(an_array, handles)),

    };
    const fidl_type out_of_line_type =
        fidl_type(FidlCodedStruct(out_of_line_fields,
                                  ArrayCount(out_of_line_fields),
                                  sizeof(an_array)));
    const fidl_type out_of_line_pointer_type =
        fidl_type(FidlCodedStructPointer(&out_of_line_type.coded_struct));
    const FidlField fields[] = {
        FidlField(
            &out_of_line_pointer_type, offsetof(decltype(message), inline_struct.maybe_array)),
    };
    const fidl_type message_type =
        fidl_type(FidlCodedStruct(fields,
                                  ArrayCount(fields),
                                  sizeof(inline_data)));

    zx_handle_t handles[4] = {};

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&message_type, &message, sizeof(message),
                              handles, ArrayCount(handles), &actual_handles, &error);

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

    struct inline_data {
        fidl_message_header_t header = {};
        fidl_string_t string = {6, nullptr};
    };
    struct message_layout {
        inline_data inline_struct;
        alignas(FIDL_ALIGNMENT) char data[6] = "hello";
    } message;
    message.inline_struct.string.data = &message.data[0];
    const fidl_type string = fidl_type(FidlCodedString(FIDL_MAX_SIZE, false));
    const FidlField fields[] = {
        FidlField(
            &string, offsetof(decltype(message), inline_struct.string)),

    };
    const fidl_type message_type =
        fidl_type(FidlCodedStruct(fields,
                                  ArrayCount(fields),
                                  sizeof(inline_data)));

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&message_type, &message, sizeof(message),
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
    EXPECT_EQ(message.data[5], 0);

    END_TEST;
}

bool encode_present_nullable_string() {
    BEGIN_TEST;

    struct inline_data {
        fidl_message_header_t header = {};
        fidl_string_t string = {6, nullptr};
    };
    struct message_layout {
        inline_data inline_struct;
        alignas(FIDL_ALIGNMENT) char data[6] = "hello";
    } message;
    message.inline_struct.string.data = &message.data[0];
    const fidl_type string = fidl_type(FidlCodedString(FIDL_MAX_SIZE, true));
    const FidlField fields[] = {
        FidlField(
            &string, offsetof(decltype(message), inline_struct.string)),

    };
    const fidl_type message_type =
        fidl_type(FidlCodedStruct(fields,
                                  ArrayCount(fields),
                                  sizeof(inline_data)));

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&message_type, &message, sizeof(message),
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
    EXPECT_EQ(message.data[5], 0);

    END_TEST;
}

bool encode_multiple_present_nullable_string() {
    BEGIN_TEST;

    // Among other things, this test ensures we handle out-of-line
    // alignment to FIDL_ALIGNMENT (i.e., 8) bytes correctly.
    struct inline_data {
        fidl_message_header_t header = {};
        fidl_string_t string = {6, nullptr};
        fidl_string_t string2 = {8, nullptr};
    };
    struct message_layout {
        inline_data inline_struct;
        alignas(FIDL_ALIGNMENT) char data[6] = "hello";
        alignas(FIDL_ALIGNMENT) char data2[8] = "world!!";
    } message;
    message.inline_struct.string.data = &message.data[0];
    message.inline_struct.string2.data = &message.data2[0];
    const fidl_type string = fidl_type(FidlCodedString(FIDL_MAX_SIZE, true));
    const FidlField fields[] = {
        FidlField(
            &string, offsetof(decltype(message), inline_struct.string)),
        FidlField(
            &string, offsetof(decltype(message), inline_struct.string2)),
    };
    const fidl_type message_type =
        fidl_type(FidlCodedStruct(fields,
                                  ArrayCount(fields),
                                  sizeof(inline_data)));

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&message_type, &message, sizeof(message),
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
    EXPECT_EQ(message.data[5], 0);
    EXPECT_EQ(message.inline_struct.string2.size, 8);
    EXPECT_EQ(reinterpret_cast<uint64_t>(message.inline_struct.string2.data), FIDL_ALLOC_PRESENT);
    EXPECT_EQ(message.data2[0], 'w');
    EXPECT_EQ(message.data2[1], 'o');
    EXPECT_EQ(message.data2[2], 'r');
    EXPECT_EQ(message.data2[3], 'l');
    EXPECT_EQ(message.data2[4], 'd');
    EXPECT_EQ(message.data2[5], '!');
    EXPECT_EQ(message.data2[6], '!');
    EXPECT_EQ(message.data2[7], 0);

    END_TEST;
}

bool encode_absent_nonnullable_string_error() {
    BEGIN_TEST;

    struct inline_data {
        fidl_message_header_t header = {};
        fidl_string_t string = {0u, nullptr};
    };
    struct message_layout {
        inline_data inline_struct;
    } message;
    const fidl_type string = fidl_type(FidlCodedString(FIDL_MAX_SIZE, false));
    const FidlField fields[] = {
        FidlField(
            &string, offsetof(decltype(message), inline_struct.string)),
    };
    const fidl_type message_type =
        fidl_type(FidlCodedStruct(fields,
                                  ArrayCount(fields),
                                  sizeof(inline_data)));

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&message_type, &message, sizeof(message),
                              nullptr, 0, &actual_handles, &error);

    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_NONNULL(error, error);

    END_TEST;
}

bool encode_absent_nullable_string() {
    BEGIN_TEST;

    struct inline_data {
        fidl_message_header_t header = {};
        fidl_string_t string = {0u, nullptr};
    };
    struct message_layout {
        inline_data inline_struct;
    } message;
    const fidl_type string = fidl_type(FidlCodedString(FIDL_MAX_SIZE, true));
    const FidlField fields[] = {
        FidlField(
            &string, offsetof(decltype(message), inline_struct.string)),
    };
    const fidl_type message_type =
        fidl_type(FidlCodedStruct(fields,
                                  ArrayCount(fields),
                                  sizeof(inline_data)));

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&message_type, &message, sizeof(message),
                              nullptr, 0, &actual_handles, &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);
    EXPECT_EQ(actual_handles, 0u);
    EXPECT_EQ(reinterpret_cast<uint64_t>(message.inline_struct.string.data), FIDL_ALLOC_ABSENT);

    END_TEST;
}

bool encode_present_nonnullable_bounded_string() {
    BEGIN_TEST;

    struct inline_data {
        fidl_message_header_t header = {};
        fidl_string_t string = {6, nullptr};
    };
    struct message_layout {
        inline_data inline_struct;
        char data[6] = "hello";
    } message;
    message.inline_struct.string.data = &message.data[0];
    const fidl_type string = fidl_type(FidlCodedString(32, false));
    const FidlField fields[] = {
        FidlField(
            &string, offsetof(decltype(message), inline_struct.string)),

    };
    const fidl_type message_type =
        fidl_type(FidlCodedStruct(fields,
                                  ArrayCount(fields),
                                  sizeof(inline_data)));

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&message_type, &message, sizeof(message),
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
    EXPECT_EQ(message.data[5], 0);

    END_TEST;
}

bool encode_present_nullable_bounded_string() {
    BEGIN_TEST;

    struct inline_data {
        fidl_message_header_t header = {};
        fidl_string_t string = {6, nullptr};
    };
    struct message_layout {
        inline_data inline_struct;
        alignas(FIDL_ALIGNMENT) char data[8] = { 'h', 'e', 'l', 'l', 'o', 0, 0, 0 };
    } message;
    message.inline_struct.string.data = &message.data[0];
    const fidl_type string = fidl_type(FidlCodedString(32, true));
    const FidlField fields[] = {
        FidlField(
            &string, offsetof(decltype(message), inline_struct.string)),

    };
    const fidl_type message_type =
        fidl_type(FidlCodedStruct(fields,
                                  ArrayCount(fields),
                                  sizeof(inline_data)));

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&message_type, &message, sizeof(message),
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
    EXPECT_EQ(message.data[5], 0);

    END_TEST;
}

bool encode_absent_nonnullable_bounded_string_error() {
    BEGIN_TEST;

    struct inline_data {
        fidl_message_header_t header = {};
        fidl_string_t string = {0u, nullptr};
    };
    struct message_layout {
        inline_data inline_struct;
    } message;
    const fidl_type string = fidl_type(FidlCodedString(32, false));
    const FidlField fields[] = {
        FidlField(
            &string, offsetof(decltype(message), inline_struct.string)),

    };
    const fidl_type message_type =
        fidl_type(FidlCodedStruct(fields,
                                  ArrayCount(fields),
                                  sizeof(inline_data)));

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&message_type, &message, sizeof(message),
                              nullptr, 0, &actual_handles, &error);

    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_NONNULL(error, error);
    EXPECT_EQ(reinterpret_cast<uint64_t>(message.inline_struct.string.data), FIDL_ALLOC_ABSENT);

    END_TEST;
}

bool encode_absent_nullable_bounded_string() {
    BEGIN_TEST;

    struct inline_data {
        fidl_message_header_t header = {};
        fidl_string_t string = {0u, nullptr};
    };
    struct message_layout {
        inline_data inline_struct;
    } message;
    const fidl_type string = fidl_type(FidlCodedString(32, true));
    const FidlField fields[] = {
        FidlField(
            &string, offsetof(decltype(message), inline_struct.string)),
    };
    const fidl_type message_type =
        fidl_type(FidlCodedStruct(fields,
                                  ArrayCount(fields),
                                  sizeof(inline_data)));

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&message_type, &message, sizeof(message),
                              nullptr, 0, &actual_handles, &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);
    EXPECT_EQ(reinterpret_cast<uint64_t>(message.inline_struct.string.data), FIDL_ALLOC_ABSENT);

    END_TEST;
}

bool encode_present_nonnullable_bounded_string_short_error() {
    BEGIN_TEST;

    struct inline_data {
        fidl_message_header_t header = {};
        fidl_string_t short_string = {6, nullptr};
        fidl_string_t string = {6, nullptr};
    };
    struct message_layout {
        inline_data inline_struct;
        alignas(FIDL_ALIGNMENT) char data[6] = "hello";
        alignas(FIDL_ALIGNMENT) char data2[6] = "hello";
    } message;
    message.inline_struct.short_string.data = &message.data[0];
    message.inline_struct.string.data = &message.data2[0];
    const fidl_type short_string = fidl_type(FidlCodedString(4, false));
    const fidl_type string = fidl_type(FidlCodedString(FIDL_MAX_SIZE, false));
    const FidlField fields[] = {
        FidlField(
            &short_string, offsetof(decltype(message), inline_struct.short_string)),

        FidlField(
            &string, offsetof(decltype(message), inline_struct.string)),
    };
    const fidl_type message_type =
        fidl_type(FidlCodedStruct(fields,
                                  ArrayCount(fields),
                                  sizeof(inline_data)));

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&message_type, &message, sizeof(message),
                              nullptr, 0, &actual_handles, &error);

    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_NONNULL(error);

    END_TEST;
}

bool encode_present_nullable_bounded_string_short_error() {
    BEGIN_TEST;

    struct inline_data {
        fidl_message_header_t header = {};
        fidl_string_t short_string = {6, nullptr};
        fidl_string_t string = {6, nullptr};
    };
    struct message_layout {
        inline_data inline_struct;
        alignas(FIDL_ALIGNMENT) char data[6] = "hello";
        alignas(FIDL_ALIGNMENT) char data2[6] = "hello";
    } message;
    message.inline_struct.short_string.data = &message.data[0];
    message.inline_struct.string.data = &message.data2[0];
    const fidl_type short_string = fidl_type(FidlCodedString(4, true));
    const fidl_type string = fidl_type(FidlCodedString(FIDL_MAX_SIZE, true));
    const FidlField fields[] = {
        FidlField(
            &short_string, offsetof(decltype(message), inline_struct.short_string)),
        FidlField(
            &string, offsetof(decltype(message), inline_struct.string)),
    };
    const fidl_type message_type =
        fidl_type(FidlCodedStruct(fields,
                                  ArrayCount(fields),
                                  sizeof(inline_data)));

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&message_type, &message, sizeof(message),
                              nullptr, 0, &actual_handles, &error);

    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_NONNULL(error);

    END_TEST;
}

bool encode_present_nonnullable_vector_of_handles() {
    BEGIN_TEST;

    struct inline_data {
        fidl_message_header_t header = {};
        fidl_vector_t vector = {4, nullptr};
    };
    struct message_layout {
        inline_data inline_struct;
        alignas(FIDL_ALIGNMENT) zx_handle_t handles[4] = {
        dummy_handle_0,
        dummy_handle_1,
        dummy_handle_2,
        dummy_handle_3,
        };
    } message;
    message.inline_struct.vector.data = &message.handles[0];

    const auto vector_of_handles =
        fidl_type(FidlCodedVector(&kSingleHandleType,
                                  FIDL_MAX_SIZE,
                                  sizeof(*message.handles), false));
    const FidlField fields[] = {
        FidlField(
            &vector_of_handles, offsetof(decltype(message), inline_struct.vector)),

    };
    const fidl_type message_type =
        fidl_type(FidlCodedStruct(fields,
                                  ArrayCount(fields),
                                  sizeof(inline_data)));

    zx_handle_t handles[4] = {};

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&message_type, &message, sizeof(message),
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

bool encode_present_nullable_vector_of_handles() {
    BEGIN_TEST;

    struct inline_data {
        fidl_message_header_t header = {};
        fidl_vector_t vector = {4, nullptr};
    };
    struct message_layout {
        inline_data inline_struct;
        alignas(FIDL_ALIGNMENT) zx_handle_t handles[4] = {
        dummy_handle_0,
        dummy_handle_1,
        dummy_handle_2,
        dummy_handle_3,
        };
    } message;
    message.inline_struct.vector.data = &message.handles[0];

    const auto vector_of_handles =
        fidl_type(FidlCodedVector(&kSingleHandleType,
                                  FIDL_MAX_SIZE,
                                  sizeof(*message.handles), false));
    const FidlField fields[] = {
        FidlField(
            &vector_of_handles, offsetof(decltype(message), inline_struct.vector)),

    };
    const fidl_type message_type =
        fidl_type(FidlCodedStruct(fields,
                                  ArrayCount(fields),
                                  sizeof(inline_data)));

    zx_handle_t handles[4] = {};

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&message_type, &message, sizeof(message),
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

    struct inline_data {
        fidl_message_header_t header = {};
        fidl_vector_t vector = {4, nullptr};
    };
    struct message_layout {
        inline_data inline_struct;
        alignas(FIDL_ALIGNMENT) zx_handle_t handles[4] = {
        dummy_handle_0,
        dummy_handle_1,
        dummy_handle_2,
        dummy_handle_3,
        };
    } message;

    const auto vector_of_handles =
        fidl_type(FidlCodedVector(&kSingleHandleType,
                                  FIDL_MAX_SIZE,
                                  sizeof(*message.handles), false));
    const FidlField fields[] = {
        FidlField(
            &vector_of_handles, offsetof(decltype(message), inline_struct.vector)),

    };
    const fidl_type message_type =
        fidl_type(FidlCodedStruct(fields,
                                  ArrayCount(fields),
                                  sizeof(inline_data)));

    zx_handle_t handles[4] = {};

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&message_type, &message, sizeof(message),
                              handles, ArrayCount(handles), &actual_handles, &error);

    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_NONNULL(error, error);

    END_TEST;
}

bool encode_absent_nullable_vector_of_handles() {
    BEGIN_TEST;

    struct inline_data {
        fidl_message_header_t header = {};
        fidl_vector_t vector = {4, nullptr};
    };
    struct message_layout {
        inline_data inline_struct;
    } message;

    const auto vector_of_handles =
        fidl_type(FidlCodedVector(&kSingleHandleType,
                                  FIDL_MAX_SIZE,
                                  sizeof(4), true));
    const FidlField fields[] = {
        FidlField(
            &vector_of_handles, offsetof(decltype(message), inline_struct.vector)),

    };
    const fidl_type message_type =
        fidl_type(FidlCodedStruct(fields,
                                  ArrayCount(fields),
                                  sizeof(inline_data)));

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&message_type, &message, sizeof(message),
                              nullptr, 0u, &actual_handles, &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);
    EXPECT_EQ(actual_handles, 0u);

    auto message_handles = reinterpret_cast<uint64_t>(message.inline_struct.vector.data);
    EXPECT_EQ(message_handles, FIDL_ALLOC_ABSENT);

    END_TEST;
}

bool encode_present_nonnullable_bounded_vector_of_handles() {
    BEGIN_TEST;

    struct inline_data {
        fidl_message_header_t header = {};
        fidl_vector_t vector = {4, nullptr};
    };
    struct message_layout {
        inline_data inline_struct;
        alignas(FIDL_ALIGNMENT) zx_handle_t handles[4] = {
        dummy_handle_0,
        dummy_handle_1,
        dummy_handle_2,
        dummy_handle_3,
        };
    } message;
    message.inline_struct.vector.data = &message.handles[0];

    const auto vector_of_handles =
        fidl_type(FidlCodedVector(&kSingleHandleType,
                                  32,
                                  sizeof(*message.handles), false));
    const FidlField fields[] = {
        FidlField(
            &vector_of_handles, offsetof(decltype(message), inline_struct.vector)),

    };
    const fidl_type message_type =
        fidl_type(FidlCodedStruct(fields,
                                  ArrayCount(fields),
                                  sizeof(inline_data)));

    zx_handle_t handles[4] = {};

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&message_type, &message, sizeof(message),
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

bool encode_present_nullable_bounded_vector_of_handles() {
    BEGIN_TEST;

    struct inline_data {
        fidl_message_header_t header = {};
        fidl_vector_t vector = {4, nullptr};
    };
    struct message_layout {
        inline_data inline_struct;
        alignas(FIDL_ALIGNMENT) zx_handle_t handles[4] = {
        dummy_handle_0,
        dummy_handle_1,
        dummy_handle_2,
        dummy_handle_3,
        };
    } message;
    message.inline_struct.vector.data = &message.handles[0];

    const auto vector_of_handles =
        fidl_type(FidlCodedVector(&kSingleHandleType,
                                  32,
                                  sizeof(*message.handles), true));
    const FidlField fields[] = {
        FidlField(
            &vector_of_handles, offsetof(decltype(message), inline_struct.vector)),

    };
    const fidl_type message_type =
        fidl_type(FidlCodedStruct(fields,
                                  ArrayCount(fields),
                                  sizeof(inline_data)));

    zx_handle_t handles[4] = {};

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&message_type, &message, sizeof(message),
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

    struct inline_data {
        fidl_message_header_t header = {};
        fidl_vector_t vector = {0, nullptr};
    };
    struct message_layout {
        inline_data inline_struct;
    } message;

    const auto vector_of_handles =
        fidl_type(FidlCodedVector(&kSingleHandleType,
                                  32, 4, true));
    const FidlField fields[] = {
        FidlField(
            &vector_of_handles, offsetof(decltype(message), inline_struct.vector)),
    };
    const fidl_type message_type =
        fidl_type(FidlCodedStruct(fields,
                                  ArrayCount(fields),
                                  sizeof(inline_data)));

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&message_type, &message, sizeof(message),
                              nullptr, 0u, &actual_handles, &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);
    EXPECT_EQ(actual_handles, 0u);

    auto message_handles = reinterpret_cast<uint64_t>(message.inline_struct.vector.data);
    EXPECT_EQ(message_handles, FIDL_ALLOC_ABSENT);

    END_TEST;
}

bool encode_absent_nullable_bounded_vector_of_handles() {
    BEGIN_TEST;

    struct inline_data {
        fidl_message_header_t header = {};
        fidl_vector_t vector = {4, nullptr};
    };
    struct message_layout {
        inline_data inline_struct;
    } message;

    const auto vector_of_handles =
        fidl_type(FidlCodedVector(&kSingleHandleType,
                                  32, 4, true));
    const FidlField fields[] = {
        FidlField(
            &vector_of_handles, offsetof(decltype(message), inline_struct.vector)),
    };
    const fidl_type message_type =
        fidl_type(FidlCodedStruct(fields,
                                  ArrayCount(fields),
                                  sizeof(inline_data)));

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&message_type, &message, sizeof(message),
                              nullptr, 0u, &actual_handles, &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);
    EXPECT_EQ(actual_handles, 0u);

    auto message_handles = reinterpret_cast<uint64_t>(message.inline_struct.vector.data);
    EXPECT_EQ(message_handles, FIDL_ALLOC_ABSENT);

    END_TEST;
}

bool encode_present_nonnullable_bounded_vector_of_handles_short_error() {
    BEGIN_TEST;

    struct inline_data {
        fidl_message_header_t header = {};
        fidl_vector_t short_vector = {4, nullptr};
        fidl_vector_t vector = {4, nullptr};
    };
    struct message_layout {
        inline_data inline_struct;
        alignas(FIDL_ALIGNMENT) zx_handle_t handles[4] = {
        dummy_handle_0,
        dummy_handle_1,
        dummy_handle_2,
        dummy_handle_3,
        };
        alignas(FIDL_ALIGNMENT) zx_handle_t handles2[4] = {
        dummy_handle_4,
        dummy_handle_5,
        dummy_handle_6,
        dummy_handle_7,
        };
    } message;
    message.inline_struct.short_vector.data = &message.handles[0];
    message.inline_struct.vector.data = &message.handles2[0];

    const auto short_vector_of_handles =
        fidl_type(FidlCodedVector(&kSingleHandleType,
                                  2,
                                  sizeof(*message.handles), false));
    const auto vector_of_handles =
        fidl_type(FidlCodedVector(&kSingleHandleType,
                                  FIDL_MAX_SIZE,
                                  sizeof(*message.handles2), false));
    const FidlField fields[] = {
        FidlField(
            &short_vector_of_handles, offsetof(decltype(message), inline_struct.short_vector)),
        FidlField(
            &vector_of_handles, offsetof(decltype(message), inline_struct.vector)),
    };
    const fidl_type message_type =
        fidl_type(FidlCodedStruct(fields,
                                  ArrayCount(fields),
                                  sizeof(inline_data)));

    zx_handle_t handles[8] = {};

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&message_type, &message, sizeof(message),
                              handles, ArrayCount(handles), &actual_handles, &error);

    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_NONNULL(error);

    END_TEST;
}

bool encode_present_nullable_bounded_vector_of_handles_short_error() {
    BEGIN_TEST;

    struct inline_data {
        fidl_message_header_t header = {};
        fidl_vector_t short_vector = {4, nullptr};
        fidl_vector_t vector = {4, nullptr};
    };
    struct message_layout {
        inline_data inline_struct;
        alignas(FIDL_ALIGNMENT) zx_handle_t handles[4] = {
        dummy_handle_0,
        dummy_handle_1,
        dummy_handle_2,
        dummy_handle_3,
        };
        alignas(FIDL_ALIGNMENT) zx_handle_t handles2[4] = {
        dummy_handle_4,
        dummy_handle_5,
        dummy_handle_6,
        dummy_handle_7,
        };
    } message;
    message.inline_struct.short_vector.data = &message.handles[0];
    message.inline_struct.vector.data = &message.handles2[0];

    const auto short_vector_of_handles =
        fidl_type(FidlCodedVector(&kSingleHandleType,
                                  2,
                                  sizeof(*message.handles), true));
    const auto vector_of_handles =
        fidl_type(FidlCodedVector(&kSingleHandleType,
                                  4,
                                  sizeof(*message.handles2), true));
    const FidlField fields[] = {
        FidlField(
            &short_vector_of_handles, offsetof(decltype(message), inline_struct.short_vector)),
        FidlField(
            &vector_of_handles, offsetof(decltype(message), inline_struct.vector)),
    };
    const fidl_type message_type =
        fidl_type(FidlCodedStruct(fields,
                                  ArrayCount(fields),
                                  sizeof(inline_data)));

    zx_handle_t handles[8] = {};

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&message_type, &message, sizeof(message),
                              handles, ArrayCount(handles), &actual_handles, &error);

    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_NONNULL(error);

    END_TEST;
}

bool encode_bad_tagged_union_error() {
    BEGIN_TEST;

    enum single_handle_tag : uint32_t {
        kHandle = 0u,
        kInvalid = 23u,
    };

    struct single_handle_union {
        fidl_union_tag_t tag = kInvalid;
        union {
            zx_handle_t handle = dummy_handle_0;
        };
    };

    struct inline_data {
        fidl_message_header_t header = {};
        single_handle_union data;
    };
    struct message_layout {
        inline_data inline_struct;
    } message;

    const fidl_type* union_members[] = {
        &kSingleHandleType,
    };
    const fidl_type union_type = fidl_type(FidlCodedUnion(union_members,
                                                          ArrayCount(union_members),
                                                          sizeof(single_handle_union)));
    const FidlField fields[] = {
        FidlField(
            &union_type, offsetof(decltype(message), inline_struct.data)),
    };
    const fidl_type message_type =
        fidl_type(FidlCodedStruct(fields,
                                  ArrayCount(fields),
                                  sizeof(inline_data)));

    zx_handle_t handles[1] = {};

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&message_type, &message, sizeof(message),
                              handles, ArrayCount(handles), &actual_handles, &error);

    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_NONNULL(error);

    END_TEST;
}

bool encode_single_armed_present_nonnullable_union() {
    BEGIN_TEST;

    enum single_handle_tag : uint32_t {
        kHandle = 0u,
    };

    struct single_handle_union {
        fidl_union_tag_t tag = kHandle;
        union {
            zx_handle_t handle = dummy_handle_0;
        };
    };

    struct inline_data {
        fidl_message_header_t header = {};
        single_handle_union data;
    };
    struct message_layout {
        inline_data inline_struct;
    } message;

    const fidl_type* union_members[] = {
        &kSingleHandleType,
    };
    const fidl_type union_type = fidl_type(FidlCodedUnion(union_members,
                                                          ArrayCount(union_members),
                                                          sizeof(single_handle_union)));
    const FidlField fields[] = {
        FidlField(
            &union_type, offsetof(decltype(message), inline_struct.data)),

    };
    const fidl_type message_type =
        fidl_type(FidlCodedStruct(fields,
                                  ArrayCount(fields),
                                  sizeof(inline_data)));

    zx_handle_t handles[1] = {};

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&message_type, &message, sizeof(message),
                              handles, ArrayCount(handles), &actual_handles, &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);
    EXPECT_EQ(actual_handles, 1u);
    EXPECT_EQ(message.inline_struct.data.tag, kHandle);
    EXPECT_EQ(message.inline_struct.data.handle, FIDL_HANDLE_PRESENT);
    EXPECT_EQ(handles[0], dummy_handle_0);

    END_TEST;
}

bool encode_many_armed_present_nonnullable_union() {
    BEGIN_TEST;

    enum many_handle_tag : uint32_t {
        kHandle = 0u,
        kArrayOfHandles = 1u,
        kArrayOfArrayOfHandles = 2u,
    };

    struct many_handle_union {
        fidl_union_tag_t tag = kArrayOfArrayOfHandles;
        union {
            zx_handle_t array_of_array_of_handles[2][2] = {
                {
                    dummy_handle_0,
                    dummy_handle_1,
                },
                {
                    dummy_handle_2,
                    dummy_handle_3,
                }
            };
            zx_handle_t array_of_handles[2];
            zx_handle_t handle;
        };
    };

    struct inline_data {
        fidl_message_header_t header = {};
        many_handle_union data;
    };
    struct message_layout {
        inline_data inline_struct;
    } message;

    const fidl_type one_handle =
        fidl_type(FidlCodedHandle(ZX_OBJ_TYPE_NONE, false));
    const fidl_type array_of_handles =
        fidl_type(FidlCodedArray(&one_handle,
                                 ArraySize(*message.inline_struct.data.array_of_array_of_handles),
                                 sizeof(**message.inline_struct.data.array_of_array_of_handles)));
    const fidl_type array_of_array_of_handles =
        fidl_type(FidlCodedArray(&array_of_handles,
                                 ArraySize(message.inline_struct.data.array_of_array_of_handles),
                                 sizeof(*message.inline_struct.data.array_of_array_of_handles)));
    const fidl_type* union_members[] = {
        &one_handle,
        &array_of_handles,
        &array_of_array_of_handles,
    };
    const fidl_type union_type = fidl_type(FidlCodedUnion(union_members,
                                                          ArrayCount(union_members),
                                                          sizeof(many_handle_union)));
    const FidlField fields[] = {
        FidlField(
            &union_type, offsetof(decltype(message), inline_struct.data)),

    };
    const fidl_type message_type =
        fidl_type(FidlCodedStruct(fields,
                                  ArrayCount(fields),
                                  sizeof(inline_data)));

    zx_handle_t handles[4] = {};

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&message_type, &message, sizeof(message),
                              handles, ArrayCount(handles), &actual_handles, &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);
    EXPECT_EQ(actual_handles, 4u);
    EXPECT_EQ(message.inline_struct.data.tag, kArrayOfArrayOfHandles);
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

    enum single_handle_tag : uint32_t {
        kHandle = 0u,
    };

    struct single_handle_union {
        fidl_union_tag_t tag = kHandle;
        union {
            zx_handle_t handle = dummy_handle_0;
        };
    };

    struct inline_data {
        fidl_message_header_t header = {};
        single_handle_union* data = nullptr;
    };
    struct message_layout {
        inline_data inline_struct;
        alignas(FIDL_ALIGNMENT) single_handle_union data;
    } message;
    message.inline_struct.data = &message.data;

    const fidl_type* union_members[] = {
        &kSingleNullableHandleType,
    };
    const fidl_type union_type = fidl_type(FidlCodedUnion(union_members,
                                                          ArrayCount(union_members),
                                                          sizeof(single_handle_union)));
    const fidl_type union_pointer_type = fidl_type(FidlCodedUnionPointer(&union_type.coded_union));
    const FidlField fields[] = {
        FidlField(
            &union_pointer_type, offsetof(decltype(message), inline_struct.data)),

    };
    const fidl_type message_type =
        fidl_type(FidlCodedStruct(fields,
                                  ArrayCount(fields),
                                  sizeof(inline_data)));

    zx_handle_t handles[1] = {};

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&message_type, &message, sizeof(message),
                              handles, ArrayCount(handles), &actual_handles, &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);
    EXPECT_EQ(actual_handles, 1u);
    EXPECT_EQ(reinterpret_cast<uint64_t>(message.inline_struct.data), FIDL_ALLOC_PRESENT);
    EXPECT_EQ(message.data.tag, kHandle);
    EXPECT_EQ(message.data.handle, FIDL_HANDLE_PRESENT);
    EXPECT_EQ(handles[0], dummy_handle_0);

    END_TEST;
}

bool encode_many_armed_present_nullable_union() {
    BEGIN_TEST;

    enum many_handle_tag : uint32_t {
        kHandle = 0u,
        kArrayOfHandles = 1u,
        kArrayOfArrayOfHandles = 2u,
    };

    struct many_handle_union {
        fidl_union_tag_t tag = kArrayOfArrayOfHandles;
        union {
            zx_handle_t array_of_array_of_handles[2][2] = {
                {
        dummy_handle_0,
        dummy_handle_1,
                },
                {
        dummy_handle_2,
        dummy_handle_3,
                }};
            zx_handle_t array_of_handles[2];
            zx_handle_t handle;
        };
    };

    struct inline_data {
        fidl_message_header_t header = {};
        many_handle_union* data = nullptr;
    };
    struct message_layout {
        inline_data inline_struct;
        alignas(FIDL_ALIGNMENT) many_handle_union data;
    } message;
    message.inline_struct.data = &message.data;

    const fidl_type one_handle =
        fidl_type(FidlCodedHandle(ZX_OBJ_TYPE_NONE, true));
    const fidl_type array_of_handles =
        fidl_type(FidlCodedArray(&one_handle,
                                 ArraySize(*message.inline_struct.data->array_of_array_of_handles),
                                 sizeof(**message.inline_struct.data->array_of_array_of_handles)));
    const fidl_type array_of_array_of_handles =
        fidl_type(FidlCodedArray(&array_of_handles,
                                 ArraySize(message.inline_struct.data->array_of_array_of_handles),
                                 sizeof(*message.inline_struct.data->array_of_array_of_handles)));
    const fidl_type* union_members[] = {
        &one_handle,
        &array_of_handles,
        &array_of_array_of_handles,
    };
    const fidl_type union_type = fidl_type(FidlCodedUnion(union_members,
                                                          ArrayCount(union_members),
                                                          sizeof(many_handle_union)));
    const fidl_type union_pointer_type = fidl_type(FidlCodedUnionPointer(&union_type.coded_union));
    const FidlField fields[] = {
        FidlField(
            &union_pointer_type, offsetof(decltype(message), inline_struct.data)),

    };
    const fidl_type message_type =
        fidl_type(FidlCodedStruct(fields,
                                  ArrayCount(fields),
                                  sizeof(inline_data)));

    zx_handle_t handles[4] = {};

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&message_type, &message, sizeof(message),
                              handles, ArrayCount(handles), &actual_handles, &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);
    EXPECT_EQ(actual_handles, 4u);
    EXPECT_EQ(reinterpret_cast<uint64_t>(message.inline_struct.data), FIDL_ALLOC_PRESENT);
    EXPECT_EQ(message.data.tag, kArrayOfArrayOfHandles);
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

    enum single_handle_tag : uint32_t {
        kHandle = 0u,
    };

    struct single_handle_union {
        fidl_union_tag_t tag = kHandle;
        union {
            zx_handle_t handle = FIDL_HANDLE_PRESENT;
        };
    };

    struct inline_data {
        fidl_message_header_t header = {};
        single_handle_union* data = nullptr;
    };
    struct message_layout {
        inline_data inline_struct;
    } message;

    const fidl_type* union_members[] = {
        &kSingleNullableHandleType};
    const fidl_type union_type = fidl_type(FidlCodedUnion(union_members,
                                                          ArrayCount(union_members),
                                                          sizeof(single_handle_union)));
    const fidl_type union_pointer_type = fidl_type(FidlCodedUnionPointer(&union_type.coded_union));
    const FidlField fields[] = {
        FidlField(
            &union_pointer_type, offsetof(decltype(message), inline_struct.data)),

    };
    const fidl_type message_type =
        fidl_type(FidlCodedStruct(fields,
                                  ArrayCount(fields),
                                  sizeof(inline_data)));

    zx_handle_t handles[1] = {};

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&message_type, &message, sizeof(message),
                              handles, ArrayCount(handles), &actual_handles, &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);
    EXPECT_EQ(actual_handles, 0u);
    EXPECT_EQ(reinterpret_cast<uint64_t>(message.inline_struct.data), FIDL_ALLOC_ABSENT);

    END_TEST;
}

bool encode_many_armed_absent_nullable_union() {
    BEGIN_TEST;

    enum many_handle_tag : uint32_t {
        kHandle = 0u,
        kArrayOfHandles = 1u,
        kArrayOfArrayOfHandles = 2u,
    };

    struct many_handle_union {
        fidl_union_tag_t tag;
        union {
            zx_handle_t array_of_array_of_handles[2][2];
            zx_handle_t array_of_handles[2];
            zx_handle_t handle;
        };
    };

    struct inline_data {
        fidl_message_header_t header = {};
        many_handle_union* data = nullptr;
    };
    struct message_layout {
        inline_data inline_struct;
    } message;

    const fidl_type one_handle =
        fidl_type(FidlCodedHandle(ZX_OBJ_TYPE_NONE, true));
    const fidl_type array_of_handles =
        fidl_type(FidlCodedArray(&one_handle,
                                 ArraySize(*message.inline_struct.data->array_of_array_of_handles),
                                 sizeof(**message.inline_struct.data->array_of_array_of_handles)));
    const fidl_type array_of_array_of_handles =
        fidl_type(FidlCodedArray(&array_of_handles,
                                 ArraySize(message.inline_struct.data->array_of_array_of_handles),
                                 sizeof(*message.inline_struct.data->array_of_array_of_handles)));
    const fidl_type* union_members[] = {
        &one_handle,
        &array_of_handles,
        &array_of_array_of_handles,
    };
    const fidl_type union_type = fidl_type(FidlCodedUnion(union_members,
                                                          ArrayCount(union_members),
                                                          sizeof(many_handle_union)));
    const fidl_type union_pointer_type = fidl_type(FidlCodedUnionPointer(&union_type.coded_union));
    const FidlField fields[] = {
        FidlField(
            &union_pointer_type, offsetof(decltype(message), inline_struct.data)),

    };
    const fidl_type message_type =
        fidl_type(FidlCodedStruct(fields,
                                  ArrayCount(fields),
                                  sizeof(inline_data)));

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&message_type, &message, sizeof(message),
                              nullptr, 0u, &actual_handles, &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);
    EXPECT_EQ(actual_handles, 0u);
    EXPECT_EQ(reinterpret_cast<uint64_t>(message.inline_struct.data), FIDL_ALLOC_ABSENT);

    END_TEST;
}

bool encode_nested_nonnullable_structs() {
    BEGIN_TEST;

    // Note the traversal order! l1 -> l3 -> l2 -> l0

    struct level_3 {
        uint32_t padding_3 = 0u;
        zx_handle_t handle_3 = dummy_handle_1;
    };

    struct level_2 {
        uint64_t padding_2 = 0u;
        level_3 l3;
        zx_handle_t handle_2 = dummy_handle_2;
    };

    struct level_1 {
        zx_handle_t handle_1 = dummy_handle_0;
        level_2 l2;
        uint64_t padding_1 = 0u;
    };

    struct level_0 {
        uint64_t padding_0 = 0u;
        level_1 l1;
        zx_handle_t handle_0 = dummy_handle_3;
    };

    struct inline_data {
        fidl_message_header_t header = {};
        level_0 l0;
    };

    struct message_layout {
        inline_data inline_struct;
    } message;

    const FidlField level_3_fields[] = {
        FidlField(
            &kSingleHandleType, offsetof(decltype(message.inline_struct.l0.l1.l2.l3), handle_3)),
    };
    const fidl_type level_3_struct =
        fidl_type(FidlCodedStruct(level_3_fields,
                                  ArrayCount(level_3_fields),
                                  sizeof(level_3)));
    const FidlField level_2_fields[] = {
        FidlField(
            &level_3_struct, offsetof(decltype(message.inline_struct.l0.l1.l2), l3)),
        FidlField(
            &kSingleHandleType, offsetof(decltype(message.inline_struct.l0.l1.l2), handle_2)),
    };
    const fidl_type level_2_struct =
        fidl_type(FidlCodedStruct(level_2_fields,
                                  ArrayCount(level_2_fields),
                                  sizeof(level_2)));
    const FidlField level_1_fields[] = {
        FidlField(
            &kSingleHandleType, offsetof(decltype(message.inline_struct.l0.l1), handle_1)),
        FidlField(
            &level_2_struct, offsetof(decltype(message.inline_struct.l0.l1), l2)),
    };
    const fidl_type level_1_struct =
        fidl_type(FidlCodedStruct(level_1_fields,
                                  ArrayCount(level_1_fields),
                                  sizeof(level_1)));
    const FidlField level_0_fields[] = {
        FidlField(
            &level_1_struct, offsetof(decltype(message.inline_struct.l0), l1)),
        FidlField(
            &kSingleHandleType, offsetof(decltype(message.inline_struct.l0), handle_0)),
    };
    const fidl_type level_0_struct =
        fidl_type(FidlCodedStruct(level_0_fields,
                                  ArrayCount(level_1_fields),
                                  sizeof(level_0)));
    const FidlField fields[] = {
        FidlField(
            &level_0_struct, offsetof(decltype(message), inline_struct.l0)),

    };
    const fidl_type message_type =
        fidl_type(FidlCodedStruct(fields,
                                  ArrayCount(fields),
                                  sizeof(inline_data)));

    zx_handle_t handles[4] = {};

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&message_type, &message, sizeof(message),
                              handles, ArrayCount(handles), &actual_handles, &error);

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

    struct level_3 {
        uint32_t padding_3 = 0u;
        zx_handle_t handle_3;
    };

    struct level_2 {
        uint64_t padding_2 = 0u;
        level_3* l3_present = nullptr;
        level_3* l3_absent = nullptr;
        level_3 l3_inline;
        zx_handle_t handle_2;
    };

    struct level_1 {
        zx_handle_t handle_1;
        level_2* l2_present = nullptr;
        level_2 l2_inline;
        level_2* l2_absent = nullptr;
        uint64_t padding_1 = 0u;
    };

    struct level_0 {
        uint64_t padding_0 = 0u;
        level_1* l1_absent = nullptr;
        level_1 l1_inline;
        zx_handle_t handle_0;
        level_1* l1_present = nullptr;
    };

    struct inline_data {
        fidl_message_header_t header = {};
        level_0 l0_inline;
        level_0* l0_absent = nullptr;
        level_0* l0_present = nullptr;
    };

    static_assert(sizeof(inline_data) == 136, "");

    struct message_layout {
        inline_data inline_struct;
        alignas(FIDL_ALIGNMENT) level_2 in_in_out_2;
        alignas(FIDL_ALIGNMENT) level_3 in_in_out_out_3;
        alignas(FIDL_ALIGNMENT) level_3 in_in_in_out_3;
        alignas(FIDL_ALIGNMENT) level_1 in_out_1;
        alignas(FIDL_ALIGNMENT) level_2 in_out_out_2;
        alignas(FIDL_ALIGNMENT) level_3 in_out_out_out_3;
        alignas(FIDL_ALIGNMENT) level_3 in_out_in_out_3;
        alignas(FIDL_ALIGNMENT) level_0 out_0;
        alignas(FIDL_ALIGNMENT) level_2 out_in_out_2;
        alignas(FIDL_ALIGNMENT) level_3 out_in_out_out_3;
        alignas(FIDL_ALIGNMENT) level_3 out_in_in_out_3;
        alignas(FIDL_ALIGNMENT) level_1 out_out_1;
        alignas(FIDL_ALIGNMENT) level_2 out_out_out_2;
        alignas(FIDL_ALIGNMENT) level_3 out_out_out_out_3;
        alignas(FIDL_ALIGNMENT) level_3 out_out_in_out_3;
    } message;

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
    message.inline_struct.l0_present->l1_present->l2_present->l3_present = &message.out_out_out_out_3;
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

    const FidlField level_3_fields[] = {
        FidlField(
            &kSingleHandleType, offsetof(level_3, handle_3)),

    };
    const fidl_type level_3_struct =
        fidl_type(FidlCodedStruct(level_3_fields,
                                  ArrayCount(level_3_fields),
                                  sizeof(level_3)));
    const fidl_type level_3_struct_pointer = fidl_type(FidlCodedStructPointer(&level_3_struct.coded_struct));
    const FidlField level_2_fields[] = {
        FidlField(
            &level_3_struct_pointer, offsetof(level_2, l3_present)),
        FidlField(
            &level_3_struct_pointer, offsetof(level_2, l3_absent)),
        FidlField(
            &level_3_struct, offsetof(level_2, l3_inline)),
        FidlField(
            &kSingleHandleType, offsetof(level_2, handle_2)),
    };
    const fidl_type level_2_struct =
        fidl_type(FidlCodedStruct(level_2_fields,
                                  ArrayCount(level_2_fields),
                                  sizeof(level_2)));
    const fidl_type level_2_struct_pointer = fidl_type(FidlCodedStructPointer(&level_2_struct.coded_struct));
    const FidlField level_1_fields[] = {
        FidlField(
            &kSingleHandleType, offsetof(level_1, handle_1)),
        FidlField(
            &level_2_struct_pointer, offsetof(level_1, l2_present)),
        FidlField(
            &level_2_struct, offsetof(level_1, l2_inline)),
        FidlField(
            &level_2_struct_pointer, offsetof(level_1, l2_absent)),
    };
    const fidl_type level_1_struct =
        fidl_type(FidlCodedStruct(level_1_fields,
                                  ArrayCount(level_1_fields),
                                  sizeof(level_1)));
    const fidl_type level_1_struct_pointer = fidl_type(FidlCodedStructPointer(&level_1_struct.coded_struct));
    const FidlField level_0_fields[] = {
        FidlField(
            &level_1_struct_pointer, offsetof(level_0, l1_absent)),
        FidlField(
            &level_1_struct, offsetof(level_0, l1_inline)),
        FidlField(
            &kSingleHandleType, offsetof(level_0, handle_0)),
        FidlField(
            &level_1_struct_pointer, offsetof(level_0, l1_present)),
    };
    const fidl_type level_0_struct =
        fidl_type(FidlCodedStruct(level_0_fields,
                                  ArrayCount(level_0_fields),
                                  sizeof(level_0)));
    const fidl_type level_0_struct_pointer = fidl_type(FidlCodedStructPointer(&level_0_struct.coded_struct));
    const FidlField fields[] = {
        FidlField(
            &level_0_struct, offsetof(inline_data, l0_inline)),
        FidlField(
            &level_0_struct_pointer, offsetof(inline_data, l0_absent)),
        FidlField(
            &level_0_struct_pointer, offsetof(inline_data, l0_present)),
    };
    const fidl_type message_type =
        fidl_type(FidlCodedStruct(fields,
                                  ArrayCount(fields),
                                  sizeof(inline_data)));

    zx_handle_t handles[30] = {};

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&message_type, &message, sizeof(message),
                              handles, ArrayCount(handles), &actual_handles, &error);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NULL(error, error);

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
    EXPECT_EQ(reinterpret_cast<uintptr_t>(message.inline_struct.l0_inline.l1_absent), FIDL_ALLOC_ABSENT);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(message.inline_struct.l0_inline.l1_inline.l2_absent), FIDL_ALLOC_ABSENT);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(message.inline_struct.l0_inline.l1_inline.l2_inline.l3_absent), FIDL_ALLOC_ABSENT);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(message.in_in_out_2.l3_absent), FIDL_ALLOC_ABSENT);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(message.in_out_1.l2_absent), FIDL_ALLOC_ABSENT);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(message.in_out_1.l2_inline.l3_absent), FIDL_ALLOC_ABSENT);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(message.in_out_out_2.l3_absent), FIDL_ALLOC_ABSENT);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(message.out_0.l1_absent), FIDL_ALLOC_ABSENT);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(message.out_0.l1_inline.l2_absent), FIDL_ALLOC_ABSENT);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(message.out_0.l1_inline.l2_inline.l3_absent), FIDL_ALLOC_ABSENT);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(message.out_in_out_2.l3_absent), FIDL_ALLOC_ABSENT);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(message.out_out_1.l2_absent), FIDL_ALLOC_ABSENT);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(message.out_out_1.l2_inline.l3_absent), FIDL_ALLOC_ABSENT);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(message.out_out_out_2.l3_absent), FIDL_ALLOC_ABSENT);

    END_TEST;
}

bool encode_nested_struct_recursion_too_deep_error() {
    BEGIN_TEST;

    struct level_34 {
        zx_handle_t handle = dummy_handle_0;
    };
    struct level_33 {
        level_34 l34;
    };
    struct level_32 {
        level_33 l33;
    };
    struct level_31 {
        level_32 l32;
    };
    struct level_30 {
        level_31 l31;
    };
    struct level_29 {
        level_30 l30;
    };
    struct level_28 {
        level_29 l29;
    };
    struct level_27 {
        level_28 l28;
    };
    struct level_26 {
        level_27 l27;
    };
    struct level_25 {
        level_26 l26;
    };
    struct level_24 {
        level_25 l25;
    };
    struct level_23 {
        level_24 l24;
    };
    struct level_22 {
        level_23 l23;
    };
    struct level_21 {
        level_22 l22;
    };
    struct level_20 {
        level_21 l21;
    };
    struct level_19 {
        level_20 l20;
    };
    struct level_18 {
        level_19 l19;
    };
    struct level_17 {
        level_18 l18;
    };
    struct level_16 {
        level_17 l17;
    };
    struct level_15 {
        level_16 l16;
    };
    struct level_14 {
        level_15 l15;
    };
    struct level_13 {
        level_14 l14;
    };
    struct level_12 {
        level_13 l13;
    };
    struct level_11 {
        level_12 l12;
    };
    struct level_10 {
        level_11 l11;
    };
    struct level_9 {
        level_10 l10;
    };
    struct level_8 {
        level_9 l9;
    };
    struct level_7 {
        level_8 l8;
    };
    struct level_6 {
        level_7 l7;
    };
    struct level_5 {
        level_6 l6;
    };
    struct level_4 {
        level_5 l5;
    };
    struct level_3 {
        level_4 l4;
    };
    struct level_2 {
        level_3 l3;
    };
    struct level_1 {
        level_2 l2;
    };
    struct level_0 {
        level_1 l1;
    };

    struct inline_data {
        fidl_message_header_t header = {};
        level_0 l0;
    };
    struct message_layout {
        inline_data inline_struct;
    } message;

    const FidlField level_34_fields[] = {
        FidlField(&kSingleHandleType, offsetof(level_34, handle)),
    };
    const fidl_type level_34_struct = fidl_type(FidlCodedStruct(level_34_fields, ArrayCount(level_34_fields), sizeof(level_34)));

    const FidlField level_33_fields[] = {
        FidlField(&level_34_struct, offsetof(level_33, l34)),
    };
    const fidl_type level_33_struct = fidl_type(FidlCodedStruct(level_33_fields, ArrayCount(level_33_fields), sizeof(level_33)));
    const FidlField level_32_fields[] = {
        FidlField(&level_33_struct, offsetof(level_32, l33)),
    };
    const fidl_type level_32_struct = fidl_type(FidlCodedStruct(level_32_fields, ArrayCount(level_32_fields), sizeof(level_32)));
    const FidlField level_31_fields[] = {
        FidlField(&level_32_struct, offsetof(level_31, l32)),
    };
    const fidl_type level_31_struct = fidl_type(FidlCodedStruct(level_31_fields, ArrayCount(level_31_fields), sizeof(level_31)));
    const FidlField level_30_fields[] = {
        FidlField(&level_31_struct, offsetof(level_30, l31)),
    };
    const fidl_type level_30_struct = fidl_type(FidlCodedStruct(level_30_fields, ArrayCount(level_30_fields), sizeof(level_30)));
    const FidlField level_29_fields[] = {
        FidlField(&level_30_struct, offsetof(level_29, l30)),
    };
    const fidl_type level_29_struct = fidl_type(FidlCodedStruct(level_29_fields, ArrayCount(level_29_fields), sizeof(level_29)));
    const FidlField level_28_fields[] = {
        FidlField(&level_29_struct, offsetof(level_28, l29)),
    };
    const fidl_type level_28_struct = fidl_type(FidlCodedStruct(level_28_fields, ArrayCount(level_28_fields), sizeof(level_28)));
    const FidlField level_27_fields[] = {
        FidlField(&level_28_struct, offsetof(level_27, l28)),
    };
    const fidl_type level_27_struct = fidl_type(FidlCodedStruct(level_27_fields, ArrayCount(level_27_fields), sizeof(level_27)));
    const FidlField level_26_fields[] = {
        FidlField(&level_27_struct, offsetof(level_26, l27)),
    };
    const fidl_type level_26_struct = fidl_type(FidlCodedStruct(level_26_fields, ArrayCount(level_26_fields), sizeof(level_26)));
    const FidlField level_25_fields[] = {
        FidlField(&level_26_struct, offsetof(level_25, l26)),
    };
    const fidl_type level_25_struct = fidl_type(FidlCodedStruct(level_25_fields, ArrayCount(level_25_fields), sizeof(level_25)));
    const FidlField level_24_fields[] = {
        FidlField(&level_25_struct, offsetof(level_24, l25)),
    };
    const fidl_type level_24_struct = fidl_type(FidlCodedStruct(level_24_fields, ArrayCount(level_24_fields), sizeof(level_24)));
    const FidlField level_23_fields[] = {
        FidlField(&level_24_struct, offsetof(level_23, l24)),
    };
    const fidl_type level_23_struct = fidl_type(FidlCodedStruct(level_23_fields, ArrayCount(level_23_fields), sizeof(level_23)));
    const FidlField level_22_fields[] = {
        FidlField(&level_23_struct, offsetof(level_22, l23)),
    };
    const fidl_type level_22_struct = fidl_type(FidlCodedStruct(level_22_fields, ArrayCount(level_22_fields), sizeof(level_22)));
    const FidlField level_21_fields[] = {
        FidlField(&level_22_struct, offsetof(level_21, l22)),
    };
    const fidl_type level_21_struct = fidl_type(FidlCodedStruct(level_21_fields, ArrayCount(level_21_fields), sizeof(level_21)));
    const FidlField level_20_fields[] = {
        FidlField(&level_21_struct, offsetof(level_20, l21)),
    };
    const fidl_type level_20_struct = fidl_type(FidlCodedStruct(level_20_fields, ArrayCount(level_20_fields), sizeof(level_20)));
    const FidlField level_19_fields[] = {
        FidlField(&level_20_struct, offsetof(level_19, l20)),
    };
    const fidl_type level_19_struct = fidl_type(FidlCodedStruct(level_19_fields, ArrayCount(level_19_fields), sizeof(level_19)));
    const FidlField level_18_fields[] = {
        FidlField(&level_19_struct, offsetof(level_18, l19)),
    };
    const fidl_type level_18_struct = fidl_type(FidlCodedStruct(level_18_fields, ArrayCount(level_18_fields), sizeof(level_18)));
    const FidlField level_17_fields[] = {
        FidlField(&level_18_struct, offsetof(level_17, l18)),
    };
    const fidl_type level_17_struct = fidl_type(FidlCodedStruct(level_17_fields, ArrayCount(level_17_fields), sizeof(level_17)));
    const FidlField level_16_fields[] = {
        FidlField(&level_17_struct, offsetof(level_16, l17)),
    };
    const fidl_type level_16_struct = fidl_type(FidlCodedStruct(level_16_fields, ArrayCount(level_16_fields), sizeof(level_16)));
    const FidlField level_15_fields[] = {
        FidlField(&level_16_struct, offsetof(level_15, l16)),
    };
    const fidl_type level_15_struct = fidl_type(FidlCodedStruct(level_15_fields, ArrayCount(level_15_fields), sizeof(level_15)));
    const FidlField level_14_fields[] = {
        FidlField(&level_15_struct, offsetof(level_14, l15)),
    };
    const fidl_type level_14_struct = fidl_type(FidlCodedStruct(level_14_fields, ArrayCount(level_14_fields), sizeof(level_14)));
    const FidlField level_13_fields[] = {
        FidlField(&level_14_struct, offsetof(level_13, l14)),
    };
    const fidl_type level_13_struct = fidl_type(FidlCodedStruct(level_13_fields, ArrayCount(level_13_fields), sizeof(level_13)));
    const FidlField level_12_fields[] = {
        FidlField(&level_13_struct, offsetof(level_12, l13)),
    };
    const fidl_type level_12_struct = fidl_type(FidlCodedStruct(level_12_fields, ArrayCount(level_12_fields), sizeof(level_12)));
    const FidlField level_11_fields[] = {
        FidlField(&level_12_struct, offsetof(level_11, l12)),
    };
    const fidl_type level_11_struct = fidl_type(FidlCodedStruct(level_11_fields, ArrayCount(level_11_fields), sizeof(level_11)));
    const FidlField level_10_fields[] = {
        FidlField(&level_11_struct, offsetof(level_10, l11)),
    };
    const fidl_type level_10_struct = fidl_type(FidlCodedStruct(level_10_fields, ArrayCount(level_10_fields), sizeof(level_10)));
    const FidlField level_9_fields[] = {
        FidlField(&level_10_struct, offsetof(level_9, l10)),
    };
    const fidl_type level_9_struct = fidl_type(FidlCodedStruct(level_9_fields, ArrayCount(level_9_fields), sizeof(level_9)));
    const FidlField level_8_fields[] = {
        FidlField(&level_9_struct, offsetof(level_8, l9)),
    };
    const fidl_type level_8_struct = fidl_type(FidlCodedStruct(level_8_fields, ArrayCount(level_8_fields), sizeof(level_8)));
    const FidlField level_7_fields[] = {
        FidlField(&level_8_struct, offsetof(level_7, l8)),
    };
    const fidl_type level_7_struct = fidl_type(FidlCodedStruct(level_7_fields, ArrayCount(level_7_fields), sizeof(level_7)));
    const FidlField level_6_fields[] = {
        FidlField(&level_7_struct, offsetof(level_6, l7)),
    };
    const fidl_type level_6_struct = fidl_type(FidlCodedStruct(level_6_fields, ArrayCount(level_6_fields), sizeof(level_6)));
    const FidlField level_5_fields[] = {
        FidlField(&level_6_struct, offsetof(level_5, l6)),
    };
    const fidl_type level_5_struct = fidl_type(FidlCodedStruct(level_5_fields, ArrayCount(level_5_fields), sizeof(level_5)));
    const FidlField level_4_fields[] = {
        FidlField(&level_5_struct, offsetof(level_4, l5)),
    };
    const fidl_type level_4_struct = fidl_type(FidlCodedStruct(level_4_fields, ArrayCount(level_4_fields), sizeof(level_4)));
    const FidlField level_3_fields[] = {
        FidlField(&level_4_struct, offsetof(level_3, l4)),
    };
    const fidl_type level_3_struct = fidl_type(FidlCodedStruct(level_3_fields, ArrayCount(level_3_fields), sizeof(level_3)));
    const FidlField level_2_fields[] = {
        FidlField(&level_3_struct, offsetof(level_2, l3)),
    };
    const fidl_type level_2_struct = fidl_type(FidlCodedStruct(level_2_fields, ArrayCount(level_2_fields), sizeof(level_2)));
    const FidlField level_1_fields[] = {
        FidlField(&level_2_struct, offsetof(level_1, l2)),
    };
    const fidl_type level_1_struct = fidl_type(FidlCodedStruct(level_1_fields, ArrayCount(level_1_fields), sizeof(level_1)));
    const FidlField level_0_fields[] = {
        FidlField(&level_1_struct, offsetof(level_0, l1)),
    };
    const fidl_type level_0_struct = fidl_type(FidlCodedStruct(level_0_fields, ArrayCount(level_0_fields), sizeof(level_0)));

    const FidlField fields[] = {
        FidlField(
            &level_0_struct, offsetof(decltype(message), inline_struct.l0)),

    };
    const fidl_type message_type =
        fidl_type(FidlCodedStruct(fields,
                                  ArrayCount(fields),
                                  sizeof(inline_data)));

    zx_handle_t handles[1] = {};

    const char* error = nullptr;
    uint32_t actual_handles = 0u;
    auto status = fidl_encode(&message_type, &message, sizeof(message),
                              handles, ArrayCount(handles), &actual_handles, &error);

    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_NONNULL(error);

    END_TEST;
}

BEGIN_TEST_CASE(null_parameters)
RUN_TEST(encode_null_encode_parameters)
END_TEST_CASE(null_parameters)

BEGIN_TEST_CASE(handles)
RUN_TEST(encode_single_present_handle)
RUN_TEST(encode_multiple_present_handles)
RUN_TEST(encode_single_absent_handle)
RUN_TEST(encode_multiple_absent_handles)
END_TEST_CASE(handles)

BEGIN_TEST_CASE(arrays)
RUN_TEST(encode_array_of_present_handles)
RUN_TEST(encode_array_of_nullable_handles)
RUN_TEST(encode_array_of_nullable_handles_with_insufficient_handles_error)
RUN_TEST(encode_array_of_array_of_present_handles)
RUN_TEST(encode_out_of_line_array)
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
