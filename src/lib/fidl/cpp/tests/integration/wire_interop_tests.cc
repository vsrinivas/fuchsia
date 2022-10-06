// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fidl.cpp.wire.interop.test/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/cpp/client.h>
#include <lib/stdcompat/string_view.h>
#include <zircon/assert.h>

#include <vector>

#include <zxtest/zxtest.h>

namespace {

class WireTestBase : public fidl::WireServer<fidl_cpp_wire_interop_test::Interop> {
  void RoundTrip(RoundTripRequestView request, RoundTripCompleter::Sync& completer) override {
    ZX_PANIC("Unreachable");
  }
  void TryRoundTrip(TryRoundTripRequestView request,
                    TryRoundTripCompleter::Sync& completer) override {
    ZX_PANIC("Unreachable");
  }
  void OneWay(OneWayRequestView request, OneWayCompleter::Sync& completer) override {
    ZX_PANIC("Unreachable");
  }
};

class MockData {
 public:
  // Helpers to build mock domain objects and test that they are equal to expected.
  static fidl_cpp_wire_interop_test::Node MakeNaturalFile();
  static fidl_cpp_wire_interop_test::wire::Node MakeWireFile(fidl::AnyArena& arena);
  static void CheckNaturalFile(const fidl_cpp_wire_interop_test::Node& node);
  static void CheckWireFile(const fidl_cpp_wire_interop_test::wire::Node& node);

  static fidl_cpp_wire_interop_test::Node MakeNaturalDir();
  static fidl_cpp_wire_interop_test::wire::Node MakeWireDir(fidl::AnyArena& arena);
  static void CheckNaturalDir(const fidl_cpp_wire_interop_test::Node& node);
  static void CheckWireDir(const fidl_cpp_wire_interop_test::wire::Node& node);

 private:
  const static char kFileName[9];
  static std::vector<uint8_t> kFileContent;
  static const char kDirName[8];
};

const char MockData::kFileName[9] = "foo file";
std::vector<uint8_t> MockData::kFileContent = {1, 2, 3};
const char MockData::kDirName[8] = "bar dir";

fidl_cpp_wire_interop_test::Node MockData::MakeNaturalFile() {
  fidl_cpp_wire_interop_test::Node node;
  node.name() = kFileName;
  node.kind() = fidl_cpp_wire_interop_test::Kind::WithFile({{.content = kFileContent}});
  return node;
}

fidl_cpp_wire_interop_test::wire::Node MockData::MakeWireFile(fidl::AnyArena& arena) {
  auto kind = fidl_cpp_wire_interop_test::wire::Kind::WithFile(arena);
  kind.file().content = fidl::VectorView<uint8_t>::FromExternal(kFileContent);

  return fidl_cpp_wire_interop_test::wire::Node::Builder(arena).name(kFileName).kind(kind).Build();
}

void MockData::CheckNaturalFile(const fidl_cpp_wire_interop_test::Node& node) {
  ASSERT_TRUE(node.name().has_value());
  EXPECT_EQ(kFileName, node.name());
  ASSERT_TRUE(node.kind().has_value());
  EXPECT_EQ(fidl_cpp_wire_interop_test::Kind::Tag::kFile, node.kind()->Which());
  EXPECT_EQ(kFileContent, node.kind()->file()->content());
}

void MockData::CheckWireFile(const fidl_cpp_wire_interop_test::wire::Node& node) {
  ASSERT_TRUE(node.has_name());
  EXPECT_EQ(fidl::StringView{kFileName}.get(), node.name().get());
  ASSERT_TRUE(node.has_kind());
  EXPECT_EQ(fidl_cpp_wire_interop_test::wire::Kind::Tag::kFile, node.kind().Which());
  std::vector<uint8_t> content(node.kind().file().content.begin(),
                               node.kind().file().content.end());
  EXPECT_EQ(kFileContent, content);
}

fidl_cpp_wire_interop_test::Node MockData::MakeNaturalDir() {
  fidl_cpp_wire_interop_test::Node node;
  node.name() = kDirName;

  fidl_cpp_wire_interop_test::Node child = MakeNaturalFile();
  fidl_cpp_wire_interop_test::Directory directory;
  directory.children() = std::make_unique<fidl_cpp_wire_interop_test::Children>();
  directory.children()->elements().emplace_back(std::move(child));
  node.kind() = fidl_cpp_wire_interop_test::Kind::WithDirectory(std::move(directory));
  return node;
}

fidl_cpp_wire_interop_test::wire::Node MockData::MakeWireDir(fidl::AnyArena& arena) {
  auto node = fidl_cpp_wire_interop_test::wire::Node::Builder(arena)
                  .name(kDirName)
                  .kind(fidl_cpp_wire_interop_test::wire::Kind::WithDirectory(arena))
                  .Build();

  fidl::ObjectView<fidl_cpp_wire_interop_test::wire::Children>& children =
      node.kind().directory().children;
  children.Allocate(arena);
  children->elements.Allocate(arena, 1);
  children->elements[0] = MakeWireFile(arena);
  return node;
}

void MockData::CheckNaturalDir(const fidl_cpp_wire_interop_test::Node& node) {
  ASSERT_TRUE(node.name().has_value());
  EXPECT_EQ(kDirName, node.name());
  ASSERT_TRUE(node.kind().has_value());
  ASSERT_EQ(fidl_cpp_wire_interop_test::Kind::Tag::kDirectory, node.kind()->Which());

  const fidl_cpp_wire_interop_test::Directory& dir = node.kind()->directory().value();
  EXPECT_EQ(1, dir.children()->elements().size());
  const fidl_cpp_wire_interop_test::Node& child = dir.children()->elements()[0];
  CheckNaturalFile(child);
}

void MockData::CheckWireDir(const fidl_cpp_wire_interop_test::wire::Node& node) {
  EXPECT_TRUE(node.has_name());
  EXPECT_EQ(fidl::StringView{kDirName}.get(), node.name().get());
  EXPECT_TRUE(node.has_kind());
  EXPECT_EQ(fidl_cpp_wire_interop_test::wire::Kind::Tag::kDirectory, node.kind().Which());
  const fidl_cpp_wire_interop_test::wire::Directory& dir = node.kind().directory();
  EXPECT_EQ(1, dir.children->elements.count());
  const fidl_cpp_wire_interop_test::wire::Node& child = dir.children->elements[0];
  CheckWireFile(child);
}

// Test fixture to simplify creating endpoints and a unified client to talk to
// a wire domain object server.
class UnifiedClientToWireServerBase : public zxtest::Test, public MockData {
 public:
  UnifiedClientToWireServerBase() : loop_(&kAsyncLoopConfigNeverAttachToThread) {}

