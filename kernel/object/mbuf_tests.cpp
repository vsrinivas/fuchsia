// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/mbuf.h>

#include <fbl/unique_ptr.h>
#include <lib/unittest/unittest.h>
#include <lib/unittest/user_memory.h>

namespace {

using testing::UserMemory;

static bool initial_state() {
    BEGIN_TEST;
    MBufChain chain;
    EXPECT_TRUE(chain.is_empty(), "");
    EXPECT_FALSE(chain.is_full(), "");
    EXPECT_EQ(0U, chain.size(), "");
    END_TEST;
}

// Tests reading a stream when the chain is empty.
static bool stream_read_empty() {
    BEGIN_TEST;
    fbl::unique_ptr<UserMemory> mem = UserMemory::Create(1);
    auto mem_out = make_user_out_ptr(mem->out());

    MBufChain chain;
    EXPECT_EQ(0U, chain.Read(mem_out, 1, false), "");
    END_TEST;
}

// Tests reading a stream with a zero-length buffer.
static bool stream_read_zero() {
    BEGIN_TEST;
    fbl::unique_ptr<UserMemory> mem = UserMemory::Create(1);
    auto mem_in = make_user_in_ptr(mem->in());
    auto mem_out = make_user_out_ptr(mem->out());

    MBufChain chain;
    size_t written = 7;
    ASSERT_EQ(ZX_OK, chain.WriteStream(mem_in, 1, &written), "");
    ASSERT_EQ(1U, written, "");

    EXPECT_EQ(0U, chain.Read(mem_out, 0, false), "");
    END_TEST;
}

// Tests basic WriteStream/Read functionality.
static bool stream_write_basic() {
    BEGIN_TEST;
    constexpr size_t kWriteLen = 1024;
    constexpr int kNumWrites = 5;

    fbl::unique_ptr<UserMemory> mem = UserMemory::Create(kWriteLen);
    auto mem_in = make_user_in_ptr(mem->in());
    auto mem_out = make_user_out_ptr(mem->out());

    size_t written = 0;
    MBufChain chain;
    // Call write several times with different buffer contents.
    for (int i = 0; i < kNumWrites; ++i) {
        char buf[kWriteLen] = {0};
        memset(buf, 'A' + i, kWriteLen);
        ASSERT_EQ(ZX_OK, mem_out.copy_array_to_user(buf, kWriteLen), "");
        ASSERT_EQ(ZX_OK, chain.WriteStream(mem_in, kWriteLen, &written), "");
        ASSERT_EQ(kWriteLen, written, "");
        EXPECT_FALSE(chain.is_empty(), "");
        EXPECT_FALSE(chain.is_full(), "");
        EXPECT_EQ((i + 1) * kWriteLen, chain.size(), "");
    }

    // Read it all back in one call.
    constexpr size_t kTotalLen = kWriteLen * kNumWrites;
    ASSERT_EQ(kTotalLen, chain.size(), "");
    fbl::unique_ptr<UserMemory> read_buf = UserMemory::Create(kTotalLen);
    auto read_buf_in = make_user_in_ptr(read_buf->in());
    auto read_buf_out = make_user_out_ptr(read_buf->out());
    size_t result = chain.Read(read_buf_out, kTotalLen, false);
    ASSERT_EQ(kTotalLen, result, "");
    EXPECT_TRUE(chain.is_empty(), "");
    EXPECT_FALSE(chain.is_full(), "");
    EXPECT_EQ(0U, chain.size(), "");

    // Verify result.
    fbl::AllocChecker ac;
    auto expected_buf = fbl::unique_ptr<char[]>(new (&ac) char[kTotalLen]);
    ASSERT_TRUE(ac.check(), "");
    for (int i = 0; i < kNumWrites; ++i) {
        memset(static_cast<void*>(expected_buf.get() + i * kWriteLen), 'A' + i, kWriteLen);
    }
    auto actual_buf = fbl::unique_ptr<char[]>(new (&ac) char[kTotalLen]);
    ASSERT_TRUE(ac.check(), "");
    ASSERT_EQ(ZX_OK, read_buf_in.copy_array_from_user(actual_buf.get(), kTotalLen), "");
    EXPECT_EQ(0, memcmp(static_cast<void*>(expected_buf.get()),
                        static_cast<void*>(actual_buf.get()), kTotalLen), "");
    END_TEST;
}

// Tests writing a stream with a zero-length buffer.
static bool stream_write_zero() {
    BEGIN_TEST;
    fbl::unique_ptr<UserMemory> mem = UserMemory::Create(1);
    auto mem_in = make_user_in_ptr(mem->in());
    size_t written = 7;
    MBufChain chain;
    // TODO(maniscalco): Is ZX_ERR_SHOULD_WAIT really the right error here in this case?
    EXPECT_EQ(ZX_ERR_SHOULD_WAIT, chain.WriteStream(mem_in, 0, &written), "");
    EXPECT_EQ(7U, written, "");
    EXPECT_TRUE(chain.is_empty(), "");
    EXPECT_FALSE(chain.is_full(), "");
    EXPECT_EQ(0U, chain.size(), "");
    END_TEST;
}

// Tests writing a stream to the chain until it stops accepting writes.
static bool stream_write_too_much() {
    BEGIN_TEST;
    constexpr size_t kWriteLen = 65536;
    fbl::unique_ptr<UserMemory> mem = UserMemory::Create(kWriteLen);
    auto mem_in = make_user_in_ptr(mem->in());
    auto mem_out = make_user_out_ptr(mem->out());
    size_t written = 0;
    MBufChain chain;
    size_t total_written = 0;

    // Fill the chain until it refuses to take any more.
    while (!chain.is_full() && chain.WriteStream(mem_in, kWriteLen, &written) == ZX_OK) {
        total_written += written;
    }
    ASSERT_FALSE(chain.is_empty(), "");
    ASSERT_TRUE(chain.is_full(), "");
    EXPECT_EQ(total_written, chain.size(), "");

    // Read it all back out and see we get back the same number of bytes we wrote.
    size_t total_read = 0;
    size_t bytes_read = 0;
    while (!chain.is_empty() && (bytes_read = chain.Read(mem_out, kWriteLen, false)) > 0) {
        total_read += bytes_read;
    }
    EXPECT_TRUE(chain.is_empty(), "");
    EXPECT_EQ(0U, chain.size(), "");
    EXPECT_EQ(total_written, total_read, "");
    END_TEST;
}

// Tests reading a datagram when chain is empty.
static bool datagram_read_empty() {
    BEGIN_TEST;
    fbl::unique_ptr<UserMemory> mem = UserMemory::Create(1);
    auto mem_out = make_user_out_ptr(mem->out());

    MBufChain chain;
    EXPECT_EQ(0U, chain.Read(mem_out, 1, true), "");
    EXPECT_TRUE(chain.is_empty(), "");
    END_TEST;
}

// Tests reading a datagram with a zero-length buffer.
static bool datagram_read_zero() {
    BEGIN_TEST;
    fbl::unique_ptr<UserMemory> mem = UserMemory::Create(1);
    auto mem_in = make_user_in_ptr(mem->in());
    auto mem_out = make_user_out_ptr(mem->out());

    MBufChain chain;
    size_t written = 7;
    ASSERT_EQ(ZX_OK, chain.WriteDatagram(mem_in, 1, &written), "");
    ASSERT_EQ(1U, written, "");
    EXPECT_EQ(0U, chain.Read(mem_out, 0, true), "");
    EXPECT_FALSE(chain.is_empty(), "");
    END_TEST;
}

// Tests reading a datagram into a buffer that's too small.
static bool datagram_read_buffer_too_small() {
    BEGIN_TEST;
    constexpr size_t kWriteLen = 32;
    fbl::unique_ptr<UserMemory> mem = UserMemory::Create(kWriteLen);
    auto mem_in = make_user_in_ptr(mem->in());
    auto mem_out = make_user_out_ptr(mem->out());
    size_t written = 0;
    MBufChain chain;

    // Write the 'A' datagram.
    char buf[kWriteLen] = {0};
    memset(buf, 'A', sizeof(buf));
    ASSERT_EQ(ZX_OK, mem_out.copy_array_to_user(buf, sizeof(buf)), "");
    ASSERT_EQ(ZX_OK, chain.WriteDatagram(mem_in, kWriteLen, &written), "");
    ASSERT_EQ(kWriteLen, written, "");
    EXPECT_EQ(kWriteLen, chain.size(), "");
    ASSERT_FALSE(chain.is_empty(), "");

    // Write the 'B' datagram.
    memset(buf, 'B', sizeof(buf));
    ASSERT_EQ(ZX_OK, mem_out.copy_array_to_user(buf, sizeof(buf)), "");
    ASSERT_EQ(ZX_OK, chain.WriteDatagram(mem_in, kWriteLen, &written), "");
    ASSERT_EQ(kWriteLen, written, "");
    EXPECT_EQ(2 * kWriteLen, chain.size(), "");
    ASSERT_FALSE(chain.is_empty(), "");

    // Read back the first datagram, but with a buffer that's too small.  See that we get back a
    // truncated 'A' datagram.
    memset(buf, 0, sizeof(buf));
    ASSERT_EQ(ZX_OK, mem_out.copy_array_to_user(buf, sizeof(buf)), "");
    EXPECT_EQ(1U, chain.Read(mem_out, 1, true), "");
    EXPECT_FALSE(chain.is_empty(), "");
    ASSERT_EQ(ZX_OK, mem_in.copy_array_from_user(buf, sizeof(buf)), "");
    EXPECT_EQ('A', buf[0], "");
    EXPECT_EQ(0, buf[1], "");

    // Read the next one and see that it's 'B' implying the remainder of 'A' was discarded.
    EXPECT_EQ(kWriteLen, chain.size(), "");
    memset(buf, 0, kWriteLen);
    ASSERT_EQ(ZX_OK, mem_out.copy_array_to_user(buf, sizeof(buf)), "");
    EXPECT_EQ(kWriteLen, chain.Read(mem_out, kWriteLen, true), "");
    EXPECT_TRUE(chain.is_empty(), "");
    EXPECT_EQ(0U, chain.size(), "");
    ASSERT_EQ(ZX_OK, mem_in.copy_array_from_user(buf, sizeof(buf)), "");
    char expected_buf[kWriteLen] = {0};
    memset(expected_buf, 'B', kWriteLen);
    EXPECT_EQ(0, memcmp(expected_buf, buf, kWriteLen), "");
    END_TEST;
}

// Tests basic WriteDatagram/Read functionality.
static bool datagram_write_basic() {
    BEGIN_TEST;
    constexpr int kNumDatagrams = 100;
    constexpr size_t kMaxLength = kNumDatagrams;
    size_t written = 0;

    fbl::unique_ptr<UserMemory> mem = UserMemory::Create(kMaxLength);
    auto mem_in = make_user_in_ptr(mem->in());
    auto mem_out = make_user_out_ptr(mem->out());

    MBufChain chain;
    // Write a series of datagrams with different sizes.
    for (unsigned i = 1; i <= kNumDatagrams; ++i) {
        char buf[kMaxLength] = {0};
        memset(buf, i, i);
        ASSERT_EQ(ZX_OK, mem_out.copy_array_to_user(buf, sizeof(buf)), "");
        ASSERT_EQ(ZX_OK, chain.WriteDatagram(mem_in, i, &written), "");
        ASSERT_EQ(i, written, "");
        EXPECT_FALSE(chain.is_empty(), "");
        EXPECT_FALSE(chain.is_full(), "");
    }

    // Read them back and verify their contents.
    for (unsigned i = 1; i <= kNumDatagrams; ++i) {
        char expected_buf[kMaxLength] = {0};
        memset(expected_buf, i, i);
        size_t result = chain.Read(mem_out, i, true);
        ASSERT_EQ(i, result, "");
        char actual_buf[kMaxLength] = {0};
        ASSERT_EQ(ZX_OK, mem_in.copy_array_from_user(actual_buf, sizeof(actual_buf)), "");
        EXPECT_EQ(0, memcmp(expected_buf, actual_buf, i), "");
    }
    EXPECT_TRUE(chain.is_empty(), "");
    EXPECT_EQ(0U, chain.size(), "");
    END_TEST;
}

// Tests writing a zero-length datagram to the chain.
static bool datagram_write_zero() {
    BEGIN_TEST;
    fbl::unique_ptr<UserMemory> mem = UserMemory::Create(1);
    auto mem_in = make_user_in_ptr(mem->in());

    size_t written = 7;
    MBufChain chain;
    EXPECT_EQ(ZX_ERR_INVALID_ARGS, chain.WriteDatagram(mem_in, 0, &written), "");
    EXPECT_EQ(7U, written, "");
    EXPECT_TRUE(chain.is_empty(), "");
    EXPECT_FALSE(chain.is_full(), "");
    EXPECT_EQ(0U, chain.size(), "");
    END_TEST;
}

// Tests writing datagrams to the chain until it stops accepting writes.
static bool datagram_write_too_much() {
    BEGIN_TEST;
    constexpr size_t kWriteLen = 65536;
    fbl::unique_ptr<UserMemory> mem = UserMemory::Create(kWriteLen);
    auto mem_in = make_user_in_ptr(mem->in());
    auto mem_out = make_user_out_ptr(mem->out());

    size_t written = 0;
    MBufChain chain;
    int num_datagrams_written = 0;
    // Fill the chain until it refuses to take any more.
    while (!chain.is_full() && chain.WriteDatagram(mem_in, kWriteLen, &written) == ZX_OK) {
        ++num_datagrams_written;
        ASSERT_EQ(kWriteLen, written, "");
    }
    ASSERT_FALSE(chain.is_empty(), "");
    EXPECT_EQ(kWriteLen * num_datagrams_written, chain.size(), "");
    // Read it all back out and see that there's none left over.
    int num_datagrams_read = 0;
    while (!chain.is_empty() && chain.Read(mem_out, kWriteLen, true) > 0) {
        ++num_datagrams_read;
    }
    EXPECT_TRUE(chain.is_empty(), "");
    EXPECT_EQ(0U, chain.size(), "");
    EXPECT_EQ(num_datagrams_written, num_datagrams_read, "");
    END_TEST;
}

} // namespace

UNITTEST_START_TESTCASE(mbuf_tests)
UNITTEST("initial_state", initial_state)
UNITTEST("stream_read_empty", stream_read_empty)
UNITTEST("stream_read_zero", stream_read_zero)
UNITTEST("stream_write_basic", stream_write_basic)
UNITTEST("stream_write_zero", stream_write_zero)
UNITTEST("stream_write_too_much", stream_write_too_much)
UNITTEST("datagram_read_empty", datagram_read_empty)
UNITTEST("datagram_read_zero", datagram_read_zero)
UNITTEST("datagram_read_buffer_too_small", datagram_read_buffer_too_small)
UNITTEST("datagram_write_basic", datagram_write_basic)
UNITTEST("datagram_write_zero", datagram_write_zero)
UNITTEST("datagram_write_too_much", datagram_write_too_much)
UNITTEST_END_TESTCASE(mbuf_tests, "mbuf", "MBuf test");
