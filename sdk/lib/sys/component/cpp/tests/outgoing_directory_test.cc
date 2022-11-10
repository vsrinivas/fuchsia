// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fidl/fuchsia.examples/cpp/fidl.h>
#include <fidl/fuchsia.io/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/component/incoming/cpp/service_client.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/namespace.h>
#include <lib/fidl/cpp/wire/client.h>
#include <lib/sync/cpp/completion.h>
#include <lib/sys/component/cpp/outgoing_directory.h>
#include <lib/zx/channel.h>
#include <lib/zx/handle.h>
#include <unistd.h>
#include <zircon/assert.h>
#include <zircon/errors.h>

#include <algorithm>
#include <array>
#include <memory>
#include <thread>
#include <utility>

#include <fbl/unique_fd.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <src/lib/storage/vfs/cpp/managed_vfs.h>
#include <src/lib/storage/vfs/cpp/pseudo_dir.h>
#include <src/lib/storage/vfs/cpp/pseudo_file.h>
#include <src/lib/testing/loop_fixture/real_loop_fixture.h>

namespace {

// Expected path of directory hosting FIDL Services & Protocols.
constexpr char kSvcDirectoryPath[] = "svc";

constexpr char kTestString[] = "FizzBuzz";
constexpr char kTestStringReversed[] = "zzuBzziF";

class EchoImpl final : public fidl::WireServer<fuchsia_examples::Echo> {
 public:
  explicit EchoImpl(bool reversed) : reversed_(reversed) {}

  void SendString(SendStringRequestView request, SendStringCompleter::Sync& completer) override {}

  void EchoString(EchoStringRequestView request, EchoStringCompleter::Sync& completer) override {
    std::string value(request->value.get());
    if (reversed_) {
      std::reverse(value.begin(), value.end());
    }
    auto reply = fidl::StringView::FromExternal(value);
    completer.Reply(reply);
  }

 private:
  bool reversed_ = false;
};

class NaturalEchoImpl final : public fidl::Server<fuchsia_examples::Echo> {
 public:
  explicit NaturalEchoImpl(bool reversed) : reversed_(reversed) {}

  void SendString(SendStringRequest& request, SendStringCompleter::Sync& completer) override {}

  void EchoString(EchoStringRequest& request, EchoStringCompleter::Sync& completer) override {
    std::string value(request.value());
    if (reversed_) {
      std::reverse(value.begin(), value.end());
    }
    completer.Reply(value);
  }

 private:
  bool reversed_ = false;
};

class OutgoingDirectoryTest : public gtest::RealLoopFixture {
 public:
  void SetUp() override {
    outgoing_directory_ = std::make_unique<component::OutgoingDirectory>(
        component::OutgoingDirectory::Create(dispatcher()));
    auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
    ZX_ASSERT(outgoing_directory_->Serve(std::move(endpoints->server)).is_ok());
    client_end_ = std::move(endpoints->client);
  }

  component::OutgoingDirectory* GetOutgoingDirectory() { return outgoing_directory_.get(); }

  fidl::ClientEnd<fuchsia_io::Directory> TakeSvcClientEnd(
      fidl::ClientEnd<fuchsia_io::Directory> root, fidl::StringView path = kSvcDirectoryPath) {
    zx::channel server_end, client_end;
    ZX_ASSERT(ZX_OK == zx::channel::create(0, &server_end, &client_end));
    // Check if this has already be initialized.
    if (!svc_client_.is_valid()) {
      svc_client_ = fidl::WireClient<fuchsia_io::Directory>(std::move(root), dispatcher());
    }

    auto status = svc_client_->Open(
        fuchsia_io::wire::OpenFlags::kRightWritable | fuchsia_io::wire::OpenFlags::kRightReadable,
        fuchsia_io::wire::kModeTypeDirectory, path,
        fidl::ServerEnd<fuchsia_io::Node>(std::move(server_end)));
    ZX_ASSERT_MSG(status.ok(), "Failed to open /%s client: %s", path.data(),
                  status.status_string());
    return fidl::ClientEnd<fuchsia_io::Directory>(std::move(client_end));
  }