  void SetUp() final {
    zx::status client_end =
        fidl::CreateEndpoints<fidl_cpp_wire_interop_test::Interop>(&server_end_);
    ASSERT_OK(client_end.status_value());
    client_.Bind(std::move(*client_end), loop_.dispatcher(), GetEventHandler());
  }

  virtual fidl::AsyncEventHandler<fidl_cpp_wire_interop_test::Interop>* GetEventHandler() = 0;

  async::Loop& loop() { return loop_; }
  fidl::ServerEnd<fidl_cpp_wire_interop_test::Interop>& server_end() { return server_end_; }
  fidl::Client<fidl_cpp_wire_interop_test::Interop>& client() { return client_; }

 private:
  async::Loop loop_;
  fidl::ServerEnd<fidl_cpp_wire_interop_test::Interop> server_end_;
  fidl::Client<fidl_cpp_wire_interop_test::Interop> client_;
};

// Test fixture that does not care about events (besides the fact that there
// should not be any errors).
class UnifiedClientToWireServer : public UnifiedClientToWireServerBase {
 private:
  fidl::AsyncEventHandler<fidl_cpp_wire_interop_test::Interop>* GetEventHandler() final {
    return &event_handler_;
  }

  class FailOnClientError : public fidl::AsyncEventHandler<fidl_cpp_wire_interop_test::Interop> {
    // We should not observe any terminal error from the client during these tests.
    void on_fidl_error(fidl::UnbindInfo info) final {
      ADD_FATAL_FAILURE("Detected client error during test: %s", info.FormatDescription().c_str());
    }
  };

