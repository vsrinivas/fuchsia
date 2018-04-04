// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/message_packet.h>

#include <fbl/unique_ptr.h>
#include <lib/unittest/unittest.h>
#include <lib/unittest/user_memory.h>
#include <lib/user_copy/user_ptr.h>

namespace {

using testing::UserMemory;

// Create a MessagePacket and call CopyDataTo.
static bool create() {
    BEGIN_TEST;
    constexpr size_t kSize = 62234;
    fbl::unique_ptr<UserMemory> mem = UserMemory::Create(kSize);
    auto mem_in = make_user_in_ptr(mem->in());
    auto mem_out = make_user_out_ptr(mem->out());

    fbl::AllocChecker ac;
    auto buf = fbl::unique_ptr<char[]>(new (&ac) char[kSize]);
    ASSERT_TRUE(ac.check(), "");
    memset(buf.get(), 'A', kSize);
    ASSERT_EQ(ZX_OK, mem_out.copy_array_to_user(buf.get(), kSize), "");

    constexpr uint32_t kNumHandles = 64;
    fbl::unique_ptr<MessagePacket> mp;
    EXPECT_EQ(ZX_OK, MessagePacket::Create(mem_in, kSize, kNumHandles, &mp), "");
    ASSERT_EQ(kSize, mp->data_size(), "");
    EXPECT_EQ(kNumHandles, mp->num_handles(), "");
    EXPECT_NE(0U, mp->get_txid(), "");

    auto result_buf = fbl::unique_ptr<char[]>(new (&ac) char[kSize]);
    ASSERT_TRUE(ac.check(), "");
    ASSERT_EQ(ZX_OK, mp->CopyDataTo(mem_out), "");
    ASSERT_EQ(ZX_OK, mem_in.copy_array_from_user(result_buf.get(), kSize), "");
    EXPECT_EQ(0, memcmp(buf.get(), result_buf.get(), kSize), "");
    END_TEST;
}

// Create a MessagePacket via void* and call CopyDataTo.
static bool create_void_star() {
    BEGIN_TEST;
    constexpr size_t kSize = 4;
    fbl::unique_ptr<UserMemory> mem = UserMemory::Create(kSize);
    auto mem_in = make_user_in_ptr(mem->in());
    auto mem_out = make_user_out_ptr(mem->out());

    fbl::AllocChecker ac;
    auto in_buf = fbl::unique_ptr<char[]>(new (&ac) char[kSize]);
    ASSERT_TRUE(ac.check(), "");
    memset(in_buf.get(), 'B', kSize);
    void* in = in_buf.get();

    constexpr uint32_t kNumHandles = 0;
    fbl::unique_ptr<MessagePacket> mp;
    EXPECT_EQ(ZX_OK, MessagePacket::Create(in, kSize, kNumHandles, &mp), "");
    ASSERT_EQ(kSize, mp->data_size(), "");
    EXPECT_EQ(kNumHandles, mp->num_handles(), "");
    EXPECT_NE(0U, mp->get_txid(), "");

    auto result_buf = fbl::unique_ptr<char[]>(new (&ac) char[kSize]);
    ASSERT_TRUE(ac.check(), "");
    ASSERT_EQ(ZX_OK, mp->CopyDataTo(mem_out), "");
    ASSERT_EQ(ZX_OK, mem_in.copy_array_from_user(result_buf.get(), kSize), "");
    EXPECT_EQ(0, memcmp(in_buf.get(), result_buf.get(), kSize), "");
    END_TEST;
}

// Create a MessagePacket with zero-length data.
static bool create_zero() {
    BEGIN_TEST;
    fbl::unique_ptr<UserMemory> mem = UserMemory::Create(1);
    auto mem_in = make_user_in_ptr(mem->in());
    auto mem_out = make_user_out_ptr(mem->out());

    fbl::unique_ptr<MessagePacket> mp;
    EXPECT_EQ(ZX_OK, MessagePacket::Create(mem_in, 0, 0, &mp), "");
    ASSERT_EQ(0U, mp->data_size(), "");
    EXPECT_EQ(0U, mp->num_handles(), "");
    EXPECT_EQ(0U, mp->get_txid(),"");

    ASSERT_EQ(ZX_OK, mp->CopyDataTo(mem_out), "");
    END_TEST;
}

// Attempt to create a MessagePacket with too many handles.
static bool create_too_many_handles() {
    BEGIN_TEST;
    fbl::unique_ptr<UserMemory> mem = UserMemory::Create(1);
    auto mem_in = make_user_in_ptr(mem->in());

    fbl::unique_ptr<MessagePacket> mp;
    EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, MessagePacket::Create(mem_in, 1, 65, &mp), "");
    END_TEST;
}

}  // namespace

UNITTEST_START_TESTCASE(message_packet_tests)
UNITTEST("create", create)
UNITTEST("create_void_star", create_void_star)
UNITTEST("create_zero", create_zero)
UNITTEST("create_too_many_handles", create_too_many_handles)
UNITTEST_END_TESTCASE(message_packet_tests, "message_packet", "MessagePacket tests");
