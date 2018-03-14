// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/mbuf.h>

#include <lib/user_copy/fake_user_ptr.h>
#include <fbl/unique_ptr.h>
#include <unittest.h>

using internal::testing::make_fake_user_out_ptr;
using internal::testing::make_fake_user_in_ptr;

namespace {

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
    char buf[1] = {0};
    MBufChain chain;
    auto dst = make_fake_user_out_ptr(static_cast<void*>(buf));
    EXPECT_EQ(0U, chain.Read(dst, sizeof(buf), false), "");
    END_TEST;
}

// Tests reading a stream with a zero-length buffer.
static bool stream_read_zero() {
    BEGIN_TEST;
    char buf[1] = {'A'};
    auto src = make_fake_user_in_ptr(static_cast<const void*>(buf));
    MBufChain chain;
    size_t written = 7;
    ASSERT_EQ(ZX_OK, chain.WriteStream(src, 1, &written), "");
    ASSERT_EQ(1U, written, "");
    auto dst = make_fake_user_out_ptr(static_cast<void*>(buf));
    EXPECT_EQ(0U, chain.Read(dst, 0, false), "");
    END_TEST;
}

// Tests basic WriteStream/Read functionality.
static bool stream_write_basic() {
    BEGIN_TEST;
    constexpr size_t kWriteLen = 1024;
    constexpr int kNumWrites = 5;
    char buf[kWriteLen] = {0};
    auto src = make_fake_user_in_ptr(static_cast<const void*>(buf));

    size_t written = 0;
    MBufChain chain;
    // Call write several times with different buffer contents.
    for (int i = 0; i < kNumWrites; ++i) {
        memset(buf, 'A' + i, kWriteLen);
        ASSERT_EQ(ZX_OK, chain.WriteStream(src, kWriteLen, &written), "");
        ASSERT_EQ(kWriteLen, written, "");
        EXPECT_FALSE(chain.is_empty(), "");
        EXPECT_FALSE(chain.is_full(), "");
        EXPECT_EQ((i + 1) * kWriteLen, chain.size(), "");
    }

    // Read it all back in one call.
    fbl::AllocChecker ac;
    auto read_buf = fbl::unique_ptr<char[]>(new (&ac) char[kWriteLen * kNumWrites]);
    ASSERT_TRUE(ac.check(), "");
    auto dst = make_fake_user_out_ptr(static_cast<void*>(read_buf.get()));
    size_t result = chain.Read(dst, kNumWrites * kWriteLen, false);
    ASSERT_EQ(kNumWrites * kWriteLen, result, "");
    EXPECT_TRUE(chain.is_empty(), "");
    EXPECT_FALSE(chain.is_full(), "");
    EXPECT_EQ(0U, chain.size(), "");

    // Verify result.
    auto expected_buf = fbl::unique_ptr<char[]>(new (&ac) char[kWriteLen * kNumWrites]);
    ASSERT_TRUE(ac.check(), "");
    for (int i = 0; i < kNumWrites; ++i) {
        memset(static_cast<void*>(expected_buf.get() + i * kWriteLen), 'A' + i, kWriteLen);
    }
    EXPECT_EQ(0, memcmp(static_cast<void*>(expected_buf.get()),
                        static_cast<void*>(read_buf.get()), kWriteLen), "");
    END_TEST;
}

