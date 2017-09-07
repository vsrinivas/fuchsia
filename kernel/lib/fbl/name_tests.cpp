// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <fbl/name.h>

#include <unittest.h>

namespace {

constexpr char fill = 0x7f;

bool buffer_invariants_hold(const char* buf, size_t size) {
    // The buffer should start with zero or more non-NUL bytes followed
    // by a NUL.
    unsigned int idx = 0;
    while (buf[idx] != 0) {
        idx++;
        if (idx == size) {
            unittest_printf("No NUL byte found\n");
            return false;
        }
    }
    idx++; // Skip the NUL
    // The rest of the buffer should be filled with the |fill| byte.
    while (idx < size) {
        if (buf[idx] != fill) {
            unittest_printf(
                "buf[%d] 0x%02x != fill 0x%02x\n", idx, buf[idx], fill);
            return false;
        }
        idx++;
    }
    return true;
}

template <size_t Size>
bool empty_ctor(void* context) {
    BEGIN_TEST;

    // Note on |out| sizes: most tests use Size * 2 to ensure the out buffer is
    // more than big enough to read the entire name with room to spare.
    char out[Size * 2];
    memset(out, fill, sizeof(out));

    fbl::Name<Size> name;
    name.get(sizeof(out), out);

    EXPECT_EQ(out[0], 0, "");
    EXPECT_TRUE(buffer_invariants_hold(out, sizeof(out)), "");

    END_TEST;
}

template <size_t Size>
bool named_ctor_empty(void* context) {
    BEGIN_TEST;

    char out[Size * 2];
    memset(out, fill, sizeof(out));

    fbl::Name<Size> name("", 1);
    name.get(sizeof(out), out);

    EXPECT_EQ(out[0], 0, "");
    EXPECT_TRUE(buffer_invariants_hold(out, sizeof(out)), "");

    END_TEST;
}

template <size_t Size>
bool named_ctor_small(void* context) {
    BEGIN_TEST;

    char out[Size * 2];
    memset(out, fill, sizeof(out));

    fbl::Name<Size> name("a", 2);
    name.get(sizeof(out), out);

    EXPECT_EQ(out[0], 'a', "");
    EXPECT_EQ(out[1], 0, "");
    EXPECT_TRUE(buffer_invariants_hold(out, sizeof(out)), "");

    END_TEST;
}

template <size_t Size>
bool named_ctor_exact(void* context) {
    BEGIN_TEST;

    char out[Size * 2];
    memset(out, fill, sizeof(out));

    char expected_name[Size];
    memset(expected_name, 'z', Size - 1);
    expected_name[Size - 1] = 0;

    fbl::Name<Size> name(expected_name, Size);
    name.get(sizeof(out), out);

    for (size_t idx = 0; idx < Size - 1; ++idx) {
        EXPECT_EQ(out[idx], 'z', "");
    }
    EXPECT_EQ(out[Size - 1], 0, "");
    EXPECT_TRUE(buffer_invariants_hold(out, sizeof(out)), "");

    END_TEST;
}

template <size_t Size>
bool named_ctor_overflow(void* context) {
    BEGIN_TEST;

    constexpr size_t overflow_size = 2 * Size;

    char out[overflow_size * 2];
    memset(out, fill, sizeof(out));

    char expected_name[overflow_size];
    memset(expected_name, 'z', overflow_size - 1);
    expected_name[overflow_size - 1] = 0;

    fbl::Name<Size> name(expected_name, overflow_size);
    name.get(sizeof(out), out);

    for (size_t idx = 0; idx < Size - 1; ++idx) {
        EXPECT_EQ(out[idx], 'z', "");
    }
    EXPECT_EQ(out[Size - 1], 0, "");
    EXPECT_TRUE(buffer_invariants_hold(out, sizeof(out)), "");

    END_TEST;
}

template <size_t Size>
bool zero_sized_output_buffer(void* context) {
    BEGIN_TEST;

    // Neither of these bytes should be touched.
    char out[2] = {fill, fill};

    fbl::Name<Size> name("a", 2);
    name.get(0, out);

    EXPECT_EQ(out[0], fill, "");
    EXPECT_EQ(out[1], fill, "");

    END_TEST;
}

template <size_t NameSize, size_t OutSize>
bool output_buffer_size(void* context) {
    static_assert(OutSize > 0, ""); // |OutSize - 1| below would fail.

    BEGIN_TEST;

    // Longest name possible.
    char expected_name[NameSize];
    memset(expected_name, 'z', NameSize - 1);
    expected_name[NameSize - 1] = 0;

    char out[OutSize + 2]; // Extra fill at the end.
    memset(out, fill, sizeof(out));

    fbl::Name<NameSize> name(expected_name, sizeof(expected_name));
    name.get(OutSize, out);

    // Check that the name fits in the size we passed to Name::get().
    for (size_t idx = 0; idx < OutSize - 1; ++idx) {
        char msg[32];
        snprintf(msg, sizeof(msg), "idx=%zu", idx);
        EXPECT_EQ(out[idx], 'z', msg);
    }
    EXPECT_EQ(out[OutSize - 1], 0, "");

    // Check that the extra fill is intact.
    EXPECT_TRUE(buffer_invariants_hold(out, sizeof(out)), "");

    END_TEST;
}

// Test the smallest size and a typical size.
constexpr size_t kSmallestNameSize = 2;
constexpr size_t kTypicalNameSize = 32;

} // namespace

#define NAME_UNITTEST(fname) UNITTEST(#fname, fname)

UNITTEST_START_TESTCASE(name_tests)

NAME_UNITTEST(empty_ctor<kSmallestNameSize>)
NAME_UNITTEST(empty_ctor<kTypicalNameSize>)

NAME_UNITTEST(named_ctor_empty<kSmallestNameSize>)
NAME_UNITTEST(named_ctor_empty<kTypicalNameSize>)

NAME_UNITTEST(named_ctor_small<kSmallestNameSize>)
NAME_UNITTEST(named_ctor_small<kTypicalNameSize>)

NAME_UNITTEST(named_ctor_exact<kSmallestNameSize>)
NAME_UNITTEST(named_ctor_exact<kTypicalNameSize>)

NAME_UNITTEST(named_ctor_overflow<kSmallestNameSize>)
NAME_UNITTEST(named_ctor_overflow<kTypicalNameSize>)

NAME_UNITTEST(zero_sized_output_buffer<kSmallestNameSize>)
NAME_UNITTEST(zero_sized_output_buffer<kTypicalNameSize>)

// Output buffer only contains NUL.
NAME_UNITTEST((output_buffer_size<kTypicalNameSize, 1>))

// Smallest useful output buffer.
NAME_UNITTEST((output_buffer_size<kTypicalNameSize, 2>))

// Edge cases around exactly matching the Name length.
NAME_UNITTEST((output_buffer_size<kTypicalNameSize, kTypicalNameSize - 2>))
NAME_UNITTEST((output_buffer_size<kTypicalNameSize, kTypicalNameSize - 1>))
NAME_UNITTEST((output_buffer_size<kTypicalNameSize, kTypicalNameSize>))
// Don't bother testing a larger output buffer, since most of the
// earlier tests use larger output buffers.

UNITTEST_END_TESTCASE(name_tests, "nametests", "Name test", nullptr, nullptr);
