// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/unittest/unittest.h>
#include <lib/unittest/user_memory.h>
#include <lib/user_copy/user_ptr.h>

#include <ktl/unique_ptr.h>

#include "object/message_packet.h"

namespace {

using testing::UserMemory;

// Create a MessagePacket and call CopyDataTo.
static bool create() {
  BEGIN_TEST;
  constexpr size_t kSize = 62234;
  ktl::unique_ptr<UserMemory> mem = UserMemory::Create(kSize);
  auto mem_in = mem->user_in<char>();
  auto mem_out = mem->user_out<char>();

  fbl::AllocChecker ac;
  auto buf = ktl::unique_ptr<char[]>(new (&ac) char[kSize]);
  ASSERT_TRUE(ac.check());
  memset(buf.get(), 'A', kSize);
  ASSERT_EQ(ZX_OK, mem_out.copy_array_to_user(buf.get(), kSize));

  constexpr uint32_t kNumHandles = 64;
  MessagePacketPtr mp;
  EXPECT_EQ(ZX_OK, MessagePacket::Create(mem_in, kSize, kNumHandles, &mp));
  ASSERT_EQ(kSize, mp->data_size());
  EXPECT_EQ(kNumHandles, mp->num_handles());
  EXPECT_NE(0U, mp->get_txid());

  auto result_buf = ktl::unique_ptr<char[]>(new (&ac) char[kSize]);
  ASSERT_TRUE(ac.check());
  ASSERT_EQ(ZX_OK, mp->CopyDataTo(mem_out));
  ASSERT_EQ(ZX_OK, mem_in.copy_array_from_user(result_buf.get(), kSize));
  EXPECT_EQ(0, memcmp(buf.get(), result_buf.get(), kSize));
  END_TEST;
}

// Create a MessagePacket via void* and call CopyDataTo.
static bool create_void_star() {
  BEGIN_TEST;
  constexpr size_t kSize = 4;
  ktl::unique_ptr<UserMemory> mem = UserMemory::Create(kSize);
  auto mem_in = mem->user_in<char>();
  auto mem_out = mem->user_out<char>();

  fbl::AllocChecker ac;
  auto in_buf = ktl::unique_ptr<char[]>(new (&ac) char[kSize]);
  ASSERT_TRUE(ac.check());
  memset(in_buf.get(), 'B', kSize);
  char* in = in_buf.get();

  constexpr uint32_t kNumHandles = 0;
  MessagePacketPtr mp;
  EXPECT_EQ(ZX_OK, MessagePacket::Create(in, kSize, kNumHandles, &mp));
  ASSERT_EQ(kSize, mp->data_size());
  EXPECT_EQ(kNumHandles, mp->num_handles());
  EXPECT_NE(0U, mp->get_txid());

  auto result_buf = ktl::unique_ptr<char[]>(new (&ac) char[kSize]);
  ASSERT_TRUE(ac.check());
  ASSERT_EQ(ZX_OK, mp->CopyDataTo(mem_out));
  ASSERT_EQ(ZX_OK, mem_in.copy_array_from_user(result_buf.get(), kSize));
  EXPECT_EQ(0, memcmp(in_buf.get(), result_buf.get(), kSize));
  END_TEST;
}

// Create a MessagePacket with zero-length data.
static bool create_zero() {
  BEGIN_TEST;
  ktl::unique_ptr<UserMemory> mem = UserMemory::Create(1);
  auto mem_in = mem->user_in<char>();
  auto mem_out = mem->user_out<char>();

  MessagePacketPtr mp;
  EXPECT_EQ(ZX_OK, MessagePacket::Create(mem_in, 0, 0, &mp));
  ASSERT_EQ(0U, mp->data_size());
  EXPECT_EQ(0U, mp->num_handles());
  EXPECT_EQ(0U, mp->get_txid());

  ASSERT_EQ(ZX_OK, mp->CopyDataTo(mem_out));
  END_TEST;
}

// Attempt to create a MessagePacket with too many handles.
static bool create_too_many_handles() {
  BEGIN_TEST;
  ktl::unique_ptr<UserMemory> mem = UserMemory::Create(1);
  auto mem_in = mem->user_in<char>();

  MessagePacketPtr mp;
  EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, MessagePacket::Create(mem_in, 1, 65, &mp));
  END_TEST;
}

// Attempt to create a MessagePacket from memory that's not part of userspace.
static bool create_bad_mem() {
  BEGIN_TEST;
  constexpr size_t kSize = 64;

  fbl::AllocChecker ac;
  auto buf = ktl::unique_ptr<char[]>(new (&ac) char[kSize]);
  ASSERT_TRUE(ac.check());
  memset(buf.get(), 'C', kSize);
  auto in = make_user_in_ptr(static_cast<const char*>(buf.get()));

  constexpr uint32_t kNumHandles = 0;
  MessagePacketPtr mp;
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, MessagePacket::Create(in, kSize, kNumHandles, &mp));
  END_TEST;
}