// Tests writing a stream with a zero-length buffer.
static bool stream_write_zero() {
    BEGIN_TEST;
    char buf[1] = {0};
    auto src = make_fake_user_in_ptr(static_cast<const void*>(buf));
    size_t written = 7;
    MBufChain chain;
    // TODO(maniscalco): Is ZX_ERR_SHOULD_WAIT really the right error here in this case?
    EXPECT_EQ(ZX_ERR_SHOULD_WAIT, chain.WriteStream(src, 0, &written), "");
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
    fbl::AllocChecker ac;
    auto buf = fbl::unique_ptr<char[]>(new (&ac) char[kWriteLen]);
    ASSERT_TRUE(ac.check(), "");
    memset(static_cast<void*>(buf.get()), 'A', kWriteLen);
    auto src = make_fake_user_in_ptr(static_cast<const void*>(buf.get()));
    size_t written = 0;
    MBufChain chain;
    size_t total_written = 0;

    // Fill the chain until it refuses to take any more.
    while (!chain.is_full() && chain.WriteStream(src, kWriteLen, &written) == ZX_OK) {
        total_written += written;
    }
    ASSERT_FALSE(chain.is_empty(), "");
    ASSERT_TRUE(chain.is_full(), "");
    EXPECT_EQ(total_written, chain.size(), "");

    // Read it all back out and see we get back the same number of bytes we wrote.
    size_t total_read = 0;
    size_t bytes_read = 0;
    auto dst = make_fake_user_out_ptr(static_cast<void*>(buf.get()));
    while (!chain.is_empty() && (bytes_read = chain.Read(dst, kWriteLen, false)) > 0) {
        total_read += bytes_read;
    }
    EXPECT_TRUE(chain.is_empty(), "");
    EXPECT_EQ(0U, chain.size(), "");
    EXPECT_EQ(total_written, total_read, "");
    END_TEST;
}

// TODO(ZX-1847): Implemented a test that verifies behavior of calling ReadDatagram when MBufChain
// is empty.

// Tests reading a datagram with a zero-length buffer.
static bool datagram_read_zero() {
    BEGIN_TEST;
    char buf[1] = {'A'};
    auto src = make_fake_user_in_ptr(static_cast<const void*>(buf));
    MBufChain chain;
    size_t written = 7;
    ASSERT_EQ(ZX_OK, chain.WriteDatagram(src, 1, &written), "");
    ASSERT_EQ(1U, written, "");
    auto dst = make_fake_user_out_ptr(static_cast<void*>(buf));
    EXPECT_EQ(0U, chain.Read(dst, 0, true), "");
    EXPECT_FALSE(chain.is_empty(), "");
    END_TEST;
}

// Tests reading a datagram into a buffer that's too small.
static bool datagram_read_buffer_too_small() {
    BEGIN_TEST;
    constexpr size_t kWriteLen = 32;
    char buf[kWriteLen] = {0};
    size_t written = 0;
    MBufChain chain;
    auto src = make_fake_user_in_ptr(static_cast<const void*>(buf));

    // Write the 'A' datagram.
    memset(buf, 'A', kWriteLen);
    ASSERT_EQ(ZX_OK, chain.WriteDatagram(src, kWriteLen, &written), "");
    ASSERT_EQ(kWriteLen, written, "");
    EXPECT_EQ(kWriteLen, chain.size(), "");
    ASSERT_FALSE(chain.is_empty(), "");

    // Write the 'B' datagram.
    memset(buf, 'B', kWriteLen);
    ASSERT_EQ(ZX_OK, chain.WriteDatagram(src, kWriteLen, &written), "");
    ASSERT_EQ(kWriteLen, written, "");
    EXPECT_EQ(2 * kWriteLen, chain.size(), "");
    ASSERT_FALSE(chain.is_empty(), "");

    // Read back the first datagram, but with a buffer that's too small.  See that we get back a
    // truncated 'A' datagram.
    auto dst = make_fake_user_out_ptr(static_cast<void*>(buf));
    memset(buf, 0, kWriteLen);
    EXPECT_EQ(1U, chain.Read(dst, 1, true), "");
    EXPECT_FALSE(chain.is_empty(), "");
    EXPECT_EQ('A', buf[0],"");

    // Read the next one and see that it's 'B' implying the remainder of 'A' was discarded.
    EXPECT_EQ(kWriteLen, chain.size(), "");
    memset(buf, 0, kWriteLen);
    EXPECT_EQ(kWriteLen, chain.Read(dst, kWriteLen, true), "");
    EXPECT_TRUE(chain.is_empty(), "");
    EXPECT_EQ(0U, chain.size(), "");
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
    char buf[kMaxLength] = {0};
    auto src = make_fake_user_in_ptr(static_cast<const void*>(buf));

    MBufChain chain;
    // Write a series of datagrams with different sizes.
    for (unsigned i = 1; i <= kNumDatagrams; ++i) {
        memset(buf, i, i);
        ASSERT_EQ(ZX_OK, chain.WriteDatagram(src, i, &written), "");
        ASSERT_EQ(i, written, "");
        EXPECT_FALSE(chain.is_empty(), "");
        EXPECT_FALSE(chain.is_full(), "");
    }

    // Read them back and verify their contents.
    auto dst = make_fake_user_out_ptr(static_cast<void*>(buf));
    for (unsigned i = 1; i <= kNumDatagrams; ++i) {
        char expected_buf[kMaxLength] = {0};
        memset(expected_buf, i, i);
        size_t result = chain.Read(dst, i, true);
        ASSERT_EQ(i, result, "");
        EXPECT_EQ(0, memcmp(expected_buf, buf, i), "");
    }
    EXPECT_TRUE(chain.is_empty(), "");
    EXPECT_EQ(0U, chain.size(), "");
    END_TEST;
}

