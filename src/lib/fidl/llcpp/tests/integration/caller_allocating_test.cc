// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/llcpptest.protocol.test/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/llcpp/server.h>

#include <cstdint>

#include <zxtest/zxtest.h>

#include "arena_checker.h"

namespace test = ::llcpptest_protocol_test;

//
// This file tests the behavior of caller-allocating flavors (i.e. the
// `.buffer()` syntax) of clients and server APIs end-to-end.
//

class WireCallTest : public ::zxtest::Test {
 public:
  class Frobinator : public fidl::WireServer<test::Frobinator> {
   public:
    void Frob(FrobRequestView request, FrobCompleter::Sync& completer) override {}

    void Grob(GrobRequestView request, GrobCompleter::Sync& completer) override {
      completer.Reply(request->value);
    }
  };

  void SetUp() override {
    loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigAttachToCurrentThread);
    zx::status server_end = fidl::CreateEndpoints(&client_end_);
    ASSERT_OK(server_end.status_value());
    fidl::BindServer(loop_->dispatcher(), std::move(*server_end), std::make_unique<Frobinator>());
    ASSERT_OK(loop_->StartThread());
  }

  const fidl::ClientEnd<test::Frobinator>& client_end() { return client_end_; }

 private:
  std::unique_ptr<async::Loop> loop_;
  fidl::ClientEnd<test::Frobinator> client_end_;
};

bool IsPointerInBufferSpan(void* pointer, fidl::BufferSpan buffer_span) {
  auto* data = static_cast<uint8_t*>(pointer);
  if (data > buffer_span.data) {
    if (data - buffer_span.data < buffer_span.capacity) {
      return true;
    }
  }
  return false;
}

TEST_F(WireCallTest, CallerAllocateBufferSpan) {
  fidl::SyncClientBuffer<test::Frobinator::Grob> buffer;
  fidl::WireUnownedResult result = fidl::WireCall(client_end()).buffer(buffer.view())->Grob("test");
  ASSERT_OK(result.status());
  EXPECT_EQ(result->value.get(), "test");
  EXPECT_TRUE(IsPointerInBufferSpan(result.Unwrap(), buffer.view()));
}

TEST_F(WireCallTest, CallerAllocateBufferSpanLeftValueVeneerObject) {
  fidl::SyncClientBuffer<test::Frobinator::Grob> buffer;
  auto buffered = fidl::WireCall(client_end()).buffer(buffer.view());
  fidl::WireUnownedResult result = buffered->Grob("test");
  ASSERT_OK(result.status());
  EXPECT_EQ(result->value.get(), "test");
  EXPECT_TRUE(IsPointerInBufferSpan(result.Unwrap(), buffer.view()));
}

TEST_F(WireCallTest, CallerAllocateArena) {
  fidl::Arena arena;
  fidl::WireUnownedResult result = fidl::WireCall(client_end()).buffer(arena)->Grob("test");
  ASSERT_OK(result.status());
  EXPECT_EQ(result->value.get(), "test");
  EXPECT_TRUE(fidl_testing::ArenaChecker::IsPointerInArena(result.Unwrap(), arena));
}

TEST_F(WireCallTest, CallerAllocateArenaLeftValueVeneerObject) {
  // Pre-allocate a 1 MiB arena.
  constexpr size_t kArenaSize = 1024ul * 1024ul;
  auto arena = std::make_unique<fidl::Arena<kArenaSize>>();
  auto buffered = fidl::WireCall(client_end()).buffer(*arena);
  // Using an arena, we can now afford to make multiple calls without extra heap
  // allocation, while keeping all the responses simultaneously alive...
  fidl::WireUnownedResult result_foo = buffered->Grob("foo");
  fidl::WireUnownedResult result_bar = buffered->Grob("bar");
  fidl::WireUnownedResult result_baz = buffered->Grob("baz");
  ASSERT_OK(result_foo.status());
  ASSERT_OK(result_bar.status());
  ASSERT_OK(result_baz.status());
  EXPECT_EQ(result_foo->value.get(), "foo");
  EXPECT_TRUE(fidl_testing::ArenaChecker::IsPointerInArena(result_foo.Unwrap(), *arena));
  EXPECT_EQ(result_bar->value.get(), "bar");
  EXPECT_TRUE(fidl_testing::ArenaChecker::IsPointerInArena(result_bar.Unwrap(), *arena));
  EXPECT_EQ(result_baz->value.get(), "baz");
  EXPECT_TRUE(fidl_testing::ArenaChecker::IsPointerInArena(result_baz.Unwrap(), *arena));
}

TEST_F(WireCallTest, CallerAllocateInsufficientBufferSize) {
  uint8_t small_buffer[8];
  fidl::WireUnownedResult result = fidl::WireCall(client_end())
                                       .buffer(fidl::BufferSpan(small_buffer, sizeof(small_buffer)))
                                       ->Grob("test");
  EXPECT_STATUS(ZX_ERR_BUFFER_TOO_SMALL, result.status());
  EXPECT_EQ(fidl::Reason::kEncodeError, result.reason());
}
