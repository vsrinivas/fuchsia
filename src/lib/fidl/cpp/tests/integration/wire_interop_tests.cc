// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/cpp/wire/interop/test/cpp/fidl_v2.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/cpp/client.h>
#include <lib/stdcompat/string_view.h>
#include <zircon/assert.h>

#include <vector>

#include <zxtest/zxtest.h>

namespace {

class TestBase : public fidl::WireServer<fidl_cpp_wire_interop_test::Interop> {
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

// Test fixture to simplify creating endpoints and a unified client to talk to
// a wire domain object server.
class UnifiedClientToWireServerBase : public zxtest::Test {
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
  async::Loop loop_;
  fidl::ServerEnd<fidl_cpp_wire_interop_test::Interop> server_end_;
  fidl::Client<fidl_cpp_wire_interop_test::Interop> client_;

  // Mock data.
  const static char kFileName[9];
  static std::vector<uint8_t> kFileContent;
  static const char kDirName[8];
};

const char UnifiedClientToWireServerBase::kFileName[9] = "foo file";
std::vector<uint8_t> UnifiedClientToWireServerBase::kFileContent = {1, 2, 3};
const char UnifiedClientToWireServerBase::kDirName[8] = "bar dir";

fidl_cpp_wire_interop_test::Node UnifiedClientToWireServerBase::MakeNaturalFile() {
  fidl_cpp_wire_interop_test::Node node;
  node.set_name(kFileName);
  fidl_cpp_wire_interop_test::Kind kind;
  kind.file().content = kFileContent;
  node.set_kind(std::move(kind));
  return node;
}

fidl_cpp_wire_interop_test::wire::Node UnifiedClientToWireServerBase::MakeWireFile(
    fidl::AnyArena& arena) {
  fidl_cpp_wire_interop_test::wire::Node node(arena);
  node.set_name(arena, kFileName);
  fidl_cpp_wire_interop_test::wire::Kind kind;
  kind.set_file(arena);
  kind.mutable_file().content = fidl::VectorView<uint8_t>::FromExternal(kFileContent);
  node.set_kind(arena, kind);
  return node;
}

void UnifiedClientToWireServerBase::CheckNaturalFile(const fidl_cpp_wire_interop_test::Node& node) {
  EXPECT_TRUE(node.has_name());
  EXPECT_EQ(kFileName, node.name());
  EXPECT_TRUE(node.has_kind());
  EXPECT_EQ(fidl_cpp_wire_interop_test::Kind::Tag::kFile, node.kind().Which());
  EXPECT_EQ(kFileContent, node.kind().file().content);
}

void UnifiedClientToWireServerBase::CheckWireFile(
    const fidl_cpp_wire_interop_test::wire::Node& node) {
  EXPECT_TRUE(node.has_name());
  EXPECT_EQ(fidl::StringView{kFileName}.get(), node.name().get());
  EXPECT_TRUE(node.has_kind());
  EXPECT_EQ(fidl_cpp_wire_interop_test::wire::Kind::Tag::kFile, node.kind().which());
  std::vector<uint8_t> content(node.kind().file().content.begin(),
                               node.kind().file().content.end());
  EXPECT_EQ(kFileContent, content);
}

fidl_cpp_wire_interop_test::Node UnifiedClientToWireServerBase::MakeNaturalDir() {
  fidl_cpp_wire_interop_test::Node node;
  node.set_name(kDirName);
  fidl_cpp_wire_interop_test::Kind kind;

  // TODO(fxbug.dev/82189): Use natural domain objects instead of HLCPP domain objects.
  fidl::cpp::wire::interop::test::Node child = MakeNaturalFile();
  kind.directory().children = std::make_unique<fidl::cpp::wire::interop::test::Children>();
  kind.directory().children->elements.emplace_back(std::move(child));
  node.set_kind(std::move(kind));
  return node;
}

fidl_cpp_wire_interop_test::wire::Node UnifiedClientToWireServerBase::MakeWireDir(
    fidl::AnyArena& arena) {
  fidl_cpp_wire_interop_test::wire::Node node(arena);
  node.set_name(arena, kDirName);
  fidl_cpp_wire_interop_test::wire::Kind kind;
  kind.set_directory(arena);
  node.set_kind(arena, kind);
  fidl::ObjectView<fidl_cpp_wire_interop_test::wire::Children>& children =
      kind.mutable_directory().children;
  children.Allocate(arena);
  children->elements.Allocate(arena, 1);
  children->elements[0] = MakeWireFile(arena);
  return node;
}

void UnifiedClientToWireServerBase::CheckNaturalDir(const fidl_cpp_wire_interop_test::Node& node) {
  EXPECT_TRUE(node.has_name());
  EXPECT_EQ(kDirName, node.name());
  EXPECT_TRUE(node.has_kind());
  EXPECT_EQ(fidl_cpp_wire_interop_test::Kind::Tag::kDirectory, node.kind().Which());

  const ::fidl::cpp::wire::interop::test::Directory& dir = node.kind().directory();
  EXPECT_EQ(1, dir.children->elements.size());
  const fidl_cpp_wire_interop_test::Node& child = dir.children->elements[0];
  CheckNaturalFile(child);
}

void UnifiedClientToWireServerBase::CheckWireDir(
    const fidl_cpp_wire_interop_test::wire::Node& node) {
  EXPECT_TRUE(node.has_name());
  EXPECT_EQ(fidl::StringView{kDirName}.get(), node.name().get());
  EXPECT_TRUE(node.has_kind());
  EXPECT_EQ(fidl_cpp_wire_interop_test::wire::Kind::Tag::kDirectory, node.kind().which());
  const fidl_cpp_wire_interop_test::wire::Directory& dir = node.kind().directory();
  EXPECT_EQ(1, dir.children->elements.count());
  const fidl_cpp_wire_interop_test::wire::Node& child = dir.children->elements[0];
  CheckWireFile(child);
}

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
  class Server : public TestBase {
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
    client()->RoundTrip(
        std::move(request),
        [&](fidl::Response<fidl_cpp_wire_interop_test::Interop::RoundTrip>& response) {
          CheckNaturalFile(response->node());
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
    client().wire()->RoundTrip(
        node, [&](fidl::WireResponse<fidl_cpp_wire_interop_test::Interop::RoundTrip>* response) {
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
  class Server : public TestBase {
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
    client()->TryRoundTrip(
        std::move(request),
        [&](fidl::Response<fidl_cpp_wire_interop_test::Interop::TryRoundTrip>& response) {
          // TODO(fxbug.dev/90111): Translate error syntax to `::fitx::result`.
          fpromise::result<::fidl::cpp::wire::interop::test::Node, zx_status_t> result =
              std::move(response->result());
          ASSERT_TRUE(result.is_ok());

          ::fidl::cpp::wire::interop::test::Node& node = result.value();
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
    client().wire()->TryRoundTrip(
        node, [&](fidl::WireResponse<fidl_cpp_wire_interop_test::Interop::TryRoundTrip>* response) {
          ASSERT_TRUE(response->result.is_response());
          CheckWireDir(response->result.response().node);
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
    client()->TryRoundTrip(
        std::move(request),
        [&](fidl::Response<fidl_cpp_wire_interop_test::Interop::TryRoundTrip>& response) {
          // TODO(fxbug.dev/90111): Translate error syntax to `::fitx::result`.
          fpromise::result<::fidl::cpp::wire::interop::test::Node, zx_status_t> result =
              std::move(response->result());
          ASSERT_TRUE(result.is_error());
          EXPECT_STATUS(ZX_ERR_INVALID_ARGS, result.error());
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
    client().wire()->TryRoundTrip(
        node, [&](fidl::WireResponse<fidl_cpp_wire_interop_test::Interop::TryRoundTrip>* response) {
          ASSERT_TRUE(response->result.is_err());
          EXPECT_STATUS(ZX_ERR_INVALID_ARGS, response->result.err());
          got_response = true;
        });
    ASSERT_OK(loop().RunUntilIdle());
    EXPECT_EQ(4, server.num_calls);
    EXPECT_TRUE(got_response);
  }
}

// Test sending a one way call.
TEST_F(UnifiedClientToWireServer, OneWay) {
  class Server : public TestBase {
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
    fitx::result<fidl::Error> result = client()->OneWay({MakeNaturalFile()});
    ASSERT_TRUE(result.is_ok());
    ASSERT_OK(loop().RunUntilIdle());
    EXPECT_EQ(1, server.num_calls);
  }

  {
    // Test with wire domain objects.
    fidl::Arena arena;
    fidl::Result result = client().wire()->OneWay(MakeWireFile(arena));
    ASSERT_TRUE(result.ok());
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
      CheckNaturalDir(event->node());
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
  class Server : public TestBase {};
  Server server;
  auto binding = fidl::BindServer(loop().dispatcher(), std::move(server_end()), &server);

  EXPECT_EQ(0, num_events());

  // Send an event.
  fidl::Arena arena;
  auto node = MakeWireDir(arena);
  fidl::Result result = binding->OnNode(node);
  ASSERT_OK(result.status());

  // Test receiving natural domain objects.
  ASSERT_OK(loop().RunUntilIdle());
  EXPECT_EQ(1, num_events());

  // TODO(fxbug.dev/60240): Send an event using natural objects.
}

}  // namespace
