// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/unittest/unittest.h>
#include <lib/unittest/user_memory.h>

#include <fbl/array.h>
#include <ktl/unique_ptr.h>

#include "object/mbuf.h"

namespace {

using testing::UserMemory;

enum class MessageType { kStream, kDatagram };

enum class ReadType { kRead, kPeek };

// Writes a null-terminated string into |chain|.
//
// Helps eliminate some of the boilerplate code dealing with copying in and
// out of user memory to make the test logic more obvious.
//
// The null terminator on |str| is not copied into |chain|, it's just used
// so that we can easily determine the length without requiring the caller
// to pass it in separately.
//
// Returns false if a user memory operation fails or |chain| failed to write
// the whole |str|.
bool WriteHelper(MBufChain* chain, const char* str, MessageType message_type) {
  BEGIN_TEST;

  const size_t length = strlen(str);
  ktl::unique_ptr<UserMemory> memory = UserMemory::Create(length);
  ASSERT_NE(nullptr, memory.get());
  ASSERT_EQ(ZX_OK, make_user_out_ptr(memory->out<char>()).copy_array_to_user(str, length));

  auto user_in = make_user_in_ptr(memory->in<char>());
  size_t written = 0;
  if (message_type == MessageType::kDatagram) {
    ASSERT_EQ(ZX_OK, chain->WriteDatagram(user_in, length, &written));
  } else {
    ASSERT_EQ(ZX_OK, chain->WriteStream(user_in, length, &written));
  }
  ASSERT_EQ(length, written);

  END_TEST;
}

// Reads or peeks data from |chain|.
//
// Returns nullptr on memory failure.
fbl::Array<char> ReadHelper(MBufChain* chain, size_t length, MessageType message_type,
                            ReadType read_type) {
  // It's an error to create UserMemory of size 0, so bump this to 1 even if we
  // don't intend to use it.
  ktl::unique_ptr<UserMemory> memory = UserMemory::Create(length ? length : 1);
  if (!memory) {
    unittest_printf("Failed to allocate UserMemory\n");
    return nullptr;
  }

  auto user_out = make_user_out_ptr(memory->out<char>());
  bool datagram = (message_type == MessageType::kDatagram);
  size_t actual;
  zx_status_t status = (read_type == ReadType::kRead)
                           ? chain->Read(user_out, length, datagram, &actual)
                           : chain->Peek(user_out, length, datagram, &actual);
  if (status != ZX_OK) {
    return nullptr;
  }

  fbl::AllocChecker ac;
  fbl::Array<char> buffer(new (&ac) char[actual], actual);
  if (!ac.check()) {
    unittest_printf("Failed to allocate char buffer\n");
    return nullptr;
  }

  if (make_user_in_ptr(memory->in<char>()).copy_array_from_user(buffer.data(), actual) != ZX_OK) {
    unittest_printf("Failed to copy user memory bytes\n");
    return nullptr;
  }

  return buffer;
}

// Checks that the contents of |buffer| match the null-terminated |str|.
//
// fbl::Array<char> isn't supported by EXPECT_EQ() due to printf() usage so
// this allows us to write EXPECT_TRUE(Equal(...)) instead.
//
// Returns false if either the size or contents differ.
bool Equal(const fbl::Array<char>& buffer, const char* str) {
  const size_t length = strlen(str);
  return buffer.size() == length && memcmp(buffer.data(), str, length) == 0;
}

static bool initial_state() {
  BEGIN_TEST;
  MBufChain chain;
  EXPECT_TRUE(chain.is_empty());
  EXPECT_FALSE(chain.is_full());
  EXPECT_EQ(0U, chain.size());
  END_TEST;
}

// Tests reading a stream when the chain is empty.
static bool stream_read_empty() {
  BEGIN_TEST;
  ktl::unique_ptr<UserMemory> mem = UserMemory::Create(1);
  auto mem_out = make_user_out_ptr(mem->out<char>());

  MBufChain chain;
  size_t actual;
  EXPECT_EQ(ZX_OK, chain.Read(mem_out, 1, false, &actual));
  EXPECT_EQ(0U, actual);
  END_TEST;
}

// Tests reading a stream with a zero-length buffer.
static bool stream_read_zero() {
  BEGIN_TEST;
  ktl::unique_ptr<UserMemory> mem = UserMemory::Create(1);
  auto mem_in = make_user_in_ptr(mem->in<char>());
  auto mem_out = make_user_out_ptr(mem->out<char>());

  MBufChain chain;
  size_t written = 7;
  ASSERT_EQ(ZX_OK, chain.WriteStream(mem_in, 1, &written));
  ASSERT_EQ(1U, written);

  size_t actual;
  EXPECT_EQ(ZX_OK, chain.Read(mem_out, 0, false, &actual));
  EXPECT_EQ(0U, actual);
  END_TEST;
}

// Tests basic WriteStream/Read functionality.
static bool stream_write_basic() {
  BEGIN_TEST;
  constexpr size_t kWriteLen = 1024;
  constexpr int kNumWrites = 5;

  ktl::unique_ptr<UserMemory> mem = UserMemory::Create(kWriteLen);
  auto mem_in = make_user_in_ptr(mem->in<char>());
  auto mem_out = make_user_out_ptr(mem->out<char>());

  size_t written = 0;
  MBufChain chain;
  // Call write several times with different buffer contents.
  for (int i = 0; i < kNumWrites; ++i) {
    char buf[kWriteLen] = {0};
    memset(buf, 'A' + i, kWriteLen);
    ASSERT_EQ(ZX_OK, mem_out.copy_array_to_user(buf, kWriteLen));
    ASSERT_EQ(ZX_OK, chain.WriteStream(mem_in, kWriteLen, &written));
    ASSERT_EQ(kWriteLen, written);
    EXPECT_FALSE(chain.is_empty());
    EXPECT_FALSE(chain.is_full());
    EXPECT_EQ((i + 1) * kWriteLen, chain.size());
  }

  // Read it all back in one call.
  constexpr size_t kTotalLen = kWriteLen * kNumWrites;
  ASSERT_EQ(kTotalLen, chain.size());
  ktl::unique_ptr<UserMemory> read_buf = UserMemory::Create(kTotalLen);
  auto read_buf_in = make_user_in_ptr(read_buf->in<char>());
  auto read_buf_out = make_user_out_ptr(read_buf->out<char>());

  size_t actual;
  zx_status_t status = chain.Read(read_buf_out, kTotalLen, false, &actual);
  ASSERT_EQ(ZX_OK, status);
  ASSERT_EQ(kTotalLen, actual);
  EXPECT_TRUE(chain.is_empty());
  EXPECT_FALSE(chain.is_full());
  EXPECT_EQ(0U, chain.size());

  // Verify result.
  fbl::AllocChecker ac;
  auto expected_buf = ktl::unique_ptr<char[]>(new (&ac) char[kTotalLen]);
  ASSERT_TRUE(ac.check());
  for (int i = 0; i < kNumWrites; ++i) {
    memset(static_cast<void*>(expected_buf.get() + i * kWriteLen), 'A' + i, kWriteLen);
  }
  auto actual_buf = ktl::unique_ptr<char[]>(new (&ac) char[kTotalLen]);
  ASSERT_TRUE(ac.check());
  ASSERT_EQ(ZX_OK, read_buf_in.copy_array_from_user(actual_buf.get(), kTotalLen));
  EXPECT_EQ(0, memcmp(static_cast<void*>(expected_buf.get()), static_cast<void*>(actual_buf.get()),
                      kTotalLen));
  END_TEST;
}

// Tests writing a stream with a zero-length buffer.
static bool stream_write_zero() {
  BEGIN_TEST;
  ktl::unique_ptr<UserMemory> mem = UserMemory::Create(1);
  auto mem_in = make_user_in_ptr(mem->in<char>());
  size_t written = 7;
  MBufChain chain;
  // TODO(maniscalco): Is ZX_ERR_SHOULD_WAIT really the right error here in this case?
  EXPECT_EQ(ZX_ERR_SHOULD_WAIT, chain.WriteStream(mem_in, 0, &written));
  EXPECT_EQ(7U, written);
  EXPECT_TRUE(chain.is_empty());
  EXPECT_FALSE(chain.is_full());
  EXPECT_EQ(0U, chain.size());
  END_TEST;
}

// Tests writing a stream to the chain until it stops accepting writes.
static bool stream_write_too_much() {
  BEGIN_TEST;
  constexpr size_t kWriteLen = 65536;
  ktl::unique_ptr<UserMemory> mem = UserMemory::Create(kWriteLen);
  auto mem_in = make_user_in_ptr(mem->in<char>());
  auto mem_out = make_user_out_ptr(mem->out<char>());
  size_t written = 0;
  MBufChain chain;
  size_t total_written = 0;

  // Fill the chain until it refuses to take any more.
  while (!chain.is_full() && chain.WriteStream(mem_in, kWriteLen, &written) == ZX_OK) {
    total_written += written;
  }
  ASSERT_FALSE(chain.is_empty());
  ASSERT_TRUE(chain.is_full());
  EXPECT_EQ(total_written, chain.size());

  // Read it all back out and see we get back the same number of bytes we wrote.
  size_t total_read = 0;
  size_t bytes_read = 0;
  zx_status_t status;
  while (!chain.is_empty() &&
         ((status = chain.Read(mem_out, kWriteLen, false, &bytes_read)) == ZX_OK) &&
         bytes_read > 0) {
    total_read += bytes_read;
  }
  ASSERT_EQ(ZX_OK, status);
  EXPECT_TRUE(chain.is_empty());
  EXPECT_EQ(0U, chain.size());
  EXPECT_EQ(total_written, total_read);
  END_TEST;
}

static bool stream_peek() {
  BEGIN_TEST;

  MBufChain chain;
  ASSERT_TRUE(WriteHelper(&chain, "abc", MessageType::kStream));
  ASSERT_TRUE(WriteHelper(&chain, "123", MessageType::kStream));

  EXPECT_TRUE(Equal(ReadHelper(&chain, 1, MessageType::kStream, ReadType::kPeek), "a"));
  EXPECT_TRUE(Equal(ReadHelper(&chain, 3, MessageType::kStream, ReadType::kPeek), "abc"));
  EXPECT_TRUE(Equal(ReadHelper(&chain, 4, MessageType::kStream, ReadType::kPeek), "abc1"));
  EXPECT_TRUE(Equal(ReadHelper(&chain, 6, MessageType::kStream, ReadType::kPeek), "abc123"));

  // Make sure peeking didn't affect an actual read.
  EXPECT_EQ(6u, chain.size());
  EXPECT_TRUE(Equal(ReadHelper(&chain, 6, MessageType::kStream, ReadType::kRead), "abc123"));

  END_TEST;
}

static bool stream_peek_empty() {
  BEGIN_TEST;

  MBufChain chain;
  EXPECT_TRUE(Equal(ReadHelper(&chain, 1, MessageType::kStream, ReadType::kPeek), ""));

  END_TEST;
}

static bool stream_peek_zero() {
  BEGIN_TEST;

  MBufChain chain;
  ASSERT_TRUE(WriteHelper(&chain, "a", MessageType::kStream));
  EXPECT_TRUE(Equal(ReadHelper(&chain, 0, MessageType::kStream, ReadType::kPeek), ""));

  END_TEST;
}

// Ask for more data than exists, make sure it only returns the real data.
static bool stream_peek_underflow() {
  BEGIN_TEST;

  MBufChain chain;

  ASSERT_TRUE(WriteHelper(&chain, "abc", MessageType::kStream));
  EXPECT_TRUE(Equal(ReadHelper(&chain, 10, MessageType::kStream, ReadType::kPeek), "abc"));

  ASSERT_TRUE(WriteHelper(&chain, "123", MessageType::kStream));
  EXPECT_TRUE(Equal(ReadHelper(&chain, 10, MessageType::kStream, ReadType::kPeek), "abc123"));

  END_TEST;
}

// Tests reading a datagram when chain is empty.
static bool datagram_read_empty() {
  BEGIN_TEST;
  ktl::unique_ptr<UserMemory> mem = UserMemory::Create(1);
  auto mem_out = make_user_out_ptr(mem->out<char>());

  MBufChain chain;
  size_t actual;
  ASSERT_EQ(ZX_OK, chain.Read(mem_out, 1, true, &actual));
  EXPECT_EQ(0U, actual);
  EXPECT_TRUE(chain.is_empty());
  END_TEST;
}

// Tests reading a datagram with a zero-length buffer.
static bool datagram_read_zero() {
  BEGIN_TEST;
  ktl::unique_ptr<UserMemory> mem = UserMemory::Create(1);
  auto mem_in = make_user_in_ptr(mem->in<char>());
  auto mem_out = make_user_out_ptr(mem->out<char>());

  MBufChain chain;
  size_t written = 7;
  ASSERT_EQ(ZX_OK, chain.WriteDatagram(mem_in, 1, &written));
  ASSERT_EQ(1U, written);
  size_t actual;
  ASSERT_EQ(ZX_OK, chain.Read(mem_out, 0, true, &actual));
  EXPECT_EQ(0U, actual);
  EXPECT_FALSE(chain.is_empty());
  END_TEST;
}

// Tests reading a datagram into a buffer that's too small.
static bool datagram_read_buffer_too_small() {
  BEGIN_TEST;
  constexpr size_t kWriteLen = 32;
  ktl::unique_ptr<UserMemory> mem = UserMemory::Create(kWriteLen);
  auto mem_in = make_user_in_ptr(mem->in<char>());
  auto mem_out = make_user_out_ptr(mem->out<char>());
  size_t written = 0;
  MBufChain chain;

  // Write the 'A' datagram.
  char buf[kWriteLen] = {0};
  memset(buf, 'A', sizeof(buf));
  ASSERT_EQ(ZX_OK, mem_out.copy_array_to_user(buf, sizeof(buf)));
  ASSERT_EQ(ZX_OK, chain.WriteDatagram(mem_in, kWriteLen, &written));
  ASSERT_EQ(kWriteLen, written);
  EXPECT_EQ(kWriteLen, chain.size());
  ASSERT_FALSE(chain.is_empty());

  // Write the 'B' datagram.
  memset(buf, 'B', sizeof(buf));
  ASSERT_EQ(ZX_OK, mem_out.copy_array_to_user(buf, sizeof(buf)));
  ASSERT_EQ(ZX_OK, chain.WriteDatagram(mem_in, kWriteLen, &written));
  ASSERT_EQ(kWriteLen, written);
  EXPECT_EQ(2 * kWriteLen, chain.size());
  ASSERT_FALSE(chain.is_empty());

  // Read back the first datagram, but with a buffer that's too small.  See that we get back a
  // truncated 'A' datagram.
  memset(buf, 0, sizeof(buf));
  ASSERT_EQ(ZX_OK, mem_out.copy_array_to_user(buf, sizeof(buf)));
  size_t actual;
  ASSERT_EQ(ZX_OK, chain.Read(mem_out, 1, true, &actual));
  EXPECT_EQ(1U, actual);
  EXPECT_FALSE(chain.is_empty());
  ASSERT_EQ(ZX_OK, mem_in.copy_array_from_user(buf, sizeof(buf)));
  EXPECT_EQ('A', buf[0]);
  EXPECT_EQ(0, buf[1]);

  // Read the next one and see that it's 'B' implying the remainder of 'A' was discarded.
  EXPECT_EQ(kWriteLen, chain.size());
  memset(buf, 0, kWriteLen);
  ASSERT_EQ(ZX_OK, mem_out.copy_array_to_user(buf, sizeof(buf)));
  ASSERT_EQ(ZX_OK, chain.Read(mem_out, kWriteLen, true, &actual));
  EXPECT_EQ(kWriteLen, actual);
  EXPECT_TRUE(chain.is_empty());
  EXPECT_EQ(0U, chain.size());
  ASSERT_EQ(ZX_OK, mem_in.copy_array_from_user(buf, sizeof(buf)));
  char expected_buf[kWriteLen] = {0};
  memset(expected_buf, 'B', kWriteLen);
  EXPECT_EQ(0, memcmp(expected_buf, buf, kWriteLen));
  END_TEST;
}

// Tests basic WriteDatagram/Read functionality.
static bool datagram_write_basic() {
  BEGIN_TEST;
  constexpr int kNumDatagrams = 100;
  constexpr size_t kMaxLength = kNumDatagrams;
  size_t written = 0;
  size_t total_written = 0;

  ktl::unique_ptr<UserMemory> mem = UserMemory::Create(kMaxLength);
  auto mem_in = make_user_in_ptr(mem->in<char>());
  auto mem_out = make_user_out_ptr(mem->out<char>());

  MBufChain chain;
  // Write a series of datagrams with different sizes.
  for (unsigned i = 1; i <= kNumDatagrams; ++i) {
    char buf[kMaxLength] = {0};
    memset(buf, i, i);
    ASSERT_EQ(ZX_OK, mem_out.copy_array_to_user(buf, sizeof(buf)));
    ASSERT_EQ(ZX_OK, chain.WriteDatagram(mem_in, i, &written));
    ASSERT_EQ(i, written);
    total_written += written;
    EXPECT_FALSE(chain.is_empty());
    EXPECT_FALSE(chain.is_full());
  }

  // Verify size() returns correctly
  EXPECT_EQ(1U, chain.size(true));
  EXPECT_EQ(total_written, chain.size());

  // Read them back and verify their contents.
  for (unsigned i = 1; i <= kNumDatagrams; ++i) {
    EXPECT_EQ(i, chain.size(true));
    char expected_buf[kMaxLength] = {0};
    memset(expected_buf, i, i);
    size_t actual;
    zx_status_t status = chain.Read(mem_out, i, true, &actual);
    ASSERT_EQ(ZX_OK, status);
    ASSERT_EQ(i, actual);
    char actual_buf[kMaxLength] = {0};
    ASSERT_EQ(ZX_OK, mem_in.copy_array_from_user(actual_buf, sizeof(actual_buf)));
    EXPECT_EQ(0, memcmp(expected_buf, actual_buf, i));
  }
  EXPECT_TRUE(chain.is_empty());
  EXPECT_EQ(0U, chain.size());
  END_TEST;
}

// Tests writing a zero-length datagram to the chain.
static bool datagram_write_zero() {
  BEGIN_TEST;
  ktl::unique_ptr<UserMemory> mem = UserMemory::Create(1);
  auto mem_in = make_user_in_ptr(mem->in<char>());

  size_t written = 7;
  MBufChain chain;
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, chain.WriteDatagram(mem_in, 0, &written));
  EXPECT_EQ(7U, written);
  EXPECT_TRUE(chain.is_empty());
  EXPECT_FALSE(chain.is_full());
  EXPECT_EQ(0U, chain.size(true));
  EXPECT_EQ(0U, chain.size());
  END_TEST;
}