// Attempt to copy a MessagePacket to memory that's not part of userspace.
static bool copy_bad_mem() {
  BEGIN_TEST;
  constexpr size_t kSize = 64;
  ktl::unique_ptr<UserMemory> mem = UserMemory::Create(kSize);
  auto mem_in = mem->user_in<char>();
  auto mem_out = mem->user_out<char>();

  fbl::AllocChecker ac;
  auto buf = ktl::unique_ptr<char[]>(new (&ac) char[kSize]);
  ASSERT_TRUE(ac.check());
  memset(buf.get(), 'D', kSize);
  ASSERT_EQ(ZX_OK, mem_out.copy_array_to_user(buf.get(), kSize));

  constexpr uint32_t kNumHandles = 0;
  MessagePacketPtr mp;
  EXPECT_EQ(ZX_OK, MessagePacket::Create(mem_in, kSize, kNumHandles, &mp));

  auto out = make_user_out_ptr(buf.get());
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, mp->CopyDataTo(out));
  END_TEST;
}

// Create a message packet with the specified number of iovec inputs.
template <uint32_t NIovecs, uint32_t NHandles>
static bool create_iovec() {
  BEGIN_TEST;
  constexpr uint32_t kNumBytes = NIovecs * (NIovecs - 1) / 2;
  ktl::unique_ptr<UserMemory> bytes_mem = UserMemory::Create(kNumBytes);
  auto bytes_mem_in = bytes_mem->user_in<char>();
  auto bytes_mem_out = bytes_mem->user_out<char>();

  char bytes[kNumBytes];
  for (uint32_t i = 0; i < sizeof(bytes); i++) {
    bytes[i] = static_cast<char>(i);
  }
  ASSERT_EQ(ZX_OK, bytes_mem_out.copy_array_to_user(bytes, kNumBytes));

  zx_channel_iovec_t iovecs[NIovecs];
  for (uint32_t i = 0; i < NIovecs; i++) {
    iovecs[i] = zx_channel_iovec_t{
        .buffer = bytes_mem_in.get(),
        .capacity = i,
        .reserved = 0,
    };
    bytes_mem_in = bytes_mem_in.byte_offset(i);
  }

  ktl::unique_ptr<UserMemory> iovec_mem = UserMemory::Create(NIovecs * sizeof(zx_channel_iovec_t));
  auto iovec_mem_in = iovec_mem->user_in<zx_channel_iovec_t>();
  auto iovec_mem_out = iovec_mem->user_out<zx_channel_iovec_t>();
  ASSERT_EQ(ZX_OK, iovec_mem_out.copy_array_to_user(iovecs, NIovecs));

  MessagePacketPtr mp;
  EXPECT_EQ(ZX_OK, MessagePacket::Create(iovec_mem_in, NIovecs, NHandles, &mp));

  EXPECT_EQ(NHandles, mp->num_handles());

  ktl::unique_ptr<UserMemory> result_mem = UserMemory::Create(kNumBytes);
  auto result_mem_in = result_mem->user_in<char>();
  auto result_mem_out = result_mem->user_out<char>();
  ASSERT_EQ(ZX_OK, mp->CopyDataTo(result_mem_out));
  char result[kNumBytes];
  ASSERT_EQ(ZX_OK, result_mem_in.copy_array_from_user(result, kNumBytes));

  EXPECT_BYTES_EQ(reinterpret_cast<uint8_t*>(bytes), reinterpret_cast<uint8_t*>(result), kNumBytes);

  END_TEST;
}

}  // namespace

UNITTEST_START_TESTCASE(message_packet_tests)
UNITTEST("create", create)
UNITTEST("create_void_star", create_void_star)
UNITTEST("create_zero", create_zero)
UNITTEST("create_too_many_handles", create_too_many_handles)
UNITTEST("create_bad_mem", create_bad_mem)
UNITTEST("copy_bad_mem", copy_bad_mem)
UNITTEST("create_iovec_bounded", (create_iovec<MessagePacket::kIovecChunkSize, 0>))
UNITTEST("create_iovec_unbounded", (create_iovec<2 * MessagePacket::kIovecChunkSize, 0>))
UNITTEST("create_iovec_bounded_handles", (create_iovec<MessagePacket::kIovecChunkSize, 3>))
UNITTEST("create_iovec_unbounded_handles", (create_iovec<2 * MessagePacket::kIovecChunkSize, 3>))
UNITTEST_END_TESTCASE(message_packet_tests, "message_packet", "MessagePacket tests")
