// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdf/cpp/dispatcher.h>
#include <lib/fdf/cpp/protocol.h>
#include <lib/fit/defer.h>
#include <lib/sync/cpp/completion.h>
#include <lib/zx/eventpair.h>

#include <zxtest/zxtest.h>

#include "src/devices/bin/driver_runtime/dispatcher.h"
#include "src/devices/bin/driver_runtime/driver_context.h"
#include "src/devices/bin/driver_runtime/runtime_test_case.h"

class TokenTest : public RuntimeTestCase {
 public:
  void SetUp() override;
  void TearDown() override;

 protected:
  fdf::Dispatcher dispatcher_local_;
  libsync::Completion dispatcher_local_shutdown_completion_;

  fdf::Dispatcher dispatcher_remote_;
  libsync::Completion dispatcher_remote_shutdown_completion_;

  fdf::Arena arena_{nullptr};

  zx::channel token_local_, token_remote_;
};

void TokenTest::SetUp() {
  {
    driver_context::PushDriver(CreateFakeDriver());
    auto pop_driver = fit::defer([]() { driver_context::PopDriver(); });

    auto dispatcher = fdf::Dispatcher::Create(0, "local", [&](fdf_dispatcher_t* dispatcher) {
      dispatcher_local_shutdown_completion_.Signal();
    });
    ASSERT_FALSE(dispatcher.is_error());

    dispatcher_local_ = std::move(*dispatcher);
  }

  {
    driver_context::PushDriver(CreateFakeDriver());
    auto pop_driver = fit::defer([]() { driver_context::PopDriver(); });

    auto dispatcher = fdf::Dispatcher::Create(0, "remote", [&](fdf_dispatcher_t* dispatcher) {
      dispatcher_remote_shutdown_completion_.Signal();
    });
    ASSERT_FALSE(dispatcher.is_error());

    dispatcher_remote_ = std::move(*dispatcher);
  }

  arena_ = fdf::Arena('TEST');

  ASSERT_OK(zx::channel::create(0, &token_local_, &token_remote_));
}

void TokenTest::TearDown() {
  dispatcher_remote_.ShutdownAsync();
  ASSERT_OK(dispatcher_remote_shutdown_completion_.Wait());

  dispatcher_local_.ShutdownAsync();
  ASSERT_OK(dispatcher_local_shutdown_completion_.Wait());
}

class ProtocolTest : public TokenTest {
 public:
  void SetUp() override;

 protected:
  // Checks that the peer of |channel| has closed by reading from it.
  void VerifyPeerClosed(fdf::Channel& channel, fdf::Dispatcher& dispatcher);

  fdf::Channel fdf_local_, fdf_remote_;
};

void ProtocolTest::SetUp() {
  TokenTest::SetUp();

  auto fdf_channels = fdf::ChannelPair::Create(0);
  ASSERT_OK(fdf_channels.status_value());
  fdf_local_ = std::move(fdf_channels->end0);
  fdf_remote_ = std::move(fdf_channels->end1);
}

void ProtocolTest::VerifyPeerClosed(fdf::Channel& channel, fdf::Dispatcher& dispatcher) {
  libsync::Completion read_completion;
  auto channel_read = std::make_unique<fdf::ChannelRead>(
      channel.get(), 0,
      [&read_completion](fdf_dispatcher_t* dispatcher, fdf::ChannelRead* channel_read,
                         zx_status_t status) {
        ASSERT_EQ(ZX_ERR_PEER_CLOSED, status);
        read_completion.Signal();
      });
  // Registering a channel read may fail if the peer is closed quickly enough.
  zx_status_t status = channel_read->Begin(dispatcher.get());
  ASSERT_TRUE((status == ZX_OK) || (status == ZX_ERR_PEER_CLOSED));
  if (status == ZX_OK) {
    ASSERT_OK(read_completion.Wait());
  }
}

// Tests registering a protocol before a client connect request is received.
TEST_F(ProtocolTest, RegisterThenConnect) {
  libsync::Completion callback_received;
  auto handler = [&](fdf_dispatcher_t* dispatcher, fdf::Protocol* protocol, zx_status_t status,
                     fdf::Channel channel) {
    ASSERT_EQ(dispatcher_remote_.get(), dispatcher);
    ASSERT_OK(status);
    callback_received.Signal();
  };
  fdf::Protocol protocol(handler);
  ASSERT_OK(protocol.Register(std::move(token_remote_), dispatcher_remote_.get()));

  ASSERT_OK(fdf::ProtocolConnect(std::move(token_local_), std::move(fdf_remote_)));
  ASSERT_OK(callback_received.Wait());
}

