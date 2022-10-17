// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/llcpptest.protocol.test/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/wire/server.h>

#include <cstdint>

#include <src/lib/fidl/llcpp/tests/arena_checker.h>
#include <zxtest/zxtest.h>

namespace test = ::llcpptest_protocol_test;

//
// This file tests the behavior of caller-allocating flavors (i.e. the
// `.buffer()` syntax) of clients and server APIs end-to-end.
//
namespace {

class CallerAllocatingFixture : public ::zxtest::Test {
 public:
  class Frobinator : public fidl::WireServer<test::Frobinator> {
   public:
    virtual size_t frob_count() const = 0;
  };

  virtual std::shared_ptr<Frobinator> GetServer() = 0;

  void SetUp() override {
    loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigAttachToCurrentThread);
    zx::result server_end = fidl::CreateEndpoints(&client_end_);
    ASSERT_OK(server_end.status_value());
    server_ = GetServer();
    binding_ref_ = fidl::BindServer(loop_->dispatcher(), std::move(*server_end), server_);
  }

  std::unique_ptr<async::Loop>& loop() { return loop_; }
  fidl::ClientEnd<test::Frobinator>& client_end() { return client_end_; }
  const fidl::ServerBindingRef<test::Frobinator>& binding_ref() const {
    return binding_ref_.value();
  }
  size_t frob_count() const { return server_->frob_count(); }

 private:
  std::unique_ptr<async::Loop> loop_;
  fidl::ClientEnd<test::Frobinator> client_end_;
  std::shared_ptr<Frobinator> server_;
  std::optional<fidl::ServerBindingRef<test::Frobinator>> binding_ref_;
};

class CallerAllocatingFixtureWithDefaultServer : public CallerAllocatingFixture {
 public:
  class DefaultFrobinator : public Frobinator {
   public:
    void Frob(FrobRequestView request, FrobCompleter::Sync& completer) override {
      EXPECT_EQ(request->value.get(), "test");
      frob_count_++;
    }

    void Grob(GrobRequestView request, GrobCompleter::Sync& completer) override {
      completer.Reply(request->value);
    }

    void TwoWayEmptyArg(TwoWayEmptyArgCompleter::Sync& completer) override { completer.Reply(); }

    size_t frob_count() const override { return frob_count_; }

   private:
    size_t frob_count_ = 0;
  };