  FailOnClientError event_handler_;
};

// Test round-tripping a file node.
TEST_F(UnifiedClientToWireServer, RoundTrip) {
  class Server : public WireTestBase {
   public:
    void RoundTrip(RoundTripRequestView request, RoundTripCompleter::Sync& completer) final {
      CheckWireFile(request->node);
      num_calls++;
      completer.Reply(request->node);
    }

    int num_calls = 0;
  };
  Server server;
  fidl::BindServer(loop().dispatcher(), std::move(server_end()), &server);

  {
    // Test with natural domain objects.
    auto node = MakeNaturalFile();
    fidl_cpp_wire_interop_test::InteropRoundTripRequest request{std::move(node)};
    bool got_response = false;
    client()
        ->RoundTrip(std::move(request))
        .ThenExactlyOnce([&](fidl::Result<fidl_cpp_wire_interop_test::Interop::RoundTrip>& result) {
          ASSERT_TRUE(result.is_ok());
          CheckNaturalFile(result->node());
          got_response = true;
        });
    ASSERT_OK(loop().RunUntilIdle());
    EXPECT_EQ(1, server.num_calls);
    EXPECT_TRUE(got_response);
  }

  {
    // Test with wire domain objects.
    fidl::Arena arena;
    auto node = MakeWireFile(arena);
    bool got_response = false;
    client().wire()->RoundTrip(node).ThenExactlyOnce(
        [&](fidl::WireUnownedResult<fidl_cpp_wire_interop_test::Interop::RoundTrip>& result) {
          if (!result.ok()) {
            FAIL("RoundTrip failed: %s", result.error().FormatDescription().c_str());
            return;
          }
          auto* response = result.Unwrap();
          CheckWireFile(response->node);
          got_response = true;
        });
    ASSERT_OK(loop().RunUntilIdle());
    EXPECT_EQ(2, server.num_calls);
    EXPECT_TRUE(got_response);
  }
}

// Test round-tripping a directory node with error syntax.
TEST_F(UnifiedClientToWireServer, TryRoundTrip) {
  class Server : public WireTestBase {
   public:
    void TryRoundTrip(TryRoundTripRequestView request,
                      TryRoundTripCompleter::Sync& completer) final {
      CheckWireDir(request->node);
      num_calls++;
      if (reply_with_error) {
        completer.ReplyError(ZX_ERR_INVALID_ARGS);
      } else {
        completer.ReplySuccess(request->node);
      }
    }

    bool reply_with_error = false;
    int num_calls = 0;
  };
  Server server;
  fidl::BindServer(loop().dispatcher(), std::move(server_end()), &server);

  {
    // Test with natural domain objects, success case.
    auto node = MakeNaturalDir();
    fidl_cpp_wire_interop_test::InteropTryRoundTripRequest request{std::move(node)};
    bool got_response = false;
    client()
        ->TryRoundTrip(std::move(request))
        .ThenExactlyOnce(
            [&](fidl::Result<fidl_cpp_wire_interop_test::Interop::TryRoundTrip>& result) {
              ASSERT_TRUE(result.is_ok());
              fidl_cpp_wire_interop_test::InteropTryRoundTripResponse payload =
                  std::move(result.value());
              fidl_cpp_wire_interop_test::Node node = payload.node();
              CheckNaturalDir(node);
              got_response = true;
            });
    ASSERT_OK(loop().RunUntilIdle());
    EXPECT_EQ(1, server.num_calls);
    EXPECT_TRUE(got_response);
  }

  {
    // Test with wire domain objects, success case.
    fidl::Arena arena;
    auto node = MakeWireDir(arena);
    bool got_response = false;
    client().wire()->TryRoundTrip(node).ThenExactlyOnce(
        [&](fidl::WireUnownedResult<fidl_cpp_wire_interop_test::Interop::TryRoundTrip>& result) {
          if (!result.ok()) {
            FAIL("TryRoundTrip failed: %s", result.error().FormatDescription().c_str());
            return;
          }
          auto* response = result.Unwrap();
          ASSERT_TRUE(response->is_ok());
          CheckWireDir(response->value()->node);
          got_response = true;
        });
    ASSERT_OK(loop().RunUntilIdle());
    EXPECT_EQ(2, server.num_calls);
    EXPECT_TRUE(got_response);
  }

  server.reply_with_error = true;

  {
    // Test with natural domain objects, error case.
    auto node = MakeNaturalDir();
    fidl_cpp_wire_interop_test::InteropTryRoundTripRequest request{std::move(node)};
    bool got_response = false;
    client()
        ->TryRoundTrip(std::move(request))
        .ThenExactlyOnce(
            [&](fidl::Result<fidl_cpp_wire_interop_test::Interop::TryRoundTrip>& result) {
              ASSERT_FALSE(result.is_ok());
              ASSERT_TRUE(result.is_error());
              fidl::AnyErrorIn<fidl_cpp_wire_interop_test::Interop::TryRoundTrip> error =
                  result.error_value();
              ASSERT_TRUE(error.is_domain_error());
              EXPECT_STATUS(ZX_ERR_INVALID_ARGS, error.domain_error());
              got_response = true;
            });
    ASSERT_OK(loop().RunUntilIdle());
    EXPECT_EQ(3, server.num_calls);
    EXPECT_TRUE(got_response);
  }

  {
    // Test with wire domain objects, error case.
    fidl::Arena arena;
    auto node = MakeWireDir(arena);
    bool got_response = false;
    client().wire()->TryRoundTrip(node).ThenExactlyOnce(
        [&](fidl::WireUnownedResult<fidl_cpp_wire_interop_test::Interop::TryRoundTrip>& result) {
          if (!result.ok()) {
            FAIL("TryRoundTrip failed: %s", result.error().FormatDescription().c_str());
            return;
          }
          auto* response = result.Unwrap();
          ASSERT_TRUE(response->is_error());
          EXPECT_STATUS(ZX_ERR_INVALID_ARGS, response->error_value());
          got_response = true;
        });
    ASSERT_OK(loop().RunUntilIdle());
    EXPECT_EQ(4, server.num_calls);
    EXPECT_TRUE(got_response);
  }
}

// Test sending a one way call.
TEST_F(UnifiedClientToWireServer, OneWay) {
  class Server : public WireTestBase {
   public:
    void OneWay(OneWayRequestView request, OneWayCompleter::Sync& completer) override {
      CheckWireFile(request->node);
      num_calls++;
    }

    int num_calls = 0;
  };
  Server server;
  fidl::BindServer(loop().dispatcher(), std::move(server_end()), &server);
  {
    // Test with natural domain objects.
    fit::result<fidl::Error> result = client()->OneWay({MakeNaturalFile()});
    ASSERT_TRUE(result.is_ok());
    ASSERT_OK(loop().RunUntilIdle());
    EXPECT_EQ(1, server.num_calls);
  }
  {
    // Test with wire domain objects.
    fidl::Arena arena;
    fidl::Status status = client().wire()->OneWay(MakeWireFile(arena));
    ASSERT_TRUE(status.ok());
    ASSERT_OK(loop().RunUntilIdle());
    EXPECT_EQ(2, server.num_calls);
  }
}

// Test fixture that checks events.
class UnifiedClientToWireServerWithEventHandler : public UnifiedClientToWireServerBase {
 public:
  int num_events() const { return event_handler_.num_events(); }

