// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/gtest/real_loop_fixture.h>

#include <unordered_set>

#include "latency.h"
#include "packet_loss.h"
#include "reorder.h"

namespace netemul {
namespace testing {

uint8_t gNextRandomNumber = 0;
uint8_t TestRNG() { return gNextRandomNumber; }

class ControlledRand {
 public:
  ControlledRand(int64_t, int64_t) {}

  int64_t Next() { return NextRand; }

  static int64_t NextRand;
};

int64_t ControlledRand::NextRand = 0;

void NoReorder(std::vector<InterceptPacket>* packets) {
  // do nothing
}

void ReverseOrder(std::vector<InterceptPacket>* packets) {
  // just deliver packets in reverse order
  std::reverse(packets->begin(), packets->end());
}

class InterceptorsTest : public gtest::RealLoopFixture {
 public:
  InterceptPacket MakeSingleBytePacket(uint8_t value) {
    return InterceptPacket(std::vector<uint8_t>({value}),
                           data::Consumer::Ptr());
  }
};

TEST_F(InterceptorsTest, PacketLossRealRand) {
  int half_loss_count = 0;
  interceptor::PacketLoss half_loss(
      50, [&half_loss_count](InterceptPacket packet) { half_loss_count++; });
  int full_loss_count = 0;
  interceptor::PacketLoss full_loss(
      100, [&full_loss_count](InterceptPacket packet) { full_loss_count++; });
  int no_loss_count = 0;
  interceptor::PacketLoss no_loss(
      0, [&no_loss_count](InterceptPacket packet) { no_loss_count++; });

  for (int i = 0; i < 500; i++) {
    half_loss.Intercept(MakeSingleBytePacket(static_cast<uint8_t>(i)));
    full_loss.Intercept(MakeSingleBytePacket(static_cast<uint8_t>(i)));
    no_loss.Intercept(MakeSingleBytePacket(static_cast<uint8_t>(i)));
  }

  // full loss should have passed no packets
  EXPECT_EQ(full_loss_count, 0);
  // no loss should have passed all packets
  EXPECT_EQ(no_loss_count, 500);

  std::cout << half_loss_count << " packets passed at 50% loss" << std::endl;
  // expect that something around 250 packets should pass at 50% loss
  // give it wiggle room to prevent test being flaky
  EXPECT_TRUE(half_loss_count > 200 && half_loss_count < 300);
}

TEST_F(InterceptorsTest, PacketLossControlledRand) {
  int pass_count = 0;
  interceptor::PacketLoss<TestRNG> loss(
      50, [&pass_count](InterceptPacket packet) { pass_count++; });

  gNextRandomNumber = 99;
  loss.Intercept(MakeSingleBytePacket(static_cast<uint8_t>(1)));
  EXPECT_EQ(pass_count, 1);

  gNextRandomNumber = 0;
  loss.Intercept(MakeSingleBytePacket(static_cast<uint8_t>(1)));
  EXPECT_EQ(pass_count, 1);

  gNextRandomNumber = 50;
  loss.Intercept(MakeSingleBytePacket(static_cast<uint8_t>(1)));
  EXPECT_EQ(pass_count, 2);

  gNextRandomNumber = 49;
  loss.Intercept(MakeSingleBytePacket(static_cast<uint8_t>(1)));
  EXPECT_EQ(pass_count, 2);
}

TEST_F(InterceptorsTest, LatencyRealRand) {
  int pass_count = 0;
  interceptor::Latency latency(
      5, 1, [&pass_count](InterceptPacket packet) { pass_count++; });

  for (int i = 0; i < 5; i++) {
    latency.Intercept(MakeSingleBytePacket(static_cast<uint8_t>(i)));
  }

  EXPECT_EQ(pass_count, 0);
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil(
      [&pass_count]() { return pass_count == 5; }, zx::sec(2)));
}

TEST_F(InterceptorsTest, LatencyControlledRand) {
  ControlledRand::NextRand = 10;
  int pass_count = 0;
  zx::time after;
  interceptor::Latency<ControlledRand> latency(
      0, 0, [&pass_count, &after](InterceptPacket packet) {
        pass_count++;
        after = zx::clock::get_monotonic();
      });
  auto bef = zx::clock::get_monotonic();
  latency.Intercept(MakeSingleBytePacket(static_cast<uint8_t>(0)));

  EXPECT_EQ(pass_count, 0);
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil(
      [&pass_count]() { return pass_count == 1; }, zx::sec(2)));

  auto diff = (after - bef).to_msecs();
  std::cout << "measured latency = " << diff << "ms" << std::endl;
  // the "diff" time should be ~10ms based on the value set on NextRand.
  // check that it's at least the 10ms that was set:
  // (upper-bound cecking is not very CQ-friendly)
  EXPECT_TRUE(diff >= 10);
}

