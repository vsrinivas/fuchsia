// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/driver2/runtime.h>
#include <lib/driver2/runtime_connector_impl.h>
#include <lib/sys/component/cpp/outgoing_directory.h>
#include <lib/sys/component/cpp/service_client.h>

#include <set>

#include <gtest/gtest.h>

#include "src/lib/testing/loop_fixture/real_loop_fixture.h"

namespace fdf {
using namespace fuchsia_driver_framework;
}  // namespace fdf

namespace fio = fuchsia_io;

class RuntimeConnectorTest : public gtest::RealLoopFixture {
 public:
  void SetUp() override {
    RealLoopFixture::SetUp();

    runtime_connector_ = std::make_unique<driver::RuntimeConnectorImpl>(dispatcher());

    // Setup the outgoing directory.
    outgoing_ = std::make_unique<component::OutgoingDirectory>(
        component::OutgoingDirectory::Create(dispatcher()));

    auto service = [this](fidl::ServerEnd<fdf::RuntimeConnector> server_end) {
      fidl::BindServer(dispatcher(), std::move(server_end), runtime_connector_.get());
    };
    ASSERT_EQ(ZX_OK,
              outgoing_->AddProtocol<fdf::RuntimeConnector>(std::move(service)).status_value());

    auto endpoints = fidl::CreateEndpoints<fio::Directory>();
    ASSERT_EQ(ZX_OK, endpoints.status_value());

    ASSERT_EQ(ZX_OK, outgoing_->Serve(std::move(endpoints->server)).status_value());
    root_dir_ = fidl::WireClient<fuchsia_io::Directory>(std::move(endpoints->client), dispatcher());
  }

  zx::result<fidl::WireSharedClient<fdf::RuntimeConnector>> CreateRuntimeConnectorClient() {
    zx::channel server_end, client_end;
    auto status = zx::channel::create(0, &server_end, &client_end);
    if (status != ZX_OK) {
      return zx::error(status);
    }
    auto open = root_dir_->Open(
        fuchsia_io::wire::OpenFlags::kRightWritable | fuchsia_io::wire::OpenFlags::kRightReadable,
        fuchsia_io::wire::kModeTypeDirectory, "svc",
        fidl::ServerEnd<fuchsia_io::Node>(std::move(server_end)));
    if (!open.ok()) {
      return zx::error(ZX_ERR_IO);
    }
    fidl::ClientEnd<fuchsia_io::Directory> fidl_client_end(std::move(client_end));
    auto svc = component::ConnectAt<fdf::RuntimeConnector>(std::move(fidl_client_end));
    if (!svc.is_ok()) {
      return svc.take_error();
    }
    return zx::ok(fidl::WireSharedClient<fdf::RuntimeConnector>(std::move(*svc), dispatcher()));
  }

  void TearDown() override { RealLoopFixture::TearDown(); }

 protected:
  std::unique_ptr<driver::RuntimeConnectorImpl> runtime_connector_;
  std::unique_ptr<component::OutgoingDirectory> outgoing_;
  fidl::WireClient<fuchsia_io::Directory> root_dir_;
};

TEST_F(RuntimeConnectorTest, ConnectSuccess) {
  static constexpr const char* kProtocol = "test";

  auto runtime_connector_client = CreateRuntimeConnectorClient();
  ASSERT_EQ(ZX_OK, runtime_connector_client.status_value());

  auto channels = fdf::ChannelPair::Create(0);
  ASSERT_FALSE(channels.is_error());

  std::atomic_bool server_called = false;
  runtime_connector_->RegisterProtocol(kProtocol, [&](fdf::Channel channel) -> zx_status_t {
    server_called = true;
    return ZX_OK;
  });

  async::Executor executor(dispatcher());
  auto task = driver::ConnectToRuntimeProtocol<void>(*runtime_connector_client, kProtocol)
                  .then([quit_loop = QuitLoopClosure()](
                            fpromise::result<fdf::Channel, zx_status_t>& result) {
                    ASSERT_TRUE(result.is_ok());
                    quit_loop();
                  });
  executor.schedule_task(std::move(task));

  RunLoop();
  ASSERT_TRUE(server_called);
}