 private:
  class ExpectOnNodeEvent : public fidl::AsyncEventHandler<fidl_cpp_wire_interop_test::Interop> {
   public:
    int num_events() const { return num_events_; }

   private:
    // We should not observe any terminal error from the client during these tests.
    void on_fidl_error(fidl::UnbindInfo info) final {
      ADD_FATAL_FAILURE("Detected client error during test: %s", info.FormatDescription().c_str());
    }

    void OnNode(fidl::Event<fidl_cpp_wire_interop_test::Interop::OnNode>& event) final {
      CheckNaturalDir(event.node());
      num_events_++;
    }

    int num_events_ = 0;
  };

  fidl::AsyncEventHandler<fidl_cpp_wire_interop_test::Interop>* GetEventHandler() final {
    return &event_handler_;
  }

  ExpectOnNodeEvent event_handler_;
};

TEST_F(UnifiedClientToWireServerWithEventHandler, OnNode) {
  class Server : public WireTestBase {};
  Server server;
  auto binding = fidl::BindServer(loop().dispatcher(), std::move(server_end()), &server);

  EXPECT_EQ(0, num_events());

  // Send an event.
  fidl::Arena arena;
  auto node = MakeWireDir(arena);
  fidl::Status status = fidl::WireSendEvent(binding)->OnNode(node);
  ASSERT_OK(status.status());

  // Test receiving natural domain objects.
  ASSERT_OK(loop().RunUntilIdle());
  EXPECT_EQ(1, num_events());
}

class NaturalTestBase : public fidl::Server<fidl_cpp_wire_interop_test::Interop> {
  void RoundTrip(RoundTripRequest& request, RoundTripCompleter::Sync& completer) override {
    ZX_PANIC("Unreachable");
  }
  void TryRoundTrip(TryRoundTripRequest& request, TryRoundTripCompleter::Sync& completer) override {
    ZX_PANIC("Unreachable");
  }
  void OneWay(OneWayRequest& request, OneWayCompleter::Sync& completer) override {
    ZX_PANIC("Unreachable");
  }
};

// Test fixture to simplify creating endpoints and a wire client to talk to
// a natural server.
class WireClientToNaturalServerBase : public zxtest::Test, public MockData {
 public:
  WireClientToNaturalServerBase() : loop_(&kAsyncLoopConfigNeverAttachToThread) {}

  void SetUp() final {
    zx::status client_end =
        fidl::CreateEndpoints<fidl_cpp_wire_interop_test::Interop>(&server_end_);
    ASSERT_OK(client_end.status_value());
    client_.Bind(std::move(*client_end), loop_.dispatcher(), GetEventHandler());
  }

  template <typename ServerImpl>
  static auto CheckErrorsWhenUnbound() {
    return [](ServerImpl* impl, fidl::UnbindInfo info,
              fidl::ServerEnd<fidl_cpp_wire_interop_test::Interop> server_end) {
      if (info.is_user_initiated())
        return;
      if (info.is_dispatcher_shutdown())
        return;
      if (info.is_peer_closed())
        return;

      ADD_FATAL_FAILURE("Detected server error during test: %s", info.FormatDescription().c_str());
    };
  }