// Tests writing datagrams to the chain until it stops accepting writes.
static bool datagram_write_too_much() {
  BEGIN_TEST;
  constexpr size_t kWriteLen = 65536;
  ktl::unique_ptr<UserMemory> mem = UserMemory::Create(kWriteLen);
  auto mem_in = make_user_in_ptr(mem->in<char>());
  auto mem_out = make_user_out_ptr(mem->out<char>());

  size_t written = 0;
  MBufChain chain;
  int num_datagrams_written = 0;
  // Fill the chain until it refuses to take any more.
  while (!chain.is_full() && chain.WriteDatagram(mem_in, kWriteLen, &written) == ZX_OK) {
    ++num_datagrams_written;
    ASSERT_EQ(kWriteLen, written);
  }
  ASSERT_FALSE(chain.is_empty());
  EXPECT_EQ(kWriteLen * num_datagrams_written, chain.size());
  // Read it all back out and see that there's none left over.
  int num_datagrams_read = 0;
  zx_status_t status;
  size_t actual;
  while (!chain.is_empty() && ((status = chain.Read(mem_out, kWriteLen, true, &actual)) == ZX_OK) &&
         actual > 0) {
    ++num_datagrams_read;
  }
  ASSERT_EQ(ZX_OK, status);
  EXPECT_TRUE(chain.is_empty());
  EXPECT_EQ(0U, chain.size());
  EXPECT_EQ(num_datagrams_written, num_datagrams_read);
  END_TEST;
}