TEST_F(RuntimeConnectorTest, ConnectFailRejectedByServer) {
  static constexpr const char* kProtocol = "test";

  auto runtime_connector_client = CreateRuntimeConnectorClient();
  ASSERT_EQ(ZX_OK, runtime_connector_client.status_value());

  auto channels = fdf::ChannelPair::Create(0);
  ASSERT_FALSE(channels.is_error());

  std::atomic_bool server_called = false;
  runtime_connector_->RegisterProtocol(kProtocol, [&](fdf::Channel channel) -> zx_status_t {
    server_called = true;
    return ZX_ERR_INTERNAL;
  });

  async::Executor executor(dispatcher());
  auto task = driver::ConnectToRuntimeProtocol<void>(*runtime_connector_client, kProtocol)
                  .then([quit_loop = QuitLoopClosure()](
                            fpromise::result<fdf::Channel, zx_status_t>& result) {
                    ASSERT_FALSE(result.is_ok());
                    quit_loop();
                  });
  executor.schedule_task(std::move(task));

  RunLoop();
  ASSERT_TRUE(server_called);
}

TEST_F(RuntimeConnectorTest, ConnectFailNoMatchingProtocol) {
  static constexpr const char* kProtocol = "test";
  static constexpr const char* kNotSupportedProtocol = "not_supported";

  auto runtime_connector_client = CreateRuntimeConnectorClient();
  ASSERT_EQ(ZX_OK, runtime_connector_client.status_value());

  auto channels = fdf::ChannelPair::Create(0);
  ASSERT_FALSE(channels.is_error());

  std::atomic_bool server_called = false;
  runtime_connector_->RegisterProtocol(kProtocol, [&](fdf::Channel channel) -> zx_status_t {
    // This should not be called.
    ZX_ASSERT(false);
    return ZX_ERR_INTERNAL;
  });

  async::Executor executor(dispatcher());
  auto task =
      driver::ConnectToRuntimeProtocol<void>(*runtime_connector_client, kNotSupportedProtocol)
          .then(
              [quit_loop = QuitLoopClosure()](fpromise::result<fdf::Channel, zx_status_t>& result) {
                ASSERT_FALSE(result.is_ok());
                quit_loop();
              });
  executor.schedule_task(std::move(task));

  RunLoop();
  ASSERT_FALSE(server_called);
}

TEST_F(RuntimeConnectorTest, ConnectMultiple) {
  static constexpr const char* kProtocolA = "test1";
  static constexpr const char* kProtocolB = "test2";

  auto runtime_connector_client = CreateRuntimeConnectorClient();
  ASSERT_EQ(ZX_OK, runtime_connector_client.status_value());

  // We will pass each end1 to Connect and verify that each callback gets the correct handle.
  auto channelsA = fdf::ChannelPair::Create(0);
  ASSERT_FALSE(channelsA.is_error());
  auto channelsB = fdf::ChannelPair::Create(0);
  ASSERT_FALSE(channelsB.is_error());

  std::atomic_bool channelA_received = false;
  runtime_connector_->RegisterProtocol(
      kProtocolA, [&, want_handle = channelsA->end1.get()](fdf::Channel channel) -> zx_status_t {
        ZX_ASSERT(channel.get() == want_handle);
        channelA_received = true;
        return ZX_OK;
      });

  std::atomic_bool channelB_received = false;
  runtime_connector_->RegisterProtocol(
      kProtocolB, [&, want_handle = channelsB->end1.get()](fdf::Channel channel) -> zx_status_t {
        ZX_ASSERT(channel.get() == want_handle);
        channelB_received = true;
        return ZX_OK;
      });

  // First try to connect to kProtocolB.
  // We don't use |driver::ConnectToRuntimeProtocol| as we want to verify the transferred
  // channel in the server callback.
  runtime_connector_client
      ->Connect(fidl::StringView::FromExternal(kProtocolB),
                fdf::wire::RuntimeProtocolServerEnd{channelsB->end1.release()})
      .ThenExactlyOnce(
          [quit_loop = QuitLoopClosure()](
              fidl::WireUnownedResult<fdf::RuntimeConnector::Connect>& result) mutable {
            ASSERT_TRUE(result.ok());
            ASSERT_FALSE(result->is_error());
            quit_loop();
          });

  RunLoop();
  ASSERT_TRUE(channelB_received);
  ASSERT_FALSE(channelA_received);
  channelB_received = false;

  // Now try to connect to kProtocolA.
  runtime_connector_client
      ->Connect(fidl::StringView::FromExternal(kProtocolA),
                fdf::wire::RuntimeProtocolServerEnd{channelsA->end1.release()})
      .ThenExactlyOnce(
          [quit_loop = QuitLoopClosure()](
              fidl::WireUnownedResult<fdf::RuntimeConnector::Connect>& result) mutable {
            ASSERT_TRUE(result.ok());
            ASSERT_FALSE(result->is_error());
            quit_loop();
          });

  RunLoop();
  ASSERT_TRUE(channelA_received);
  ASSERT_FALSE(channelB_received);
}