  virtual fidl::WireAsyncEventHandler<fidl_cpp_wire_interop_test::Interop>* GetEventHandler() = 0;

  async::Loop& loop() { return loop_; }
  fidl::ServerEnd<fidl_cpp_wire_interop_test::Interop>& server_end() { return server_end_; }
  fidl::WireClient<fidl_cpp_wire_interop_test::Interop>& client() { return client_; }

 private:
  async::Loop loop_;
  fidl::ServerEnd<fidl_cpp_wire_interop_test::Interop> server_end_;
  fidl::WireClient<fidl_cpp_wire_interop_test::Interop> client_;
};

// Test fixture that does not care about events (besides the fact that there
// should not be any errors).
class WireClientToNaturalServer : public WireClientToNaturalServerBase {
 private:
  fidl::WireAsyncEventHandler<fidl_cpp_wire_interop_test::Interop>* GetEventHandler() final {
    return &event_handler_;
  }

  class FailOnClientError
      : public fidl::WireAsyncEventHandler<fidl_cpp_wire_interop_test::Interop> {
    // We should not observe any terminal error from the client during these tests.
    void on_fidl_error(fidl::UnbindInfo info) final {
      ADD_FATAL_FAILURE("Detected client error during test: %s", info.FormatDescription().c_str());
    }
  };

  FailOnClientError event_handler_;
};

// Test round-tripping a file node.
TEST_F(WireClientToNaturalServer, RoundTrip) {
  class Server : public NaturalTestBase {
   public:
    void RoundTrip(RoundTripRequest& request, RoundTripCompleter::Sync& completer) final {
      CheckNaturalFile(request.node());
      num_calls++;
      completer.Reply(std::move(request.node()));
    }

    int num_calls = 0;
  };
  Server server;
  fidl::BindServer(loop().dispatcher(), std::move(server_end()), &server,
                   CheckErrorsWhenUnbound<Server>());

  fidl::Arena arena;
  auto node = MakeWireFile(arena);
  bool got_response = false;
  client()->RoundTrip(node).ThenExactlyOnce(
      [&](fidl::WireUnownedResult<fidl_cpp_wire_interop_test::Interop::RoundTrip>& result) {
        if (!result.ok()) {
          FAIL("RoundTrip failed: %s", result.error().FormatDescription().c_str());
          return;
        }
        auto* response = result.Unwrap();
        CheckWireFile(response->node);
        got_response = true;
      });
  ASSERT_OK(loop().RunUntilIdle());
  EXPECT_EQ(1, server.num_calls);
  EXPECT_TRUE(got_response);
}

// Test round-tripping a directory node with error syntax.
TEST_F(WireClientToNaturalServer, TryRoundTrip) {
  class Server : public NaturalTestBase {
   public:
    void TryRoundTrip(TryRoundTripRequest& request, TryRoundTripCompleter::Sync& completer) final {
      CheckNaturalDir(request.node());
      num_calls++;
      // TODO(fxbug.dev/91363): ReplySuccess/ReplyError.
      if (reply_with_error) {
        completer.Reply(fit::error(ZX_ERR_INVALID_ARGS));
      } else {
        completer.Reply(fit::ok(std::move(request.node())));
      }
    }

    bool reply_with_error = false;
    int num_calls = 0;
  };
  Server server;
  fidl::BindServer(loop().dispatcher(), std::move(server_end()), &server,
                   CheckErrorsWhenUnbound<Server>());

  {
    // Test success case.
    fidl::Arena arena;
    auto node = MakeWireDir(arena);
    bool got_response = false;
    client()->TryRoundTrip(node).ThenExactlyOnce(
        [&](fidl::WireUnownedResult<fidl_cpp_wire_interop_test::Interop::TryRoundTrip>& result) {
          if (!result.ok()) {
            FAIL("TryRoundTrip failed: %s", result.error().FormatDescription().c_str());
            return;
          }
          auto* response = result.Unwrap();
          ASSERT_TRUE(response->is_ok());
          CheckWireDir(response->value()->node);
          got_response = true;
        });
    ASSERT_OK(loop().RunUntilIdle());
    EXPECT_EQ(1, server.num_calls);
    EXPECT_TRUE(got_response);
  }

  server.reply_with_error = true;

  {
    // Test error case.
    fidl::Arena arena;
    auto node = MakeWireDir(arena);
    bool got_response = false;
    client()->TryRoundTrip(node).ThenExactlyOnce(
        [&](fidl::WireUnownedResult<fidl_cpp_wire_interop_test::Interop::TryRoundTrip>& result) {
          if (!result.ok()) {
            FAIL("TryRoundTrip failed: %s", result.error().FormatDescription().c_str());
            return;
          }
          auto* response = result.Unwrap();
          ASSERT_TRUE(response->is_error());
          EXPECT_STATUS(ZX_ERR_INVALID_ARGS, response->error_value());
          got_response = true;
        });
    ASSERT_OK(loop().RunUntilIdle());
    EXPECT_EQ(2, server.num_calls);
    EXPECT_TRUE(got_response);
  }
}

// Test receiving a one way call.
TEST_F(WireClientToNaturalServer, OneWay) {
  class Server : public NaturalTestBase {
   public:
    void OneWay(OneWayRequest& request, OneWayCompleter::Sync& completer) override {
      CheckNaturalFile(request.node());
      num_calls++;
    }

    int num_calls = 0;
  };
  Server server;
  fidl::BindServer(loop().dispatcher(), std::move(server_end()), &server,
                   CheckErrorsWhenUnbound<Server>());

  fidl::Arena arena;
  fidl::Status status = client()->OneWay(MakeWireFile(arena));
  ASSERT_TRUE(status.ok());
  ASSERT_OK(loop().RunUntilIdle());
  EXPECT_EQ(1, server.num_calls);
}

// Test fixture that checks events.
class WireClientToNaturalServerWithEventHandler : public WireClientToNaturalServerBase {
 public:
  int num_events() const { return event_handler_.num_events(); }