// Tests receiving a client connect request before the corresponding protocol
// has been registered.
TEST_F(ProtocolTest, ConnectThenRegister) {
  ASSERT_OK(fdf::ProtocolConnect(std::move(token_local_), std::move(fdf_remote_)));

  libsync::Completion callback_received;
  auto handler = [&](fdf_dispatcher_t* dispatcher, fdf::Protocol* protocol, zx_status_t status,
                     fdf::Channel channel) {
    ASSERT_EQ(dispatcher_remote_.get(), dispatcher);
    ASSERT_OK(status);
    callback_received.Signal();
  };
  fdf::Protocol protocol(handler);
  ASSERT_OK(protocol.Register(std::move(token_remote_), dispatcher_remote_.get()));
  ASSERT_OK(callback_received.Wait());
}

struct Conn {
  zx::channel token_local;
  zx::channel token_remote;
  fdf::Channel fdf_local;
  fdf::Channel fdf_remote;

  // We will transfer |fdf_remote|, so save the handle value here to compare it when received.
  fdf_handle_t fdf_remote_handle_value;
};

// Tests requesting many protocol connections before the protocols are registered.
TEST_F(ProtocolTest, MultiplePendingConnections) {
  constexpr uint32_t kNumConns = 1024;

  Conn conns[kNumConns];

  for (uint32_t i = 0; i < kNumConns; i++) {
    ASSERT_OK(zx::channel::create(0, &conns[i].token_local, &conns[i].token_remote));
    auto fdf_channels = fdf::ChannelPair::Create(0);
    ASSERT_OK(fdf_channels.status_value());
    conns[i].fdf_local = std::move(fdf_channels->end0);
    conns[i].fdf_remote = std::move(fdf_channels->end1);
    conns[i].fdf_remote_handle_value = conns[i].fdf_remote.get();

    ASSERT_OK(
        fdf::ProtocolConnect(std::move(conns[i].token_local), std::move(conns[i].fdf_remote)));
  }

  for (uint32_t i = 0; i < kNumConns; i++) {
    libsync::Completion callback_received;
    auto handler = [&](fdf_dispatcher_t* dispatcher, fdf::Protocol* protocol, zx_status_t status,
                       fdf::Channel channel) {
      ASSERT_EQ(dispatcher_remote_.get(), dispatcher);
      ASSERT_OK(status);
      ASSERT_EQ(conns[i].fdf_remote_handle_value, channel.get());
      callback_received.Signal();
    };
    fdf::Protocol protocol(handler);
    ASSERT_OK(protocol.Register(std::move(conns[i].token_remote), dispatcher_remote_.get()));
    ASSERT_OK(callback_received.Wait());
  }
}

// Tests requesting many protocol connections, before the protocols are registered
// in a non-sequential order.
TEST_F(ProtocolTest, MultiplePendingConnectionsDifferentOrder) {
  constexpr uint32_t kNumConns = 1024;
  // We will complete every 5th connection, then every 5th connection starting at
  // index 1 etc.
  constexpr uint32_t kSkipSize = 5;

  Conn conns[kNumConns];

  for (uint32_t i = 0; i < kNumConns; i++) {
    ASSERT_OK(zx::channel::create(0, &conns[i].token_local, &conns[i].token_remote));
    auto fdf_channels = fdf::ChannelPair::Create(0);
    ASSERT_OK(fdf_channels.status_value());
    conns[i].fdf_local = std::move(fdf_channels->end0);
    conns[i].fdf_remote = std::move(fdf_channels->end1);
    conns[i].fdf_remote_handle_value = conns[i].fdf_remote.get();

    ASSERT_OK(
        fdf::ProtocolConnect(std::move(conns[i].token_local), std::move(conns[i].fdf_remote)));
  }

  uint32_t num_conns = 0;
  for (uint32_t i = 0; i < kSkipSize; i++) {
    for (uint32_t j = i; j < kNumConns; j += kSkipSize) {
      libsync::Completion callback_received;
      auto handler = [&](fdf_dispatcher_t* dispatcher, fdf::Protocol* protocol, zx_status_t status,
                         fdf::Channel channel) {
        ASSERT_EQ(dispatcher_remote_.get(), dispatcher);
        ASSERT_OK(status);
        ASSERT_EQ(conns[j].fdf_remote_handle_value, channel.get());
        callback_received.Signal();
      };
      fdf::Protocol protocol(handler);
      ASSERT_OK(protocol.Register(std::move(conns[j].token_remote), dispatcher_remote_.get()));
      ASSERT_OK(callback_received.Wait());
      num_conns++;
    }
  }
  ASSERT_EQ(kNumConns, num_conns);
}