  fidl::ClientEnd<fuchsia_io::Directory> TakeRootClientEnd() { return std::move(client_end_); }

 protected:
  fidl::WireClient<fuchsia_examples::Echo> ConnectToServiceMember(
      fuchsia_examples::EchoService::ServiceClient& service, bool reversed) {
    auto connect_result =
        reversed ? service.connect_reversed_echo() : service.connect_regular_echo();
    ZX_ASSERT(connect_result.is_ok());
    return fidl::WireClient<fuchsia_examples::Echo>(std::move(connect_result.value()),
                                                    dispatcher());
  }

  // Service handler that is pre-populated. This is only used for tests that
  // want to test failure paths.
  static fuchsia_examples::EchoService::InstanceHandler CreateNonEmptyServiceHandler() {
    return fuchsia_examples::EchoService::InstanceHandler({
        .regular_echo =
            [](fidl::ServerEnd<fuchsia_examples::Echo>) {
              // no op
            },
        .reversed_echo =
            [](fidl::ServerEnd<fuchsia_examples::Echo>) {
              // no op
            },
    });
  }

 private:
  std::unique_ptr<component::OutgoingDirectory> outgoing_directory_ = nullptr;
  fidl::ClientEnd<fuchsia_io::Directory> client_end_;
  fidl::WireClient<fuchsia_io::Directory> svc_client_;
};

TEST_F(OutgoingDirectoryTest, MutualExclusionGuarantees_CheckOperations) {
  auto outgoing_directory = std::make_unique<component::OutgoingDirectory>(
      component::OutgoingDirectory::Create(dispatcher()));

  // Cannot mutate it from a foreign thread.
  ASSERT_DEATH(
      {
        std::thread t([&] {
          EXPECT_NE(nullptr, outgoing_directory);
          EchoImpl impl(/*reversed=*/false);
          outgoing_directory->AddProtocol<fuchsia_examples::Echo>(&impl).status_value();
        });
        t.join();
      },
      "\\|component::OutgoingDirectory\\| is thread-unsafe\\.");

  // Cannot destroy it on a foreign thread.
  ASSERT_DEATH(
      {
        std::thread t([&] {
          EXPECT_NE(nullptr, outgoing_directory);
          std::unique_ptr destroy = std::move(outgoing_directory);
          destroy.reset();
        });
        t.join();
      },
      "\\|component::OutgoingDirectory\\| is thread-unsafe\\.");

  // Properly destroy it on the main thread.
  outgoing_directory.reset();
}

TEST_F(OutgoingDirectoryTest, CanBeMovedSafely) {
  auto outgoing_directory = component::OutgoingDirectory::Create(dispatcher());
  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  EchoImpl impl(/*reversed=*/false);
  ASSERT_EQ(outgoing_directory.AddProtocol<fuchsia_examples::Echo>(&impl).status_value(), ZX_OK);
  ZX_ASSERT(outgoing_directory.Serve(std::move(endpoints->server)).is_ok());

  component::OutgoingDirectory moved_in_constructor(std::move(outgoing_directory));
  component::OutgoingDirectory moved_in_assignment =
      component::OutgoingDirectory::Create(dispatcher());
  moved_in_assignment = std::move(moved_in_constructor);

  auto client_end =
      component::ConnectAt<fuchsia_examples::Echo>(TakeSvcClientEnd(std::move(endpoints->client)));
  ASSERT_EQ(client_end.status_value(), ZX_OK);
  fidl::WireClient<fuchsia_examples::Echo> client(std::move(*client_end), dispatcher());

  std::string reply_received;
  client->EchoString(kTestString)
      .ThenExactlyOnce([&reply_received, quit_loop = QuitLoopClosure()](
                           fidl::WireUnownedResult<fuchsia_examples::Echo::EchoString>& result) {
        ASSERT_TRUE(result.ok()) << "EchoString failed: " << result.error();
        auto* response = result.Unwrap();
        reply_received = std::string(response->response.data(), response->response.size());
        quit_loop();
      });
  RunLoop();

  EXPECT_EQ(reply_received, kTestString);
}

TEST_F(OutgoingDirectoryTest, AddProtocolWireServer) {
  // Setup fuchsia.examples.Echo server.
  EchoImpl impl(/*reversed=*/false);
  ASSERT_EQ(GetOutgoingDirectory()->AddProtocol<fuchsia_examples::Echo>(&impl).status_value(),
            ZX_OK);

  // Setup fuchsia.examples.Echo client.
  auto client_end =
      component::ConnectAt<fuchsia_examples::Echo>(TakeSvcClientEnd(TakeRootClientEnd()));
  ASSERT_EQ(client_end.status_value(), ZX_OK);
  fidl::WireClient<fuchsia_examples::Echo> client(std::move(*client_end), dispatcher());

  std::string reply_received;
  client->EchoString(kTestString)
      .ThenExactlyOnce([&reply_received, quit_loop = QuitLoopClosure()](
                           fidl::WireUnownedResult<fuchsia_examples::Echo::EchoString>& result) {
        ASSERT_TRUE(result.ok()) << "EchoString failed: " << result.error();
        auto* response = result.Unwrap();
        reply_received = std::string(response->response.data(), response->response.size());
        quit_loop();
      });
  RunLoop();

  EXPECT_EQ(reply_received, kTestString);
}

TEST_F(OutgoingDirectoryTest, AddProtocolNaturalServer) {
  // Setup fuchsia.examples.Echo server.
  NaturalEchoImpl impl(/*reversed=*/false);
  ASSERT_EQ(GetOutgoingDirectory()->AddProtocol<fuchsia_examples::Echo>(&impl).status_value(),
            ZX_OK);

  // Setup fuchsia.examples.Echo client.
  auto client_end =
      component::ConnectAt<fuchsia_examples::Echo>(TakeSvcClientEnd(TakeRootClientEnd()));
  ASSERT_EQ(client_end.status_value(), ZX_OK);
  fidl::Client<fuchsia_examples::Echo> client(std::move(*client_end), dispatcher());

  std::string reply_received;
  client->EchoString({kTestString})
      .ThenExactlyOnce([&reply_received, quit_loop = QuitLoopClosure()](
                           fidl::Result<fuchsia_examples::Echo::EchoString>& result) {
        ASSERT_TRUE(result.is_ok()) << "EchoString failed: " << result.error_value();
        reply_received = result->response();
        quit_loop();
      });
  RunLoop();

  EXPECT_EQ(reply_received, kTestString);
}

// Test that outgoing directory is able to serve multiple service members. In
// this case, the directory will host the `fuchsia.examples.EchoService` which
// contains two `fuchsia.examples.Echo` member. One regular, and one reversed.
TEST_F(OutgoingDirectoryTest, AddServiceServesAllMembers) {
  EchoImpl regular_impl(/*reversed=*/false);
  EchoImpl reversed_impl(/*reversed=*/true);
  ZX_ASSERT(GetOutgoingDirectory()
                ->AddService<fuchsia_examples::EchoService>(
                    std::move(fuchsia_examples::EchoService::InstanceHandler({
                        .regular_echo = regular_impl.bind_handler(dispatcher()),
                        .reversed_echo = reversed_impl.bind_handler(dispatcher()),
                    })))
                .is_ok());

  // Setup test client.
  auto open_result = component::OpenServiceAt<fuchsia_examples::EchoService>(
      TakeSvcClientEnd(TakeRootClientEnd()));
  ZX_ASSERT(open_result.is_ok());

  fuchsia_examples::EchoService::ServiceClient service = std::move(open_result.value());

  // Assert that service is connected and that proper impl returns expected reply.
  for (bool reversed : {true, false}) {
    bool message_echoed = false;
    auto client = ConnectToServiceMember(service, reversed);
    auto expected_reply = reversed ? kTestStringReversed : kTestString;
    client->EchoString(kTestString)
        .ThenExactlyOnce(
            [quit_loop = QuitLoopClosure(), &message_echoed, expected_reply = expected_reply](
                fidl::WireUnownedResult<fuchsia_examples::Echo::EchoString>& reply) {
              EXPECT_TRUE(reply.ok()) << "Reply failed with: " << reply.error().status_string();
              EXPECT_EQ(reply.value().response.get(), cpp17::string_view(expected_reply));
              message_echoed = true;
              quit_loop();
            });

    RunLoop();

    EXPECT_TRUE(message_echoed);
  }

  // Next, assert that after removing the service, the client end yields ZX_ERR_PEER_CLOSED.
  ZX_ASSERT(GetOutgoingDirectory()->RemoveService<fuchsia_examples::EchoService>().is_ok());
  for (bool reversed : {true, false}) {
    auto connect_result =
        reversed ? service.connect_reversed_echo() : service.connect_regular_echo();
    ZX_ASSERT(connect_result.is_error());
    EXPECT_EQ(connect_result.status_value(), ZX_ERR_PEER_CLOSED);
  }
}

// Test that serving a FIDL Protocol works as expected.
TEST_F(OutgoingDirectoryTest, AddProtocolCanServeMultipleProtocols) {
  constexpr static std::array<std::pair<bool, const char*>, 2> kIsReversedAndPaths = {
      {{false, "fuchsia.examples.Echo"}, {true, "fuchsia.examples.Ohce"}}};

  // Setup fuchsia.examples.Echo servers
  EchoImpl regular_impl(/*reversed=*/false);
  EchoImpl reversed_impl(/*reversed=*/true);
  for (auto [reversed, path] : kIsReversedAndPaths) {
    auto* impl = reversed ? &reversed_impl : &regular_impl;
    ASSERT_EQ(
        GetOutgoingDirectory()->AddProtocol<fuchsia_examples::Echo>(impl, path).status_value(),
        ZX_OK);
  }

  // Setup fuchsia.examples.Echo client
  for (auto [reversed, path] : kIsReversedAndPaths) {
    auto client_end =
        component::ConnectAt<fuchsia_examples::Echo>(TakeSvcClientEnd(TakeRootClientEnd()), path);
    ASSERT_EQ(client_end.status_value(), ZX_OK);
    fidl::WireClient<fuchsia_examples::Echo> client(std::move(*client_end), dispatcher());

    std::string reply_received;
    client->EchoString(kTestString)
        .ThenExactlyOnce([&reply_received, quit_loop = QuitLoopClosure()](
                             fidl::WireUnownedResult<fuchsia_examples::Echo::EchoString>& result) {
          ASSERT_TRUE(result.ok()) << "EchoString failed: " << result.error();
          auto* response = result.Unwrap();
          reply_received = std::string(response->response.data(), response->response.size());
          quit_loop();
        });
    RunLoop();

    auto expected_reply = reversed ? kTestStringReversed : kTestString;
    EXPECT_EQ(reply_received, expected_reply);
  }
}

// Test that after removing protocol, all clients are unable to make a call on
// the channel.
TEST_F(OutgoingDirectoryTest, RemoveProtocolClosesAllConnections) {
  // For this test case, 3 clients will connect to one Echo protocol.
  static constexpr size_t kNumClients = 3;

  class EventHandler : public fidl::AsyncEventHandler<fuchsia_examples::Echo> {
   public:
    EventHandler() = default;

    void on_fidl_error(fidl::UnbindInfo error) override { errors_.emplace_back(error); }

    std::vector<fidl::UnbindInfo> GetErrors() { return errors_; }

   private:
    std::vector<fidl::UnbindInfo> errors_;
  };

  EventHandler event_handler;
  EchoImpl regular_impl(/*reversed=*/false);
  ASSERT_EQ(
      GetOutgoingDirectory()->AddProtocol<fuchsia_examples::Echo>(&regular_impl).status_value(),
      ZX_OK);

  fidl::ClientEnd<fuchsia_io::Directory> svc_directory = TakeSvcClientEnd(TakeRootClientEnd());
  std::vector<fidl::Client<fuchsia_examples::Echo>> clients = {};
  for (size_t i = 0; i < kNumClients; ++i) {
    auto client_end = component::ConnectAt<fuchsia_examples::Echo>(svc_directory.borrow());

    ASSERT_EQ(client_end.status_value(), ZX_OK);

    fidl::Client<fuchsia_examples::Echo> client(std::move(*client_end), dispatcher(),
                                                &event_handler);
    client->EchoString(std::string(kTestString))
        .ThenExactlyOnce([quit_loop = QuitLoopClosure()](
                             fidl::Result<fuchsia_examples::Echo::EchoString>& result) {
          ASSERT_TRUE(result.is_ok());
          ASSERT_EQ(result->response(), kTestString);
          quit_loop();
        });
    RunLoop();

    clients.emplace_back(std::move(client));
  }

  ASSERT_EQ(GetOutgoingDirectory()->RemoveProtocol<fuchsia_examples::Echo>().status_value(), ZX_OK);
  RunLoopUntilIdle();

  ASSERT_EQ(event_handler.GetErrors().size(), kNumClients);
  for (auto& error : event_handler.GetErrors()) {
    EXPECT_TRUE(error.is_peer_closed())
        << "Expected peer_closed. Got : " << error.FormatDescription();
  }
}

// Test that serving a FIDL Protocol from a non-svc directory works as expected.
TEST_F(OutgoingDirectoryTest, AddProtocolAtServesProtocol) {
  constexpr static char kDirectory[] = "test";

  // Setup fuchsia.examples.Echo servers
  EchoImpl regular_impl(/*reversed=*/false);
  ASSERT_EQ(GetOutgoingDirectory()
                ->AddProtocolAt<fuchsia_examples::Echo>(kDirectory, &regular_impl)
                .status_value(),
            ZX_OK);

  auto client_end = component::ConnectAt<fuchsia_examples::Echo>(
      TakeSvcClientEnd(TakeRootClientEnd(), /*path=*/kDirectory));
  ASSERT_EQ(client_end.status_value(), ZX_OK);
  fidl::WireClient<fuchsia_examples::Echo> client(std::move(*client_end), dispatcher());

  std::string reply_received;
  client->EchoString(kTestString)
      .ThenExactlyOnce([&reply_received, quit_loop = QuitLoopClosure()](
                           fidl::WireUnownedResult<fuchsia_examples::Echo::EchoString>& result) {
        ASSERT_TRUE(result.ok()) << "EchoString failed: " << result.error().FormatDescription();
        auto* response = result.Unwrap();
        reply_received = std::string(response->response.data(), response->response.size());
        quit_loop();
      });
  RunLoop();

  EXPECT_EQ(reply_received, kTestString);
}

TEST_F(OutgoingDirectoryTest, AddDirectoryAtCanServeADirectory) {
  static constexpr char kTestPath[] = "root";
  static constexpr char kTestDirectory[] = "diagnostics";
  static constexpr char kTestFile[] = "sample.txt";
  static constexpr char kTestContent[] = "Hello World!";

  fs::ManagedVfs vfs(dispatcher());
  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  auto text_file = fbl::MakeRefCounted<fs::BufferedPseudoFile>(
      /*read_handler=*/[](fbl::String* output) -> zx_status_t {
        *output = kTestContent;
        return ZX_OK;
      });
  auto diagnostics = fbl::MakeRefCounted<fs::PseudoDir>();
  diagnostics->AddEntry(kTestFile, text_file);
  vfs.ServeDirectory(diagnostics, std::move(endpoints->server));
  ASSERT_EQ(GetOutgoingDirectory()
                ->AddDirectoryAt(std::move(endpoints->client), kTestPath, kTestDirectory)
                .status_value(),
            ZX_OK);

  std::thread([client_end = TakeRootClientEnd().TakeChannel().release(),
               quit_loop = QuitLoopClosure()]() {
    fbl::unique_fd root_fd;
    ASSERT_EQ(fdio_fd_create(client_end, root_fd.reset_and_get_address()), ZX_OK);
    ZX_ASSERT_MSG(root_fd.is_valid(), "Failed to open root ns as a file descriptor: %s",
                  strerror(errno));

    std::string path = std::string(kTestPath) + "/" + std::string(kTestDirectory);
    fbl::unique_fd dir_fd(openat(root_fd.get(), path.c_str(), O_DIRECTORY));
    ZX_ASSERT_MSG(dir_fd.is_valid(), "Failed to open directory \"%s\": %s", kTestDirectory,
                  strerror(errno));

    fbl::unique_fd filefd(openat(dir_fd.get(), kTestFile, O_RDONLY));
    ZX_ASSERT_MSG(filefd.is_valid(), "Failed to open file \"%s\": %s", kTestFile, strerror(errno));
    static constexpr size_t kMaxBufferSize = 1024;
    static char kReadBuffer[kMaxBufferSize];
    size_t bytes_read = read(filefd.get(), reinterpret_cast<void*>(kReadBuffer), kMaxBufferSize);
    ZX_ASSERT_MSG(bytes_read > 0, "Read 0 bytes from file at \"%s\": %s", kTestFile,
                  strerror(errno));

    std::string actual_content(kReadBuffer, bytes_read);
    EXPECT_EQ(actual_content, kTestContent);
    quit_loop();
  }).detach();

  RunLoop();

  vfs.Shutdown([quit_loop = QuitLoopClosure()](zx_status_t status) {
    ASSERT_EQ(status, ZX_OK);
    quit_loop();
  });
  RunLoop();

  EXPECT_EQ(GetOutgoingDirectory()->RemoveDirectory(kTestPath).status_value(), ZX_OK);
}

// Test that we can connect to the outgoing directory via multiple connections.
TEST_F(OutgoingDirectoryTest, ServeCanYieldMultipleConnections) {
  // Setup fuchsia.examples.Echo server
  EchoImpl regular_impl(/*reversed=*/false);
  ASSERT_EQ(
      GetOutgoingDirectory()->AddProtocol<fuchsia_examples::Echo>(&regular_impl).status_value(),
      ZX_OK);

  // Setup fuchsia.examples.Echo client
  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  // First |Serve| is invoked as part of test setup, so we'll assert that a
  // subsequent invocation is allowed.
  ASSERT_EQ(GetOutgoingDirectory()->Serve(std::move(endpoints->server)).status_value(), ZX_OK);

  std::vector<fidl::ClientEnd<fuchsia_io::Directory>> root_client_ends;
  // Take client end for channel used during invocation of |Serve| during setup.
  root_client_ends.emplace_back(TakeRootClientEnd());
  // Take client end for channel used during invocation of |Serve| in this function.
  root_client_ends.emplace_back(std::move(endpoints->client));

  while (!root_client_ends.empty()) {
    auto root = std::move(root_client_ends.back());
    root_client_ends.pop_back();

    auto client_end =
        component::ConnectAt<fuchsia_examples::Echo>(TakeSvcClientEnd(/*root=*/std::move(root)));
    ASSERT_EQ(client_end.status_value(), ZX_OK);
    fidl::WireClient<fuchsia_examples::Echo> client(std::move(*client_end), dispatcher());

    std::string reply_received;
    client->EchoString(kTestString)
        .ThenExactlyOnce([&reply_received, quit_loop = QuitLoopClosure()](
                             fidl::WireUnownedResult<fuchsia_examples::Echo::EchoString>& result) {
          ASSERT_TRUE(result.ok()) << "EchoString failed: " << result.error();
          auto* response = result.Unwrap();
          reply_received = std::string(response->response.data(), response->response.size());
          quit_loop();
        });
    RunLoop();

    EXPECT_EQ(reply_received, kTestString);
  }
}

TEST_F(OutgoingDirectoryTest, CreateFailsIfDispatcherIsNullptr) {
  ASSERT_DEATH(
      { auto outgoing_directory = component::OutgoingDirectory::Create(/*dispatcher=*/nullptr); },
      "");
}

TEST_F(OutgoingDirectoryTest, ServeFailsIfHandleInvalid) {
  auto outgoing_directory = component::OutgoingDirectory::Create(dispatcher());
  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  // Close server end in order to  invalidate channel.
  endpoints->server.reset();
  EXPECT_EQ(outgoing_directory.Serve(std::move(endpoints->server)).status_value(),
            ZX_ERR_BAD_HANDLE);
}

TEST_F(OutgoingDirectoryTest, AddServiceFailsIfInstanceNameIsEmpty) {
  EXPECT_EQ(GetOutgoingDirectory()
                ->AddService<fuchsia_examples::EchoService>(CreateNonEmptyServiceHandler(),
                                                            /*instance=*/"")
                .status_value(),
            ZX_ERR_INVALID_ARGS);
}

TEST_F(OutgoingDirectoryTest, AddServiceFailsIfEntryExists) {
  ASSERT_EQ(GetOutgoingDirectory()
                ->AddService<fuchsia_examples::EchoService>(CreateNonEmptyServiceHandler())
                .status_value(),
            ZX_OK);

  EXPECT_EQ(GetOutgoingDirectory()
                ->AddService<fuchsia_examples::EchoService>(CreateNonEmptyServiceHandler())
                .status_value(),
            ZX_ERR_ALREADY_EXISTS);
}

TEST_F(OutgoingDirectoryTest, AddServiceFailsIfServiceHandlerEmpty) {
  EXPECT_EQ(GetOutgoingDirectory()
                ->AddService<fuchsia_examples::EchoService>(component::ServiceInstanceHandler())
                .status_value(),
            ZX_ERR_INVALID_ARGS);
}

TEST_F(OutgoingDirectoryTest, AddServiceFailsIfServiceNameIsEmpty) {
  EXPECT_EQ(GetOutgoingDirectory()
                ->AddService(CreateNonEmptyServiceHandler(), /*service=*/"",
                             /*instance=*/component::kDefaultInstance)
                .status_value(),
            ZX_ERR_INVALID_ARGS);
}

TEST_F(OutgoingDirectoryTest, AddProtocolFailsIfImplIsNullptr) {
  EXPECT_EQ(GetOutgoingDirectory()
                ->AddProtocol<fuchsia_examples::Echo>(
                    /*impl*/ static_cast<fidl::WireServer<fuchsia_examples::Echo>*>(nullptr))
                .status_value(),
            ZX_ERR_INVALID_ARGS);
  EXPECT_EQ(GetOutgoingDirectory()
                ->AddProtocol<fuchsia_examples::Echo>(
                    /*impl*/ static_cast<fidl::Server<fuchsia_examples::Echo>*>(nullptr))
                .status_value(),
            ZX_ERR_INVALID_ARGS);
}

TEST_F(OutgoingDirectoryTest, AddProtocolFailsIfNameIsEmpty) {
  EchoImpl regular_impl(/*reversed=*/false);
  EXPECT_EQ(GetOutgoingDirectory()
                ->AddProtocol<fuchsia_examples::Echo>(&regular_impl, /*name=*/"")
                .status_value(),
            ZX_ERR_INVALID_ARGS);
}

TEST_F(OutgoingDirectoryTest, AddProtocolFailsIfEntryExists) {
  EchoImpl regular_impl(/*reversed=*/false);
  ASSERT_EQ(
      GetOutgoingDirectory()->AddProtocol<fuchsia_examples::Echo>(&regular_impl).status_value(),
      ZX_OK);

  EXPECT_EQ(
      GetOutgoingDirectory()->AddProtocol<fuchsia_examples::Echo>(&regular_impl).status_value(),
      ZX_ERR_ALREADY_EXISTS);
}

TEST_F(OutgoingDirectoryTest, AddProtocolAtFailsIfDirectoryIsEmpty) {
  EchoImpl regular_impl(/*reversed=*/false);
  EXPECT_EQ(GetOutgoingDirectory()
                ->AddProtocolAt<fuchsia_examples::Echo>(/*directory=*/"", &regular_impl)
                .status_value(),
            ZX_ERR_INVALID_ARGS);
}

TEST_F(OutgoingDirectoryTest, AddDirectoryFailsIfRemoteDirInvalid) {
  fidl::ClientEnd<fuchsia_io::Directory> dangling_client_end;
  ASSERT_FALSE(dangling_client_end.is_valid());

  EXPECT_EQ(GetOutgoingDirectory()
                ->AddDirectory(std::move(dangling_client_end), "AValidName")
                .status_value(),
            ZX_ERR_BAD_HANDLE);
}

TEST_F(OutgoingDirectoryTest, AddDirectoryFailsIfDirectoryNameIsEmpty) {
  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();

  EXPECT_EQ(GetOutgoingDirectory()
                ->AddDirectory(std::move(endpoints->client), /*directory_name=*/"")
                .status_value(),
            ZX_ERR_INVALID_ARGS);
}

TEST_F(OutgoingDirectoryTest, AddDirectoryFailsIfEntryExists) {
  constexpr char kDirectoryName[] = "test";

  for (auto expected_status : {ZX_OK, ZX_ERR_ALREADY_EXISTS}) {
    auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
    EXPECT_EQ(GetOutgoingDirectory()
                  ->AddDirectory(std::move(endpoints->client), kDirectoryName)
                  .status_value(),
              expected_status);
  }
}

TEST_F(OutgoingDirectoryTest, AddDirectoryFailsIfNameUsedForAddProtocolAt) {
  constexpr char kDirectoryName[] = "diagnostics";

  EchoImpl regular_impl(/*reversed=*/false);
  ASSERT_EQ(GetOutgoingDirectory()
                ->AddProtocolAt<fuchsia_examples::Echo>(kDirectoryName, &regular_impl)
                .status_value(),
            ZX_OK);

  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  EXPECT_EQ(GetOutgoingDirectory()
                ->AddDirectory(std::move(endpoints->client), kDirectoryName)
                .status_value(),
            ZX_ERR_ALREADY_EXISTS);
}

TEST_F(OutgoingDirectoryTest, RemoveServiceFailsIfEntryDoesNotExist) {
  EXPECT_EQ(GetOutgoingDirectory()->RemoveService<fuchsia_examples::EchoService>().status_value(),
            ZX_ERR_NOT_FOUND);
}

TEST_F(OutgoingDirectoryTest, RemoveProtocolFailsIfEntryDoesNotExist) {
  EXPECT_EQ(GetOutgoingDirectory()->RemoveProtocol<fuchsia_examples::Echo>().status_value(),
            ZX_ERR_NOT_FOUND);
}

TEST_F(OutgoingDirectoryTest, RemoveDirectoryFailsIfEntryDoesNotExist) {
  EXPECT_EQ(GetOutgoingDirectory()->RemoveDirectory(/*directory_name=*/"test").status_value(),
            ZX_ERR_NOT_FOUND);
}

class OutgoingDirectoryPathParameterizedFixture
    : public testing::TestWithParam<std::pair<std::string, std::string>> {};

TEST_P(OutgoingDirectoryPathParameterizedFixture, BadServicePaths) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto outgoing_directory = component::OutgoingDirectory::Create(loop.dispatcher());
  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  ZX_ASSERT(outgoing_directory.Serve(std::move(endpoints->server)).is_ok());
  component::ServiceInstanceHandler service_handler;
  fuchsia_examples::EchoService::Handler echo_service_handler(&service_handler);
  EchoImpl regular_impl(/*reversed=*/false);
  auto noop_handler = [](fidl::ServerEnd<fuchsia_examples::Echo> _request) -> void {};
  ZX_ASSERT(echo_service_handler.add_regular_echo(std::move(noop_handler)).is_ok());

  auto service_and_instance_names = GetParam();
  EXPECT_EQ(outgoing_directory
                .AddService(component::ServiceInstanceHandler(), service_and_instance_names.first,
                            service_and_instance_names.second)
                .status_value(),
            ZX_ERR_INVALID_ARGS);
}

INSTANTIATE_TEST_SUITE_P(OutgoingDirectoryTestPathTest, OutgoingDirectoryPathParameterizedFixture,
                         testing::Values(std::make_pair("", component::kDefaultInstance),
                                         std::make_pair(".", component::kDefaultInstance),
                                         std::make_pair("fuchsia.examples.EchoService", ""),
                                         std::make_pair("fuchsia.examples.EchoService", "")));

}  // namespace