 private:
  class ExpectOnNodeEvent
      : public fidl::WireAsyncEventHandler<fidl_cpp_wire_interop_test::Interop> {
   public:
    int num_events() const { return num_events_; }

   private:
    // We should not observe any terminal error from the client during these tests.
    void on_fidl_error(fidl::UnbindInfo info) final {
      ADD_FATAL_FAILURE("Detected client error during test: %s", info.FormatDescription().c_str());
    }

    void OnNode(fidl::WireEvent<fidl_cpp_wire_interop_test::Interop::OnNode>* event) final {
      CheckWireDir(event->node);
      num_events_++;
    }

    int num_events_ = 0;
  };

  fidl::WireAsyncEventHandler<fidl_cpp_wire_interop_test::Interop>* GetEventHandler() final {
    return &event_handler_;
  }

  ExpectOnNodeEvent event_handler_;
};

// Test sending an event over a |fidl::ServerEnd|.
TEST_F(WireClientToNaturalServerWithEventHandler, SendOnNodeEventOverServerEnd) {
  EXPECT_EQ(0, num_events());

  // Test sending the event with natural types.
  {
    fidl_cpp_wire_interop_test::Node node = MakeNaturalDir();
    fit::result result = fidl::SendEvent(server_end())->OnNode(node);
    EXPECT_TRUE(result.is_ok(), "%s", result.error_value().FormatDescription().c_str());
    EXPECT_OK(loop().RunUntilIdle());
    EXPECT_EQ(1, num_events());
  }

  // Test sending the event with wire types.
  {
    fidl::Arena arena;
    fidl_cpp_wire_interop_test::wire::Node node = MakeWireDir(arena);
    fidl::Status status = fidl::WireSendEvent(server_end())->OnNode(node);
    EXPECT_OK(status.status());
    EXPECT_OK(loop().RunUntilIdle());
    EXPECT_EQ(2, num_events());
  }
}

// Test sending an event over a |fidl::ServerBindingRef|.
TEST_F(WireClientToNaturalServerWithEventHandler, SendOnNodeEventOverServerBindingRef) {
  EXPECT_EQ(0, num_events());

  class Server : public NaturalTestBase {};
  Server server;
  fidl::ServerBindingRef binding_ref =
      fidl::BindServer(loop().dispatcher(), std::move(server_end()), &server);

  // Test sending the event with natural types.
  {
    fidl_cpp_wire_interop_test::Node node = MakeNaturalDir();
    fit::result result = fidl::SendEvent(binding_ref)->OnNode(node);
    EXPECT_TRUE(result.is_ok(), "%s", result.error_value().FormatDescription().c_str());
    EXPECT_OK(loop().RunUntilIdle());
    EXPECT_EQ(1, num_events());
  }

  // Test sending the event with wire types.
  {
    fidl::Arena arena;
    fidl_cpp_wire_interop_test::wire::Node node = MakeWireDir(arena);
    fidl::Status status = fidl::WireSendEvent(binding_ref)->OnNode(node);
    EXPECT_OK(status.status());
    EXPECT_OK(loop().RunUntilIdle());
    EXPECT_EQ(2, num_events());
  }
}

// Test fixture to simplify creating endpoints and a unified client to talk to
// a wire domain object server.
class UnifiedSyncClientToWireServer : public zxtest::Test, public MockData {
 public:
  UnifiedSyncClientToWireServer() : loop_(&kAsyncLoopConfigNeverAttachToThread) {}