// Tests registering a protocol to a dispatcher that has already started shutting down.
TEST_F(ProtocolTest, RegisterAfterDispatcherShutdown) {
  dispatcher_remote_.ShutdownAsync();
  ASSERT_OK(dispatcher_remote_shutdown_completion_.Wait());

  auto handler = [&](fdf_dispatcher_t* dispatcher, fdf::Protocol* protocol, zx_status_t status,
                     fdf::Channel channel) { ASSERT_FALSE(true); };
  fdf::Protocol protocol(handler);
  ASSERT_EQ(ZX_ERR_BAD_STATE,
            protocol.Register(std::move(token_remote_), dispatcher_remote_.get()));
}

// Tests shutting down a dispatcher after a protocol has been registered,
// but before the connection callback has happened.
TEST_F(ProtocolTest, DispatcherShutdown) {
  libsync::Completion callback_received;
  auto handler = [&](fdf_dispatcher_t* dispatcher, fdf::Protocol* protocol, zx_status_t status,
                     fdf::Channel channel) {
    ASSERT_EQ(dispatcher_remote_.get(), dispatcher);
    ASSERT_EQ(ZX_ERR_CANCELED, status);
    callback_received.Signal();
  };
  fdf::Protocol protocol(handler);
  ASSERT_OK(protocol.Register(std::move(token_remote_), dispatcher_remote_.get()));

  dispatcher_remote_.ShutdownAsync();
  ASSERT_OK(dispatcher_remote_shutdown_completion_.Wait());

  ASSERT_OK(callback_received.Wait());

  // Try connecting to the protocol. The user will not receive an error until they try to
  // communicate over the fdf channel.
  ASSERT_OK(fdf::ProtocolConnect(std::move(token_local_), std::move(fdf_remote_)));

  VerifyPeerClosed(fdf_local_, dispatcher_local_);
}

// Tests shutting down a dispatcher at the same time the peer token is being closed.
TEST_F(ProtocolTest, DispatcherShutdownAndPeerClosed) {
  libsync::Completion callback_received;
  auto handler = [&](fdf_dispatcher_t* dispatcher, fdf::Protocol* protocol, zx_status_t status,
                     fdf::Channel channel) {
    ASSERT_EQ(dispatcher_remote_.get(), dispatcher);
    ASSERT_EQ(ZX_ERR_CANCELED, status);
    ASSERT_FALSE(callback_received.signaled());
    callback_received.Signal();
  };
  fdf::Protocol protocol(handler);
  ASSERT_OK(protocol.Register(std::move(token_remote_), dispatcher_remote_.get()));

  // Shutdown the dispatcher at the same time as closing the token channel peer.
  token_local_.reset();
  dispatcher_remote_.ShutdownAsync();

  ASSERT_OK(dispatcher_remote_shutdown_completion_.Wait());

  ASSERT_OK(callback_received.Wait());
}

// Tests registering a protocol, and the other driver dropping their token handle
// without connecting.
TEST_F(ProtocolTest, RegisterThenPeerClosed) {
  libsync::Completion callback_received;
  auto handler = [&](fdf_dispatcher_t* dispatcher, fdf::Protocol* protocol, zx_status_t status,
                     fdf::Channel channel) {
    ASSERT_EQ(dispatcher_remote_.get(), dispatcher);
    ASSERT_EQ(ZX_ERR_CANCELED, status);
    callback_received.Signal();
  };
  fdf::Protocol protocol(handler);
  ASSERT_OK(protocol.Register(std::move(token_remote_), dispatcher_remote_.get()));

  token_local_.reset();

  // Connect callback should get canceled status.
  ASSERT_OK(callback_received.Wait());
}