  std::shared_ptr<Frobinator> GetServer() override { return std::make_shared<DefaultFrobinator>(); }
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

class WireCallTest : public CallerAllocatingFixtureWithDefaultServer {
  void SetUp() override {
    CallerAllocatingFixtureWithDefaultServer::SetUp();
    ASSERT_OK(loop()->StartThread());
  }
};

TEST_F(WireCallTest, CallerAllocateBufferSpan) {
  fidl::SyncClientBuffer<test::Frobinator::Grob> buffer;
  fidl::WireUnownedResult result = fidl::WireCall(client_end()).buffer(buffer.view())->Grob("test");
  ASSERT_OK(result.status());
  EXPECT_EQ(result.value().value.get(), "test");
  EXPECT_TRUE(IsPointerInBufferSpan(result.Unwrap(), buffer.view()));
}

TEST_F(WireCallTest, CallerAllocateBufferSpanLeftValueVeneerObject) {
  fidl::SyncClientBuffer<test::Frobinator::Grob> buffer;
  auto buffered = fidl::WireCall(client_end()).buffer(buffer.view());
  fidl::WireUnownedResult result = buffered->Grob("test");
  ASSERT_OK(result.status());
  EXPECT_EQ(result.value().value.get(), "test");
  EXPECT_TRUE(IsPointerInBufferSpan(result.Unwrap(), buffer.view()));
}

TEST_F(WireCallTest, CallerAllocateArena) {
  fidl::Arena arena;
  fidl::WireUnownedResult result = fidl::WireCall(client_end()).buffer(arena)->Grob("test");
  ASSERT_OK(result.status());
  EXPECT_EQ(result.value().value.get(), "test");
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
  EXPECT_EQ(result_foo.value().value.get(), "foo");
  EXPECT_TRUE(fidl_testing::ArenaChecker::IsPointerInArena(result_foo.Unwrap(), *arena));
  EXPECT_EQ(result_bar.value().value.get(), "bar");
  EXPECT_TRUE(fidl_testing::ArenaChecker::IsPointerInArena(result_bar.Unwrap(), *arena));
  EXPECT_EQ(result_baz.value().value.get(), "baz");
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

class WireClientTest : public CallerAllocatingFixtureWithDefaultServer {};
class WireSharedClientTest : public CallerAllocatingFixtureWithDefaultServer {};

class GrobResponseContext : public fidl::WireResponseContext<test::Frobinator::Grob> {
 public:
  void OnResult(fidl::WireUnownedResult<test::Frobinator::Grob>& result) final {
    ASSERT_OK(result.status());
    EXPECT_EQ(result.value().value.get(), "test");
    got_result = true;
  }
  bool got_result = false;
};

class TwoWayEmptyArgResponseContext
    : public fidl::WireResponseContext<test::Frobinator::TwoWayEmptyArg> {
 public:
  void OnResult(fidl::WireUnownedResult<test::Frobinator::TwoWayEmptyArg>& result) final {
    ASSERT_OK(result.status());
    got_result = true;
  }
  bool got_result = false;
};

TEST_F(WireClientTest, TwoWayCallerAllocateBufferSpan) {
  fidl::AsyncClientBuffer<test::Frobinator::Grob> buffer;
  fidl::WireClient client(std::move(client_end()), loop()->dispatcher());

  GrobResponseContext context;
  client.buffer(buffer.view())->Grob("test").ThenExactlyOnce(&context);
  loop()->RunUntilIdle();

  EXPECT_TRUE(context.got_result);
}

TEST_F(WireClientTest, TwoWayCallerAllocateArena) {
  fidl::Arena arena;
  fidl::WireClient client(std::move(client_end()), loop()->dispatcher());

  EXPECT_FALSE(fidl_testing::ArenaChecker::DidUse(arena));
  GrobResponseContext context;
  client.buffer(arena)->Grob("test").ThenExactlyOnce(&context);
  loop()->RunUntilIdle();

  EXPECT_TRUE(context.got_result);
  EXPECT_TRUE(fidl_testing::ArenaChecker::DidUse(arena));
}

TEST_F(WireClientTest, OneWayCallerAllocate) {
  fidl::AsyncClientBuffer<test::Frobinator::Grob> buffer;
  fidl::WireClient client(std::move(client_end()), loop()->dispatcher());

  fidl::Status result = client.buffer(buffer.view())->Frob("test");
  loop()->RunUntilIdle();

  EXPECT_OK(result.status());
  EXPECT_EQ(1, frob_count());

  // Test multi-request syntax.
  fidl::Arena arena;
  auto buffered = client.buffer(arena);
  EXPECT_OK(buffered->Frob("test").status());
  EXPECT_OK(buffered->Frob("test").status());
  EXPECT_OK(buffered->Frob("test").status());
  loop()->RunUntilIdle();
  EXPECT_EQ(4, frob_count());
}

TEST_F(WireSharedClientTest, TwoWayCallerAllocateBufferSpan) {
  fidl::AsyncClientBuffer<test::Frobinator::Grob> buffer;
  fidl::WireSharedClient client(std::move(client_end()), loop()->dispatcher());

  GrobResponseContext context;
  client.buffer(buffer.view())->Grob("test").ThenExactlyOnce(&context);
  loop()->RunUntilIdle();

  EXPECT_TRUE(context.got_result);
}

TEST_F(WireSharedClientTest, TwoWayCallerAllocateArena) {
  fidl::Arena arena;
  fidl::WireSharedClient client(std::move(client_end()), loop()->dispatcher());

  EXPECT_FALSE(fidl_testing::ArenaChecker::DidUse(arena));
  GrobResponseContext context;
  client.buffer(arena)->Grob("test").ThenExactlyOnce(&context);
  loop()->RunUntilIdle();

  EXPECT_TRUE(context.got_result);
  EXPECT_TRUE(fidl_testing::ArenaChecker::DidUse(arena));
}

TEST_F(WireSharedClientTest, TwoWayEmptyArgCallerAllocateArena) {
  fidl::Arena arena;
  fidl::WireSharedClient client(std::move(client_end()), loop()->dispatcher());

  EXPECT_FALSE(fidl_testing::ArenaChecker::DidUse(arena));
  TwoWayEmptyArgResponseContext context;
  client.buffer(arena)->TwoWayEmptyArg().ThenExactlyOnce(&context);
  loop()->RunUntilIdle();

  EXPECT_TRUE(context.got_result);
  EXPECT_TRUE(fidl_testing::ArenaChecker::DidUse(arena));
}

TEST_F(WireSharedClientTest, OneWayCallerAllocate) {
  fidl::AsyncClientBuffer<test::Frobinator::Grob> buffer;
  fidl::WireSharedClient client(std::move(client_end()), loop()->dispatcher());

  fidl::Status result = client.buffer(buffer.view())->Frob("test");
  loop()->RunUntilIdle();

  EXPECT_OK(result.status());
  EXPECT_EQ(1, frob_count());

  // Test multi-request syntax.
  fidl::Arena arena;
  auto buffered = client.buffer(arena);
  EXPECT_OK(buffered->Frob("test").status());
  EXPECT_OK(buffered->Frob("test").status());
  EXPECT_OK(buffered->Frob("test").status());
  loop()->RunUntilIdle();
  EXPECT_EQ(4, frob_count());
}

class WireSendEventTest : public CallerAllocatingFixtureWithDefaultServer {};

class ExpectHrobEventHandler : public fidl::WireAsyncEventHandler<test::Frobinator> {
 public:
  explicit ExpectHrobEventHandler(std::string expected) : expected_(std::move(expected)) {}

  void Hrob(fidl::WireEvent<test::Frobinator::Hrob>* event) final {
    EXPECT_EQ(event->value.get(), expected_);
    hrob_count_++;
  }

  void on_fidl_error(fidl::UnbindInfo info) final {
    ADD_FAILURE("Unexpected error: %s", info.FormatDescription().c_str());
  }

  int hrob_count() const { return hrob_count_; }

 private:
  std::string expected_;
  int hrob_count_ = 0;
};

class ExpectPeerClosedEventHandler : public fidl::WireAsyncEventHandler<test::Frobinator> {
 public:
  ExpectPeerClosedEventHandler() = default;

  void Hrob(fidl::WireEvent<test::Frobinator::Hrob>* event) final {
    ADD_FAILURE("Unexpected Hrob event: %s", std::string(event->value.get()).c_str());
  }

  void on_fidl_error(fidl::UnbindInfo info) final {
    EXPECT_EQ(fidl::Reason::kPeerClosed, info.reason());
    peer_closed_ = true;
  }

  bool peer_closed() const { return peer_closed_; }

 private:
  bool peer_closed_ = false;
};

TEST_F(WireSendEventTest, ServerBindingRefCallerAllocate) {
  fidl::Arena arena;
  ExpectHrobEventHandler event_handler{"test"};
  fidl::WireClient client(std::move(client_end()), loop()->dispatcher(), &event_handler);
  fidl::Status result = fidl::WireSendEvent(binding_ref()).buffer(arena)->Hrob("test");

  EXPECT_OK(result.status());
  EXPECT_TRUE(fidl_testing::ArenaChecker::DidUse(arena));
  loop()->RunUntilIdle();
  EXPECT_EQ(1, event_handler.hrob_count());
}

TEST_F(WireSendEventTest, ServerBindingRefCallerAllocateInsufficientBufferSize) {
  uint8_t small_buffer[8];
  ExpectPeerClosedEventHandler event_handler;
  fidl::WireClient client(std::move(client_end()), loop()->dispatcher(), &event_handler);
  fidl::Status result = fidl::WireSendEvent(binding_ref())
                            .buffer(fidl::BufferSpan(small_buffer, sizeof(small_buffer)))
                            ->Hrob("test");

  EXPECT_STATUS(ZX_ERR_BUFFER_TOO_SMALL, result.status());
  EXPECT_EQ(fidl::Reason::kEncodeError, result.reason());
  // Server is unbound due to the error.
  loop()->RunUntilIdle();
  EXPECT_TRUE(event_handler.peer_closed());
  fidl::Status error = fidl::WireSendEvent(binding_ref())->Hrob("test");
  EXPECT_TRUE(error.is_canceled());
}

TEST(WireSendEventTest, ServerEndCallerAllocate) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  fidl::ServerEnd<test::Frobinator> server_end;
  zx::result client_end = fidl::CreateEndpoints(&server_end);
  ASSERT_OK(client_end.status_value());
  ExpectHrobEventHandler event_handler{"test"};
  fidl::WireClient client(std::move(*client_end), loop.dispatcher(), &event_handler);

  fidl::Arena arena;
  fidl::Status result = fidl::WireSendEvent(server_end).buffer(arena)->Hrob("test");

  EXPECT_OK(result.status());
  EXPECT_TRUE(fidl_testing::ArenaChecker::DidUse(arena));
  loop.RunUntilIdle();
  EXPECT_EQ(1, event_handler.hrob_count());
}

TEST(WireSendEventTest, ServerEndCallerAllocateInsufficientBufferSize) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  fidl::ServerEnd<test::Frobinator> server_end;
  zx::result client_end = fidl::CreateEndpoints(&server_end);
  ASSERT_OK(client_end.status_value());
  ExpectHrobEventHandler event_handler{"test"};
  fidl::WireClient client(std::move(*client_end), loop.dispatcher(), &event_handler);