// Tests writing a datagram packet larger than the mbuf's capacity.
static bool datagram_write_huge_packet() {
  BEGIN_TEST;

  MBufChain chain;

  const size_t kHugePacketSize = chain.max_size() + 1;
  ktl::unique_ptr<UserMemory> mem = UserMemory::Create(kHugePacketSize);
  auto mem_in = make_user_in_ptr(mem->in<char>());

  size_t written;
  zx_status_t status = chain.WriteDatagram(mem_in, kHugePacketSize, &written);
  ASSERT_EQ(status, ZX_ERR_OUT_OF_RANGE);

  END_TEST;
}

static bool datagram_peek() {
  BEGIN_TEST;

  MBufChain chain;
  ASSERT_TRUE(WriteHelper(&chain, "abc", MessageType::kDatagram));

  EXPECT_TRUE(Equal(ReadHelper(&chain, 1, MessageType::kDatagram, ReadType::kPeek), "a"));
  EXPECT_TRUE(Equal(ReadHelper(&chain, 3, MessageType::kDatagram, ReadType::kPeek), "abc"));

  // Make sure peeking didn't affect an actual read.
  EXPECT_EQ(3u, chain.size());
  EXPECT_TRUE(Equal(ReadHelper(&chain, 3, MessageType::kDatagram, ReadType::kRead), "abc"));

  END_TEST;
}