// Tests the token peer closing, then the protocol being registered.
TEST_F(ProtocolTest, PeerClosedThenRegister) {
  token_local_.reset();

  libsync::Completion callback_received;
  auto handler = [&](fdf_dispatcher_t* dispatcher, fdf::Protocol* protocol, zx_status_t status,
                     fdf::Channel channel) {
    ASSERT_EQ(dispatcher_remote_.get(), dispatcher);
    ASSERT_EQ(ZX_ERR_CANCELED, status);
    callback_received.Signal();
  };
  fdf::Protocol protocol(handler);
  ASSERT_OK(protocol.Register(std::move(token_remote_), dispatcher_remote_.get()));

  // Connect callback should get canceled status.
  ASSERT_OK(callback_received.Wait());
}

// Tests requesting a protocol connection, and the token peer is dropped
// before the protocol is registered.
TEST_F(ProtocolTest, ConnectThenPeerClosed) {
  ASSERT_OK(fdf::ProtocolConnect(std::move(token_local_), std::move(fdf_remote_)));
  token_remote_.reset();
  VerifyPeerClosed(fdf_local_, dispatcher_local_);
}

// Tests the token peer closing, then the protocol connection being requested.
TEST_F(ProtocolTest, PeerClosedThenConnect) {
  token_remote_.reset();
  ASSERT_OK(fdf::ProtocolConnect(std::move(token_local_), std::move(fdf_remote_)));
  VerifyPeerClosed(fdf_local_, dispatcher_local_);
}

//
// API Errors
//

TEST_F(ProtocolTest, ConnectWrongTokenType) {
  zx::eventpair bad_token_local, bad_token_remote;
  ASSERT_OK(zx::eventpair::create(0, &bad_token_local, &bad_token_remote));

  ASSERT_EQ(ZX_ERR_BAD_HANDLE, fdf_token_transfer(bad_token_local.release(), fdf_local_.release()));
}

void NotCalledHandler(fdf_dispatcher_t* dispatcher, fdf_token_t* protocol, zx_status_t status,
                      fdf_handle_t channel) {
  ASSERT_TRUE(false);
}

TEST_F(ProtocolTest, RegisterWrongTokenType) {
  zx::eventpair bad_token_local, bad_token_remote;
  ASSERT_OK(zx::eventpair::create(0, &bad_token_local, &bad_token_remote));

  fdf_token_t protocol{NotCalledHandler};
  ASSERT_EQ(ZX_ERR_BAD_HANDLE,
            fdf_token_register(bad_token_remote.release(), dispatcher_remote_.get(), &protocol));
}

TEST_F(ProtocolTest, ConnectBadFdfHandle) {
  fdf::Channel invalid_;
  ASSERT_EQ(ZX_ERR_BAD_HANDLE, fdf::ProtocolConnect(std::move(token_local_), std::move(invalid_)));
}

TEST_F(ProtocolTest, RegisterNoDispatcher) {
  auto handler = [&](fdf_dispatcher_t* dispatcher, fdf::Protocol* protocol, zx_status_t status,
                     fdf::Channel channel) { ASSERT_FALSE(true); };
  fdf::Protocol protocol(handler);
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, protocol.Register(std::move(token_remote_), nullptr));
}

struct ProtocolHandler : fdf_token_t {
  ProtocolHandler() : fdf_token_t{&Handler} {}

  static void Handler(fdf_dispatcher_t* dispatcher, fdf_token_t* protocol, zx_status_t status,
                      fdf_handle_t channel) {
    ASSERT_OK(status);

    ProtocolHandler* self = static_cast<ProtocolHandler*>(protocol);
    self->completion.Signal();
  }

  libsync::Completion completion;
};

// Tests that registering the same protocol handler will fail.
TEST_F(ProtocolTest, RegisterSameProtocolHandlerTwice) {
  ProtocolHandler protocol_handler;

  ASSERT_OK(
      fdf_token_register(token_remote_.release(), dispatcher_remote_.get(), &protocol_handler));

  // Try registering the same token handler again.
  zx::channel token_local2, token_remote2;
  ASSERT_OK(zx::channel::create(0, &token_local2, &token_remote2));
  ASSERT_EQ(ZX_ERR_BAD_STATE, fdf_token_register(token_remote2.release(), dispatcher_remote_.get(),
                                                 &protocol_handler));

  ASSERT_OK(fdf::ProtocolConnect(std::move(token_local_), std::move(fdf_remote_)));
  ASSERT_OK(protocol_handler.completion.Wait());
}