  uint8_t small_buffer[8];
  fidl::Status result = fidl::WireSendEvent(server_end)
                            .buffer(fidl::BufferSpan(small_buffer, sizeof(small_buffer)))
                            ->Hrob("test");

  EXPECT_STATUS(ZX_ERR_BUFFER_TOO_SMALL, result.status());
  EXPECT_EQ(fidl::Reason::kEncodeError, result.reason());
  loop.RunUntilIdle();
  EXPECT_EQ(0, event_handler.hrob_count());
}

class CallerAllocatingFixtureWithCallerAllocatingServer : public CallerAllocatingFixture {
 public:
  using GrobHandler =
      fit::callback<void(Frobinator::GrobRequestView, Frobinator::GrobCompleter::Sync&)>;

  class CallerAllocatingFrobinator : public Frobinator {
   public:
    void Frob(FrobRequestView request, FrobCompleter::Sync& completer) override {
      ZX_PANIC("Unused");
    }

    void Grob(GrobRequestView request, GrobCompleter::Sync& completer) override {
      grob_handler(request, completer);
    }

    void TwoWayEmptyArg(TwoWayEmptyArgCompleter::Sync& completer) override { ZX_PANIC("Unused"); }

    size_t frob_count() const override { return 0; }

    GrobHandler grob_handler;
  };

