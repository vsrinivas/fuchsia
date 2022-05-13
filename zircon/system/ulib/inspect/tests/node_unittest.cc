// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fpromise/single_threaded_executor.h>
#include <lib/inspect/cpp/hierarchy.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/inspect/cpp/reader.h>
#include <lib/inspect/cpp/vmo/types.h>
#include <zircon/syscalls.h>
#include <zircon/time.h>
#include <zircon/types.h>

#include <thread>

#include <zxtest/zxtest.h>

namespace {

void NotifyAndWaitPeer(zx_handle_t handle) {
  zx_object_signal_peer(handle, ZX_USER_SIGNAL_0, ZX_USER_SIGNAL_0);
  zx_object_wait_one(handle, ZX_USER_SIGNAL_0, ZX_TIME_INFINITE, nullptr);
  zx_object_signal(handle, ZX_USER_SIGNAL_0, 0);
}

void WaitExpectAndNotifyPeer(zx_handle_t handle, fit::function<void()> expect) {
  zx_object_wait_one(handle, ZX_USER_SIGNAL_0, ZX_TIME_INFINITE, nullptr);
  zx_object_signal(handle, ZX_USER_SIGNAL_0, 0);
  expect();
  zx_object_signal_peer(handle, ZX_USER_SIGNAL_0, ZX_USER_SIGNAL_0);
}

TEST(Node, AtomicUpdate) {
  inspect::Inspector inspector;

  // Duplicate handle to VMO, so that reader and writer threads can have concurrent access.
  zx::vmo vmo = inspector.DuplicateVmo();

  zx_handle_t read_end, write_end;
  zx_status_t res0 = zx_eventpair_create(0, &read_end, &write_end);
  ASSERT_EQ(res0, ZX_OK);

  std::thread writer([&] {
    // At each step of the atomic update, signal reader to check that the header is still locked.
    NotifyAndWaitPeer(write_end);
    inspect::IntProperty int_val = inspector.GetRoot().CreateInt("value", 1);
    inspector.GetRoot().AtomicUpdate([&](inspect::Node& node) {
      // At each step, yield to the reader so it can assert that the update is atomic.
      auto child = node.CreateChild("child");
      NotifyAndWaitPeer(write_end);
      child.CreateInt("a", 1, &inspector);
      NotifyAndWaitPeer(write_end);
      child.CreateInt("b", 2, &inspector);
      NotifyAndWaitPeer(write_end);
      int_val.Add(1);
      inspector.emplace(std::move(child));
    });
    NotifyAndWaitPeer(write_end);
  });

  // Initial state, before the atomic update.
  WaitExpectAndNotifyPeer(read_end, [&]() {
    auto parsed = inspect::ReadFromVmo(vmo);
    ASSERT_TRUE(parsed.is_ok());
    inspect::Hierarchy hierarchy = parsed.take_value();
    EXPECT_EQ(0, hierarchy.children().size());
    EXPECT_EQ(0, hierarchy.node().properties().size());
  });
  // After CreateChild("child"): Assert the VMO is locked and we can't read.
  WaitExpectAndNotifyPeer(read_end, [&]() {
    auto parsed = inspect::ReadFromVmo(vmo);
    EXPECT_TRUE(parsed.is_error());
  });
  // After CreateInt("a", 1): Assert the VMO is locked and we can't read.
  WaitExpectAndNotifyPeer(read_end, [&]() {
    auto parsed = inspect::ReadFromVmo(vmo);
    EXPECT_TRUE(parsed.is_error());
  });
  // After CreateInt("b", 2): Assert the VMO is locked and we can't read.
  WaitExpectAndNotifyPeer(read_end, [&]() {
    auto parsed = inspect::ReadFromVmo(vmo);
    EXPECT_TRUE(parsed.is_error());
  });
  // After atomic update. Verify all state
  WaitExpectAndNotifyPeer(read_end, [&]() {
    auto parsed = inspect::ReadFromVmo(vmo).take_value();

    ASSERT_EQ(1, parsed.node().properties().size());
    EXPECT_EQ("value", parsed.node().properties()[0].name());
    EXPECT_EQ(2, parsed.node().properties()[0].Get<inspect::IntPropertyValue>().value());

    ASSERT_EQ(1, parsed.children().size());
    auto& child = parsed.children()[0].node();
    EXPECT_EQ("child", child.name());
    ASSERT_EQ(2, child.properties().size());
    EXPECT_EQ("a", child.properties()[0].name());
    EXPECT_EQ("b", child.properties()[1].name());
    EXPECT_EQ(1, child.properties()[0].Get<inspect::IntPropertyValue>().value());
    EXPECT_EQ(2, child.properties()[1].Get<inspect::IntPropertyValue>().value());
  });
  writer.join();
}

TEST(Node, Record) {
  inspect::Inspector inspector;

  auto& node = inspector.GetRoot();

  node.RecordInt("int", 1);
  node.RecordUint("uint", 2);
  node.RecordDouble("double", 3.25);
  node.RecordBool("bool", true);
  node.RecordChild("child", [&](inspect::Node& child) {
    child.RecordString("string", "foo");
    child.RecordByteVector("bytes", std::vector<uint8_t>({1, 2, 3}));
  });
  auto other_child = node.CreateChild("other");
  auto array = other_child.CreateIntArray("array", 3);
  for (size_t i = 0; i < 3; i++) {
    array.Set(i, i + 1);
  }
  other_child.Record(std::move(array));
  node.Record(std::move(other_child));

  zx::vmo vmo = inspector.DuplicateVmo();
  auto parsed = inspect::ReadFromVmo(vmo).take_value();

  ASSERT_EQ(4, parsed.node().properties().size());
  EXPECT_EQ("int", parsed.node().properties()[0].name());
  EXPECT_EQ(1, parsed.node().properties()[0].Get<inspect::IntPropertyValue>().value());

  EXPECT_EQ("uint", parsed.node().properties()[1].name());
  EXPECT_EQ(2, parsed.node().properties()[1].Get<inspect::UintPropertyValue>().value());

  EXPECT_EQ("double", parsed.node().properties()[2].name());
  EXPECT_EQ(3.25, parsed.node().properties()[2].Get<inspect::DoublePropertyValue>().value());

  EXPECT_EQ("bool", parsed.node().properties()[3].name());
  EXPECT_EQ(true, parsed.node().properties()[3].Get<inspect::BoolPropertyValue>().value());

  ASSERT_EQ(2, parsed.children().size());
  auto& parsedChild = parsed.children()[0];
  ASSERT_EQ(2, parsedChild.node().properties().size());

  EXPECT_EQ("string", parsedChild.node().properties()[0].name());
  EXPECT_EQ("foo", parsedChild.node().properties()[0].Get<inspect::StringPropertyValue>().value());

  EXPECT_EQ("bytes", parsedChild.node().properties()[1].name());
  EXPECT_EQ(std::vector<uint8_t>({1, 2, 3}),
            parsedChild.node().properties()[1].Get<inspect::ByteVectorPropertyValue>().value());

  auto& secondChild = parsed.children()[1];
  ASSERT_EQ(1, secondChild.node().properties().size());

  EXPECT_EQ("array", secondChild.node().properties()[0].name());
  EXPECT_EQ(std::vector<int64_t>({1, 2, 3}),
            secondChild.node().properties()[0].Get<inspect::IntArrayValue>().value());
}

}  // namespace