// TODO(ZX-1848): Implemented a test that verifies behavior of calling WriteDatagram with a
// zero-length buffer.

// Tests writing datagrams to the chain until it stops accepting writes.
static bool datagram_write_too_much() {
    BEGIN_TEST;
    constexpr size_t kWriteLen = 65536;
    fbl::AllocChecker ac;
    auto buf = fbl::unique_ptr<char[]>(new (&ac) char[kWriteLen]);
    ASSERT_TRUE(ac.check(), "");
    memset(static_cast<void*>(buf.get()), 'A', kWriteLen);
    auto src = make_fake_user_in_ptr(static_cast<const void*>(buf.get()));
    size_t written = 0;
    MBufChain chain;
    int num_datagrams_written = 0;
    // Fill the chain until it refuses to take any more.
    while (!chain.is_full() && chain.WriteDatagram(src, kWriteLen, &written) == ZX_OK) {
        ++num_datagrams_written;
        ASSERT_EQ(kWriteLen, written, "");
    }
    ASSERT_FALSE(chain.is_empty(), "");
    EXPECT_EQ(kWriteLen * num_datagrams_written, chain.size(), "");
    // Read it all back out and see that there's none left over.
    int num_datagrams_read = 0;
    auto dst = make_fake_user_out_ptr(static_cast<void*>(buf.get()));
    while (!chain.is_empty() && chain.Read(dst, kWriteLen, true) > 0) {
        ++num_datagrams_read;
    }
    EXPECT_TRUE(chain.is_empty(), "");
    EXPECT_EQ(0U, chain.size(), "");
    EXPECT_EQ(num_datagrams_written, num_datagrams_read, "");
    END_TEST;
}

}  // namespace

UNITTEST_START_TESTCASE(mbuf_tests)
UNITTEST("initial_state", initial_state)
UNITTEST("stream_read_empty", stream_read_empty)
UNITTEST("stream_read_zero", stream_read_zero)
UNITTEST("stream_write_basic", stream_write_basic)
UNITTEST("stream_write_zero", stream_write_zero)
UNITTEST("stream_write_too_much", stream_write_too_much)
UNITTEST("datagram_read_zero", datagram_read_zero)
UNITTEST("datagram_read_buffer_too_small", datagram_read_buffer_too_small)
UNITTEST("datagram_write_basic", datagram_write_basic)
UNITTEST("datagram_write_too_much", datagram_write_too_much)
UNITTEST_END_TESTCASE(mbuf_tests, "mbuf", "MBuf test");