  std::shared_ptr<Frobinator> GetServer() override { return impl_; }

  void set_grob_handler(GrobHandler handler) { impl_->grob_handler = std::move(handler); }

 private:
  std::shared_ptr<CallerAllocatingFrobinator> impl_ =
      std::make_shared<CallerAllocatingFrobinator>();
};

class WireCompleterTest : public CallerAllocatingFixtureWithCallerAllocatingServer {};

TEST_F(WireCompleterTest, CallerAllocateBufferSpan) {
  set_grob_handler(
      [](Frobinator::GrobRequestView request, Frobinator::GrobCompleter::Sync& completer) {
        fidl::ServerBuffer<test::Frobinator::Grob> buffer;
        completer.buffer(buffer.view()).Reply(request->value);
      });
  fidl::WireClient client(std::move(client_end()), loop()->dispatcher());
  bool called = false;
  client->Grob("test").ThenExactlyOnce(
      [&](fidl::WireUnownedResult<test::Frobinator::Grob>& result) {
        called = true;
        ASSERT_OK(result.status());
        EXPECT_EQ("test", result.value().value.get());
      });
  EXPECT_OK(loop()->RunUntilIdle());
  EXPECT_TRUE(called);
}

TEST_F(WireCompleterTest, CallerAllocateArena) {
  set_grob_handler(
      [](Frobinator::GrobRequestView request, Frobinator::GrobCompleter::Sync& completer) {
        fidl::Arena arena;
        completer.buffer(arena).Reply(request->value);
      });
  fidl::WireClient client(std::move(client_end()), loop()->dispatcher());
  bool called = false;
  client->Grob("test").ThenExactlyOnce(
      [&](fidl::WireUnownedResult<test::Frobinator::Grob>& result) {
        called = true;
        ASSERT_OK(result.status());
        EXPECT_EQ("test", result.value().value.get());
      });
  EXPECT_OK(loop()->RunUntilIdle());
  EXPECT_TRUE(called);
}

TEST_F(WireCompleterTest, CallerAllocateInsufficientBufferSize) {
  set_grob_handler(
      [](Frobinator::GrobRequestView request, Frobinator::GrobCompleter::Sync& completer) {
        FIDL_ALIGNDECL uint8_t small[8];
        fidl::BufferSpan small_buf(small, sizeof(small));
        completer.buffer(small_buf).Reply(request->value);
        fidl::Status result = completer.result_of_reply();
        EXPECT_STATUS(ZX_ERR_BUFFER_TOO_SMALL, result.status());
        EXPECT_EQ(fidl::Reason::kEncodeError, result.reason());
      });
  fidl::WireClient client(std::move(client_end()), loop()->dispatcher());
  bool called = false;
  client->Grob("test").ThenExactlyOnce(
      [&](fidl::WireUnownedResult<test::Frobinator::Grob>& result) {
        called = true;
        ASSERT_STATUS(ZX_ERR_PEER_CLOSED, result.status());
      });
  EXPECT_OK(loop()->RunUntilIdle());
  EXPECT_TRUE(called);
}

}  // namespace