TEST_F(InterceptorsTest, LatencyFlush) {
  int pass_count = 0;
  interceptor::Latency latency(
      15, 0, [&pass_count](InterceptPacket packet) { pass_count++; });

  for (int i = 0; i < 5; i++) {
    latency.Intercept(MakeSingleBytePacket(static_cast<uint8_t>(i)));
  }
  EXPECT_EQ(pass_count, 0);

  // flush all packets
  auto vec = latency.Flush();
  EXPECT_EQ(vec.size(), 5ul);
  uint8_t c = 0;
  for (auto& p : vec) {
    EXPECT_EQ(p.data()[0], c);
    c++;
  }

  // give it enough time so it *would* call the callback
  RunLoopWithTimeout(zx::msec(20));

  // no packets should have passed
  EXPECT_EQ(pass_count, 0);
}

TEST_F(InterceptorsTest, ReorderRealRand) {
  std::unordered_set<uint8_t> rcv;
  interceptor::Reorder reorder(5, zx::msec(0), [&rcv](InterceptPacket packet) {
    auto value = packet.data()[0];
    std::cout << "received packet " << (int)value << std::endl;
    rcv.insert(value);
  });
  for (uint8_t i = 0; i < 5; i++) {
    EXPECT_TRUE(rcv.empty());
    reorder.Intercept(MakeSingleBytePacket(i));
  }
  EXPECT_EQ(rcv.size(), 5ul);
  for (uint8_t i = 0; i < 5; i++) {
    EXPECT_TRUE(rcv.find(i) != rcv.end());
  }
}

TEST_F(InterceptorsTest, ReorderFakeRand) {
  // test reordering with some forced deterministic ordering algorithms:
  constexpr int packet_count = 5;
  uint8_t no_reorder_count = 0;
  uint8_t reverse_count = packet_count;
  interceptor::Reorder<NoReorder> no_reorder(
      packet_count, zx::msec(0), [&no_reorder_count](InterceptPacket packet) {
        EXPECT_EQ(packet.data()[0], no_reorder_count);
        no_reorder_count++;
      });
  interceptor::Reorder<ReverseOrder> reverse(
      packet_count, zx::msec(0), [&reverse_count](InterceptPacket packet) {
        reverse_count--;
        EXPECT_EQ(packet.data()[0], reverse_count);
      });

  for (uint8_t i = 0; i < packet_count; i++) {
    EXPECT_EQ(no_reorder_count, 0);
    EXPECT_EQ(reverse_count, packet_count);
    no_reorder.Intercept(MakeSingleBytePacket(i));
    reverse.Intercept(MakeSingleBytePacket(i));
  }
  // after we hit the packet count buffer limit check that the counts are what
  // we expect:
  EXPECT_EQ(reverse_count, 0);
  EXPECT_EQ(no_reorder_count, 5);

  // send more packets but don't hit threshold:
  no_reorder.Intercept(MakeSingleBytePacket(100));
  reverse.Intercept(MakeSingleBytePacket(100));

  // counts should not change
  EXPECT_EQ(reverse_count, 0);
  EXPECT_EQ(no_reorder_count, 5);
}

TEST_F(InterceptorsTest, ReorderTick) {
  // test that tick works. Send |packet_count| packets on a |threshold| >
  // |packet_count| threshold interceptor. Expect to get packets regardless due
  // to tick if tick is set, and no packets if tick is not set.
  constexpr int packet_count = 5;
  constexpr int threshold = 25;

  int tick_count = 0;
  interceptor::Reorder<NoReorder> reorder_with_tick(
      threshold, zx::msec(1),
      [&tick_count](InterceptPacket packet) { tick_count++; });
  interceptor::Reorder<NoReorder> reorder_no_tick(
      threshold, zx::msec(0), [](InterceptPacket packet) {
        FAIL() << "Should never pass through packets";
      });

  for (uint8_t i = 0; i < packet_count; i++) {
    EXPECT_EQ(tick_count, 0);
    reorder_with_tick.Intercept(MakeSingleBytePacket(i));
    reorder_no_tick.Intercept(MakeSingleBytePacket(i));
  }
  // didn't hit threshold, so tick count should still be zero
  EXPECT_EQ(tick_count, 0);

  ASSERT_TRUE(RunLoopWithTimeoutOrUntil(
      [&tick_count]() { return tick_count == packet_count; }, zx::sec(2)));
}

TEST_F(InterceptorsTest, ReorderFlush) {
  // Verify that flushing pending packets gets us all the unused packets back:
  constexpr int threshold = 3;
  constexpr int packet_count = 5;
  interceptor::Reorder<NoReorder> reorder(threshold, zx::msec(0),
                                          [](InterceptPacket packet) {});
  for (uint8_t i = 0; i < packet_count; i++) {
    reorder.Intercept(MakeSingleBytePacket(i));
  }
  auto flush = reorder.Flush();
  ASSERT_EQ(flush.size(), static_cast<size_t>(packet_count - threshold));
  auto it = flush.begin();
  // packets gotten from Flush() should start their count at |threshold| and go
  // up to |packet_count|
  for (int i = threshold; i < packet_count; i++) {
    EXPECT_EQ(it->data()[0], i);
    it++;
  }
}

}  // namespace testing
}  // namespace netemul