  void SetUp() final {
    zx::status client_end =
        fidl::CreateEndpoints<fidl_cpp_wire_interop_test::Interop>(&server_end_);
    ASSERT_OK(client_end.status_value());
    client_.Bind(std::move(*client_end));
  }

  async::Loop& loop() { return loop_; }
  fidl::ServerEnd<fidl_cpp_wire_interop_test::Interop>& server_end() { return server_end_; }
  fidl::SyncClient<fidl_cpp_wire_interop_test::Interop>& client() { return client_; }

 private:
  async::Loop loop_;
  fidl::ServerEnd<fidl_cpp_wire_interop_test::Interop> server_end_;
  fidl::SyncClient<fidl_cpp_wire_interop_test::Interop> client_;
};

TEST_F(UnifiedSyncClientToWireServer, RoundTrip) {
  class Server : public WireTestBase {
   public:
    void RoundTrip(RoundTripRequestView request, RoundTripCompleter::Sync& completer) final {
      CheckWireFile(request->node);
      num_calls++;
      completer.Reply(request->node);
    }

    int num_calls = 0;
  };
  Server server;
  fidl::BindServer(loop().dispatcher(), std::move(server_end()), &server);
  ASSERT_OK(loop().StartThread());

  {
    // Test with natural domain objects.
    auto node = MakeNaturalFile();
    fidl_cpp_wire_interop_test::InteropRoundTripRequest request{std::move(node)};

    fidl::Result<fidl_cpp_wire_interop_test::Interop::RoundTrip> result =
        client()->RoundTrip(std::move(request));

    ASSERT_TRUE(result.is_ok());
    CheckNaturalFile(result->node());
    EXPECT_EQ(1, server.num_calls);

    // Check that `fidl::Call` works with this one test (since they delegate
    // to the same implementation, we don't need to test `fidl::Call` everywhere).
    static_assert(std::is_same_v<cpp20::remove_cvref_t<decltype(fidl::Call(client().client_end()))>,
                                 cpp20::remove_cvref_t<decltype(client().operator->())>>,
                  "fidl::Call and fidl::SyncClient exposes the same impl");
    fidl::Result<fidl_cpp_wire_interop_test::Interop::RoundTrip> call_result =
        fidl::Call(client().client_end())->RoundTrip({MakeNaturalFile()});
    ASSERT_TRUE(call_result.is_ok());
    CheckNaturalFile(call_result->node());
    EXPECT_EQ(2, server.num_calls);
  }

  {
    // Test with wire domain objects.
    fidl::Arena arena;
    auto node = MakeWireFile(arena);

    fidl::WireResult<fidl_cpp_wire_interop_test::Interop::RoundTrip> result =
        client().wire()->RoundTrip(node);

    ASSERT_TRUE(result.ok(), "RoundTrip failed: %s", result.error().FormatDescription().c_str());
    auto* response = result.Unwrap();
    CheckWireFile(response->node);
    EXPECT_EQ(3, server.num_calls);

    // Check that `.wire().buffer()` exists with this one test
    // (caller-allocating flavors extensively tested elsewhere).
    fidl::WireUnownedResult<fidl_cpp_wire_interop_test::Interop::RoundTrip>
        caller_allocating_result = client().wire().buffer(arena)->RoundTrip(node);
    response = caller_allocating_result.Unwrap();
    ASSERT_TRUE(caller_allocating_result.ok());
    CheckWireFile(response->node);
    ASSERT_OK(loop().RunUntilIdle());
    EXPECT_EQ(4, server.num_calls);
  }
}

TEST_F(UnifiedSyncClientToWireServer, TryRoundTrip) {
  class Server : public WireTestBase {
   public:
    void TryRoundTrip(TryRoundTripRequestView request,
                      TryRoundTripCompleter::Sync& completer) final {
      CheckWireDir(request->node);
      num_calls++;
      if (reply_with_error.load()) {
        completer.ReplyError(ZX_ERR_INVALID_ARGS);
      } else {
        completer.ReplySuccess(request->node);
      }
    }

    std::atomic<bool> reply_with_error = false;
    int num_calls = 0;
  };
  Server server;
  fidl::BindServer(loop().dispatcher(), std::move(server_end()), &server);
  ASSERT_OK(loop().StartThread());

  {
    // Test with natural domain objects, success case.
    auto node = MakeNaturalDir();
    fidl_cpp_wire_interop_test::InteropTryRoundTripRequest request{std::move(node)};
    fidl::Result<fidl_cpp_wire_interop_test::Interop::TryRoundTrip> result =
        client()->TryRoundTrip(std::move(request));
    ASSERT_TRUE(result.is_ok());
    fidl_cpp_wire_interop_test::InteropTryRoundTripResponse payload = std::move(result.value());
    {
      fidl_cpp_wire_interop_test::Node node = payload.node();
      CheckNaturalDir(node);
      EXPECT_EQ(1, server.num_calls);
    }
  }

  {
    // Test with wire domain objects, success case.
    fidl::Arena arena;
    auto node = MakeWireDir(arena);
    fidl::WireResult<fidl_cpp_wire_interop_test::Interop::TryRoundTrip> result =
        client().wire()->TryRoundTrip(node);
    ASSERT_TRUE(result.ok(), "TryRoundTrip failed: %s", result.error().FormatDescription().c_str());
    auto* response = result.Unwrap();
    ASSERT_TRUE(response->is_ok());
    CheckWireDir(response->value()->node);
    EXPECT_EQ(2, server.num_calls);
  }

  server.reply_with_error.store(true);

  {
    // Test with natural domain objects, error case.
    auto node = MakeNaturalDir();
    fidl_cpp_wire_interop_test::InteropTryRoundTripRequest request{std::move(node)};
    fidl::Result<fidl_cpp_wire_interop_test::Interop::TryRoundTrip> result =
        client()->TryRoundTrip(std::move(request));
    ASSERT_FALSE(result.is_ok());
    ASSERT_TRUE(result.is_error());
    fidl::AnyErrorIn<fidl_cpp_wire_interop_test::Interop::TryRoundTrip> error =
        result.error_value();
    ASSERT_TRUE(error.is_domain_error());
    EXPECT_STATUS(ZX_ERR_INVALID_ARGS, error.domain_error());
    EXPECT_EQ(3, server.num_calls);
  }

  {
    // Test with wire domain objects, error case.
    fidl::Arena arena;
    auto node = MakeWireDir(arena);
    fidl::WireResult<fidl_cpp_wire_interop_test::Interop::TryRoundTrip> result =
        client().wire()->TryRoundTrip(node);
    ASSERT_TRUE(result.ok(), "TryRoundTrip failed: %s", result.error().FormatDescription().c_str());
    auto* response = result.Unwrap();
    ASSERT_TRUE(response->is_error());
    EXPECT_STATUS(ZX_ERR_INVALID_ARGS, response->error_value());
    EXPECT_EQ(4, server.num_calls);
  }
}

// Test sending a one way call.
TEST_F(UnifiedSyncClientToWireServer, OneWay) {
  class Server : public WireTestBase {
   public:
    void OneWay(OneWayRequestView request, OneWayCompleter::Sync& completer) override {
      CheckWireFile(request->node);
      num_calls++;
    }

    int num_calls = 0;
  };
  Server server;
  fidl::BindServer(loop().dispatcher(), std::move(server_end()), &server);
  {
    // Test with natural domain objects.
    fit::result<fidl::Error> result = client()->OneWay({MakeNaturalFile()});
    ASSERT_TRUE(result.is_ok());
    ASSERT_OK(loop().RunUntilIdle());
    EXPECT_EQ(1, server.num_calls);
  }
  {
    // Test with wire domain objects.
    fidl::Arena arena;
    fidl::Status status = client().wire()->OneWay(MakeWireFile(arena));
    ASSERT_TRUE(status.ok());
    ASSERT_OK(loop().RunUntilIdle());
    EXPECT_EQ(2, server.num_calls);
  }
}

TEST_F(UnifiedSyncClientToWireServer, OnNode) {
  fidl::Arena arena;

  class EventHandler : public fidl::SyncEventHandler<fidl_cpp_wire_interop_test::Interop> {
   public:
    void OnNode(::fidl::Event<::fidl_cpp_wire_interop_test::Interop::OnNode>& event) override {
      CheckNaturalDir(event.node());
      num_calls++;
    }

    int num_calls = 0;
  };
  EventHandler event_handler;

  ASSERT_OK(fidl::WireSendEvent(server_end())->OnNode(MakeWireDir(arena)));
  {
    // Event handler syntax.
    fidl::Status status = event_handler.HandleOneEvent(client().client_end());
    EXPECT_OK(status);
    EXPECT_EQ(1, event_handler.num_calls);
  }

  ASSERT_OK(fidl::WireSendEvent(server_end())->OnNode(MakeWireDir(arena)));
  {
    // Sync client syntax.
    fidl::Status status = client().HandleOneEvent(event_handler);
    EXPECT_OK(status);
    EXPECT_EQ(2, event_handler.num_calls);
  }
}

}  // namespace