static bool datagram_peek_empty() {
  BEGIN_TEST;

  MBufChain chain;
  EXPECT_TRUE(Equal(ReadHelper(&chain, 1, MessageType::kDatagram, ReadType::kPeek), ""));

  END_TEST;
}

static bool datagram_peek_zero() {
  BEGIN_TEST;

  MBufChain chain;
  ASSERT_TRUE(WriteHelper(&chain, "a", MessageType::kDatagram));
  EXPECT_TRUE(Equal(ReadHelper(&chain, 0, MessageType::kDatagram, ReadType::kPeek), ""));

  END_TEST;
}

static bool datagram_peek_underflow() {
  BEGIN_TEST;

  MBufChain chain;
  ASSERT_TRUE(WriteHelper(&chain, "abc", MessageType::kDatagram));
  ASSERT_TRUE(WriteHelper(&chain, "123", MessageType::kDatagram));

  // Datagram peeks should not return more than a single message.
  EXPECT_TRUE(Equal(ReadHelper(&chain, 10, MessageType::kDatagram, ReadType::kPeek), "abc"));
  EXPECT_TRUE(Equal(ReadHelper(&chain, 3, MessageType::kDatagram, ReadType::kRead), "abc"));
  EXPECT_TRUE(Equal(ReadHelper(&chain, 10, MessageType::kDatagram, ReadType::kPeek), "123"));

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
UNITTEST("stream_peek", stream_peek)
UNITTEST("stream_peek_empty", stream_peek_empty)
UNITTEST("stream_peek_zero", stream_peek_zero)
UNITTEST("stream_peek_underflow", stream_peek_underflow)
UNITTEST("datagram_read_empty", datagram_read_empty)
UNITTEST("datagram_read_zero", datagram_read_zero)
UNITTEST("datagram_read_buffer_too_small", datagram_read_buffer_too_small)
UNITTEST("datagram_write_basic", datagram_write_basic)
UNITTEST("datagram_write_zero", datagram_write_zero)
UNITTEST("datagram_write_too_much", datagram_write_too_much)
UNITTEST("datagram_write_huge_packet", datagram_write_huge_packet)
UNITTEST("datagram_peek", datagram_peek)
UNITTEST("datagram_peek_empty", datagram_peek_empty)
UNITTEST("datagram_peek_zero", datagram_peek_zero)
UNITTEST("datagram_peek_underflow", datagram_peek_underflow)
UNITTEST_END_TESTCASE(mbuf_tests, "mbuf", "MBuf test")