TEST_F(RuntimeConnectorTest, RegisterSameProtocol) {
  static constexpr const char* kProtocol = "test";

  auto runtime_connector_client = CreateRuntimeConnectorClient();
  ASSERT_EQ(ZX_OK, runtime_connector_client.status_value());

  auto channels = fdf::ChannelPair::Create(0);
  ASSERT_FALSE(channels.is_error());

  runtime_connector_->RegisterProtocol(kProtocol, [&](fdf::Channel channel) -> zx_status_t {
    // This should not be called.
    ZX_ASSERT(false);
    return ZX_ERR_INTERNAL;
  });

  std::atomic_bool channel_received = false;
  runtime_connector_->RegisterProtocol(
      kProtocol, [&, want_handle = channels->end1.get()](fdf::Channel channel) -> zx_status_t {
        ZX_ASSERT(channel.get() == want_handle);
        channel_received = true;
        return ZX_OK;
      });

  runtime_connector_client
      ->Connect(fidl::StringView::FromExternal(kProtocol),
                fdf::wire::RuntimeProtocolServerEnd{channels->end1.release()})
      .ThenExactlyOnce(
          [quit_loop = QuitLoopClosure()](
              fidl::WireUnownedResult<fdf::RuntimeConnector::Connect>& result) mutable {
            ASSERT_TRUE(result.ok());
            ASSERT_FALSE(result->is_error());
            quit_loop();
          });

  RunLoop();
  ASSERT_TRUE(channel_received);
}

TEST_F(RuntimeConnectorTest, ListProtocolsNone) {
  auto runtime_connector_client = CreateRuntimeConnectorClient();
  ASSERT_EQ(ZX_OK, runtime_connector_client.status_value());

  auto endpoints = fidl::CreateEndpoints<fdf::RuntimeProtocolIterator>();
  ASSERT_FALSE(endpoints.is_error());

  auto status = runtime_connector_client->ListProtocols(std::move(endpoints->server));
  ASSERT_TRUE(status.ok());

  auto iterator = fidl::WireSharedClient<fdf::RuntimeProtocolIterator>(std::move(endpoints->client),
                                                                       dispatcher());
  iterator->GetNext().ThenExactlyOnce(
      [quit_loop = QuitLoopClosure()](
          fidl::WireUnownedResult<fdf::RuntimeProtocolIterator::GetNext>& result) mutable {
        ASSERT_TRUE(result.ok());
        ASSERT_EQ(result->protocols.count(), 0lu);
        quit_loop();
      });

  RunLoop();
}

TEST_F(RuntimeConnectorTest, ListProtocols) {
  // Register a bunch of fake protocols.
  std::set<std::string> protocols;
  for (int i = 0; i < 30; i++) {
    auto protocol = std::to_string(i);
    protocols.insert(protocol);
    runtime_connector_->RegisterProtocol(protocol,
                                         [](fdf::Channel channel) -> zx_status_t { return ZX_OK; });
  }

  auto runtime_connector_client = CreateRuntimeConnectorClient();
  ASSERT_EQ(ZX_OK, runtime_connector_client.status_value());

  auto endpoints = fidl::CreateEndpoints<fdf::RuntimeProtocolIterator>();
  ASSERT_FALSE(endpoints.is_error());

  auto status = runtime_connector_client->ListProtocols(std::move(endpoints->server));
  ASSERT_TRUE(status.ok());

  auto iterator = fidl::WireSharedClient<fdf::RuntimeProtocolIterator>(std::move(endpoints->client),
                                                                       dispatcher());
  std::set<std::string> got_protocols;
  std::atomic_bool done = false;
  do {
    iterator->GetNext().ThenExactlyOnce(
        [&, quit_loop = QuitLoopClosure()](
            fidl::WireUnownedResult<fdf::RuntimeProtocolIterator::GetNext>& result) mutable {
          ASSERT_TRUE(result.ok());
          size_t count = result->protocols.count();
          for (size_t i = 0; i < count; i++) {
            auto protocol =
                std::string(result->protocols.at(i).data(), result->protocols.at(i).size());
            ASSERT_EQ(got_protocols.find(protocol), got_protocols.end());
            got_protocols.insert(protocol);
          }
          if (count == 0) {
            done = true;
          }
          quit_loop();
        });
    RunLoop();
  } while (!done);

  ASSERT_EQ(got_protocols.size(), 30lu);
  ASSERT_EQ(protocols, got_protocols);
}
