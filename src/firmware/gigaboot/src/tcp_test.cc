// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tcp.h"

#include <lib/efi/testing/mock_service_binding.h>
#include <lib/efi/testing/mock_tcp6.h>
#include <lib/efi/testing/stub_boot_services.h>

#include <array>

#include <efi/protocol/service-binding.h>
#include <efi/protocol/tcp6.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace {

using ::efi::MatchGuid;
using ::efi::MockBootServices;
using ::efi::MockServiceBindingProtocol;
using ::efi::MockTcp6Protocol;
using ::testing::_;
using ::testing::InSequence;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::Test;
using ::testing::Unused;

const efi_handle kTcpBindingHandle = reinterpret_cast<efi_handle>(0x10);
const efi_handle kTcpServerHandle = reinterpret_cast<efi_handle>(0x20);
const efi_handle kTcpClientHandle = reinterpret_cast<efi_handle>(0x30);
const efi_handle kTestEvent = reinterpret_cast<efi_event>(0x100);
const efi_ipv6_addr kTestAddress = {.addr = {0x01, 0x23, 0x45, 0x67}};
const uint16_t kTestPort = 12345;

// Test fixture to handle common setup/teardown.
//
// Configures mocks such that tcp6_*() functions will succeed by default.
// Tests can use EXPECT_CALL() to override default behavior if needed.
//
// Additionally tracks event create/close calls to make sure every created
// event is also closed.
class TcpTest : public Test {
 public:
  void SetUp() override {
    // For many functions, the default behavior of returning 0 (EFI_SUCCESS)
    // works without any explicit mocking.
    static_assert(EFI_SUCCESS == 0, "Fix default mocking");

    // It's important to create non-null events, since the TCP code checks
    // against null to determine if the event is pending or not.
    ON_CALL(mock_boot_services_, CreateEvent)
        .WillByDefault([this](Unused, Unused, Unused, Unused, efi_event* event) {
          open_events_++;
          *event = kTestEvent;
          return EFI_SUCCESS;
        });
    ON_CALL(mock_boot_services_, CloseEvent(kTestEvent)).WillByDefault([this](Unused) {
      EXPECT_GT(open_events_, 0);
      open_events_--;
      return EFI_SUCCESS;
    });

    // Opening the service binding handle and protocol.
    ON_CALL(
        mock_boot_services_,
        LocateHandleBuffer(ByProtocol, MatchGuid(EFI_TCP6_SERVICE_BINDING_PROTOCOL_GUID), _, _, _))
        .WillByDefault([this](Unused, Unused, Unused, size_t* num_handles, efi_handle** buf) {
          return AllocateHandleBuffer(num_handles, buf, {kTcpBindingHandle});
        });
    ON_CALL(mock_boot_services_,
            OpenProtocol(kTcpBindingHandle, MatchGuid(EFI_TCP6_SERVICE_BINDING_PROTOCOL_GUID), _, _,
                         _, _))
        .WillByDefault([this](Unused, Unused, void** intf, Unused, Unused, Unused) {
          *intf = mock_binding_protocol_.protocol();
          return EFI_SUCCESS;
        });

    // Opening the server handle and protocol.
    ON_CALL(mock_binding_protocol_, CreateChild).WillByDefault([](efi_handle* handle) {
      *handle = kTcpServerHandle;
      return EFI_SUCCESS;
    });
    ON_CALL(mock_boot_services_,
            OpenProtocol(kTcpServerHandle, MatchGuid(EFI_TCP6_PROTOCOL_GUID), _, _, _, _))
        .WillByDefault([this](Unused, Unused, void** intf, Unused, Unused, Unused) {
          *intf = mock_server_protocol_.protocol();
          return EFI_SUCCESS;
        });

    // Accepting a client.
    ON_CALL(mock_server_protocol_, Accept).WillByDefault([](efi_tcp6_listen_token* listen_token) {
      listen_token->NewChildHandle = kTcpClientHandle;
      return EFI_SUCCESS;
    });
    ON_CALL(mock_boot_services_,
            OpenProtocol(kTcpClientHandle, MatchGuid(EFI_TCP6_PROTOCOL_GUID), _, _, _, _))
        .WillByDefault([this](Unused, Unused, void** intf, Unused, Unused, Unused) {
          *intf = mock_client_protocol_.protocol();
          return EFI_SUCCESS;
        });

    // Read/Write/Disconnect/Close will work correctly using default behavior.
  }

  void TearDown() override { EXPECT_EQ(open_events_, 0); }

  // Adds expectations that the socket server and binding protocols are closed.
  //
  // This isn't necessary for proper functionality, it only adds checks that all
  // the members are closed out if a test wants to specifically look for that.
  void ExpectServerClose() {
    InSequence sequence;
    // Closing the server.
    EXPECT_CALL(mock_server_protocol_, Close);

    // Closing the server handle and protocol.
    EXPECT_CALL(mock_boot_services_,
                CloseProtocol(kTcpServerHandle, MatchGuid(EFI_TCP6_PROTOCOL_GUID), _, _));
    EXPECT_CALL(mock_binding_protocol_, DestroyChild(kTcpServerHandle));

    // Closing the service binding protocol.
    EXPECT_CALL(
        mock_boot_services_,
        CloseProtocol(kTcpBindingHandle, MatchGuid(EFI_TCP6_SERVICE_BINDING_PROTOCOL_GUID), _, _));
  }

  // Adds expectations that the socket client is disconnected.
  //
  // This isn't necessary for proper functionality, it only adds checks that all
  // the members are closed out if a test wants to specifically look for that.
  void ExpectDisconnect() {
    InSequence sequence;
    // Closing the client.
    EXPECT_CALL(mock_client_protocol_, Close);

    // Closing the client protocol. We don't need to close the client handle,
    // once the last protocol is closed EFI automatically frees the handle.
    EXPECT_CALL(mock_boot_services_,
                CloseProtocol(kTcpClientHandle, MatchGuid(EFI_TCP6_PROTOCOL_GUID), _, _));
  }

  // Allocates a handle buffer and sets it to the given contents.
  // Useful for mocking LocateHandleBuffer().
  efi_status AllocateHandleBuffer(size_t* num_handles, efi_handle** buf,
                                  std::vector<efi_handle> handles) {
    size_t handle_bytes = sizeof(handles[0]) * handles.size();
    if (efi_status status =
            mock_boot_services_.AllocatePool(EfiLoaderData, handle_bytes, (void**)buf);
        status != EFI_SUCCESS) {
      return status;
    }
    *num_handles = handles.size();
    if (handles.size() > 0) {
      memcpy(*buf, handles.data(), handle_bytes);
    }
    return EFI_SUCCESS;
  }

 protected:
  // NiceMock so that we can use ON_CALL/WillByDefault without it spamming
  // a bunch of "uninteresting call" messages.
  NiceMock<MockBootServices> mock_boot_services_;
  NiceMock<MockServiceBindingProtocol> mock_binding_protocol_;
  NiceMock<MockTcp6Protocol> mock_server_protocol_;
  NiceMock<MockTcp6Protocol> mock_client_protocol_;

  // Track the number of created events so we can always make sure we close
  // every event we create.
  int open_events_ = 0;
};

TEST_F(TcpTest, Open) {
  tcp6_socket socket = {};

  // Verify that we're passing the correct IP address/port.
  EXPECT_CALL(mock_server_protocol_, Configure).WillOnce([](efi_tcp6_config_data* config_data) {
    EXPECT_EQ(
        0, memcmp(&config_data->AccessPoint.StationAddress, &kTestAddress, sizeof(kTestAddress)));
    EXPECT_EQ(config_data->AccessPoint.StationPort, kTestPort);
    return EFI_SUCCESS;
  });

  EXPECT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_boot_services_.services(), &kTestAddress, kTestPort));
  EXPECT_EQ(socket.binding_handle, kTcpBindingHandle);
  EXPECT_EQ(socket.binding_protocol, mock_binding_protocol_.protocol());
  EXPECT_EQ(socket.server_handle, kTcpServerHandle);
  EXPECT_EQ(socket.server_protocol, mock_server_protocol_.protocol());
}

TEST_F(TcpTest, OpenMultipleBindingHandles) {
  tcp6_socket socket = {};

  // Currently if LocateHandleBuffer() gives multiple handles, we should just
  // default to using the first.
  EXPECT_CALL(
      mock_boot_services_,
      LocateHandleBuffer(ByProtocol, MatchGuid(EFI_TCP6_SERVICE_BINDING_PROTOCOL_GUID), _, _, _))
      .WillOnce([this](Unused, Unused, Unused, size_t* num_handles, efi_handle** buf) {
        return AllocateHandleBuffer(num_handles, buf, {kTcpBindingHandle, kTcpServerHandle});
      });

  EXPECT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_boot_services_.services(), &kTestAddress, kTestPort));

  EXPECT_EQ(socket.binding_handle, kTcpBindingHandle);
}

TEST_F(TcpTest, OpenFailLocateBindingHandleError) {
  tcp6_socket socket = {};

  EXPECT_CALL(
      mock_boot_services_,
      LocateHandleBuffer(ByProtocol, MatchGuid(EFI_TCP6_SERVICE_BINDING_PROTOCOL_GUID), _, _, _))
      .WillOnce(Return(EFI_NOT_FOUND));

  EXPECT_EQ(TCP6_RESULT_ERROR,
            tcp6_open(&socket, mock_boot_services_.services(), &kTestAddress, kTestPort));
}

TEST_F(TcpTest, OpenFailLocateBindingHandleZeroHandles) {
  tcp6_socket socket = {};

  EXPECT_CALL(
      mock_boot_services_,
      LocateHandleBuffer(ByProtocol, MatchGuid(EFI_TCP6_SERVICE_BINDING_PROTOCOL_GUID), _, _, _))
      .WillOnce([this](Unused, Unused, Unused, size_t* num_handles, efi_handle** buf) {
        return AllocateHandleBuffer(num_handles, buf, {});
      });

  EXPECT_EQ(TCP6_RESULT_ERROR,
            tcp6_open(&socket, mock_boot_services_.services(), &kTestAddress, kTestPort));
}

TEST_F(TcpTest, OpenFailOpenBindingProtocol) {
  tcp6_socket socket = {};

  EXPECT_CALL(mock_boot_services_, OpenProtocol(kTcpBindingHandle, _, _, _, _, _))
      .WillOnce(Return(EFI_UNSUPPORTED));
  // If we fail to open the binding protocol, we should not attempt to close it.
  EXPECT_CALL(mock_boot_services_, CloseProtocol(kTcpBindingHandle, _, _, _)).Times(0);

  EXPECT_EQ(TCP6_RESULT_ERROR,
            tcp6_open(&socket, mock_boot_services_.services(), &kTestAddress, kTestPort));
}

TEST_F(TcpTest, OpenFailCreateServerHandle) {
  tcp6_socket socket = {};

  EXPECT_CALL(mock_binding_protocol_, CreateChild).WillOnce(Return(EFI_OUT_OF_RESOURCES));
  // We successfully opened the binding protocol so we should close it, but
  // not try to destroy the child handle since it never got created.
  EXPECT_CALL(mock_boot_services_, CloseProtocol(kTcpBindingHandle, _, _, _));
  EXPECT_CALL(mock_binding_protocol_, DestroyChild).Times(0);

  EXPECT_EQ(TCP6_RESULT_ERROR,
            tcp6_open(&socket, mock_boot_services_.services(), &kTestAddress, kTestPort));
}

TEST_F(TcpTest, OpenFailOpenServerProtocol) {
  tcp6_socket socket = {};

  EXPECT_CALL(mock_boot_services_, OpenProtocol(kTcpBindingHandle, _, _, _, _, _));
  EXPECT_CALL(mock_boot_services_, OpenProtocol(kTcpServerHandle, _, _, _, _, _))
      .WillOnce(Return(EFI_UNSUPPORTED));
  // We should close the binding protocol and server handle, but not the server
  // protocol.
  EXPECT_CALL(mock_boot_services_, CloseProtocol(kTcpBindingHandle, _, _, _));
  EXPECT_CALL(mock_binding_protocol_, DestroyChild);
  EXPECT_CALL(mock_boot_services_, CloseProtocol(kTcpServerHandle, _, _, _)).Times(0);

  EXPECT_EQ(TCP6_RESULT_ERROR,
            tcp6_open(&socket, mock_boot_services_.services(), &kTestAddress, kTestPort));
}

TEST_F(TcpTest, OpenFailConfig) {
  tcp6_socket socket = {};

  EXPECT_CALL(mock_server_protocol_, Configure).WillOnce(Return(EFI_INVALID_PARAMETER));
  // We should close everything out.
  ExpectServerClose();

  EXPECT_EQ(TCP6_RESULT_ERROR,
            tcp6_open(&socket, mock_boot_services_.services(), &kTestAddress, kTestPort));
}

TEST_F(TcpTest, Accept) {
  tcp6_socket socket = {};

  ASSERT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_boot_services_.services(), &kTestAddress, kTestPort));
  // No client should be set until tcp6_accept().
  EXPECT_EQ(socket.client_handle, nullptr);
  EXPECT_EQ(socket.client_protocol, nullptr);

  EXPECT_EQ(TCP6_RESULT_SUCCESS, tcp6_accept(&socket));
  EXPECT_EQ(socket.client_handle, kTcpClientHandle);
  EXPECT_EQ(socket.client_protocol, mock_client_protocol_.protocol());
}

TEST_F(TcpTest, AcceptPending) {
  tcp6_socket socket = {};

  EXPECT_CALL(mock_boot_services_, CheckEvent(kTestEvent))
      .WillOnce(Return(EFI_NOT_READY))  // Accept() #1
      .WillOnce(Return(EFI_SUCCESS));   // Accept() #2

  ASSERT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_boot_services_.services(), &kTestAddress, kTestPort));

  EXPECT_EQ(TCP6_RESULT_PENDING, tcp6_accept(&socket));
  EXPECT_EQ(TCP6_RESULT_SUCCESS, tcp6_accept(&socket));
  EXPECT_EQ(socket.client_handle, kTcpClientHandle);
  EXPECT_EQ(socket.client_protocol, mock_client_protocol_.protocol());
}

TEST_F(TcpTest, AcceptFailCreateEvent) {
  tcp6_socket socket = {};

  EXPECT_CALL(mock_boot_services_, CreateEvent).WillOnce(Return(EFI_OUT_OF_RESOURCES));

  ASSERT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_boot_services_.services(), &kTestAddress, kTestPort));

  EXPECT_EQ(TCP6_RESULT_ERROR, tcp6_accept(&socket));
}

TEST_F(TcpTest, AcceptFailAccept) {
  tcp6_socket socket = {};

  EXPECT_CALL(mock_server_protocol_, Accept).WillOnce(Return(EFI_OUT_OF_RESOURCES));

  ASSERT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_boot_services_.services(), &kTestAddress, kTestPort));

  EXPECT_EQ(TCP6_RESULT_ERROR, tcp6_accept(&socket));
}

TEST_F(TcpTest, AcceptFailCheckEvent) {
  tcp6_socket socket = {};

  EXPECT_CALL(mock_boot_services_, CheckEvent).WillOnce(Return(EFI_OUT_OF_RESOURCES));

  ASSERT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_boot_services_.services(), &kTestAddress, kTestPort));

  EXPECT_EQ(TCP6_RESULT_ERROR, tcp6_accept(&socket));
}

TEST_F(TcpTest, AcceptFailStatusError) {
  tcp6_socket socket = {};

  // The accept event completes, but with an error status.
  EXPECT_CALL(mock_server_protocol_, Accept).WillOnce([](efi_tcp6_listen_token* listen_token) {
    listen_token->CompletionToken.Status = EFI_OUT_OF_RESOURCES;
    return EFI_SUCCESS;
  });

  ASSERT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_boot_services_.services(), &kTestAddress, kTestPort));

  EXPECT_EQ(TCP6_RESULT_ERROR, tcp6_accept(&socket));
}

TEST_F(TcpTest, AcceptFailOpenClientProtocol) {
  tcp6_socket socket = {};

  EXPECT_CALL(mock_boot_services_, OpenProtocol(kTcpBindingHandle, _, _, _, _, _));
  EXPECT_CALL(mock_boot_services_, OpenProtocol(kTcpServerHandle, _, _, _, _, _));
  EXPECT_CALL(mock_boot_services_, OpenProtocol(kTcpClientHandle, _, _, _, _, _))
      .WillOnce(Return(EFI_UNSUPPORTED));

  ASSERT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_boot_services_.services(), &kTestAddress, kTestPort));

  EXPECT_EQ(TCP6_RESULT_ERROR, tcp6_accept(&socket));
}

TEST_F(TcpTest, Read) {
  tcp6_socket socket = {};
  std::array<uint8_t, 8> data;

  // Make sure we pass the expected parameters through.
  EXPECT_CALL(mock_client_protocol_, Receive).WillOnce([&data](efi_tcp6_io_token* token) {
    EXPECT_FALSE(token->Packet.RxData->UrgentFlag);
    EXPECT_EQ(token->Packet.RxData->DataLength, data.size());
    EXPECT_EQ(token->Packet.RxData->FragmentCount, 1u);
    EXPECT_EQ(token->Packet.RxData->FragmentTable[0].FragmentLength, data.size());
    EXPECT_EQ(token->Packet.RxData->FragmentTable[0].FragmentBuffer, data.data());
    return EFI_SUCCESS;
  });

  // Make sure we call Poll() each time we read, for performance.
  EXPECT_CALL(mock_client_protocol_, Poll);

  ASSERT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_boot_services_.services(), &kTestAddress, kTestPort));
  ASSERT_EQ(TCP6_RESULT_SUCCESS, tcp6_accept(&socket));

  EXPECT_EQ(TCP6_RESULT_SUCCESS, tcp6_read(&socket, data.data(), data.size()));
}

TEST_F(TcpTest, ReadPending) {
  tcp6_socket socket = {};
  std::array<uint8_t, 8> data;

  // Read isn't ready the first time.
  EXPECT_CALL(mock_boot_services_, CheckEvent(kTestEvent))
      .WillOnce(Return(EFI_SUCCESS))    // Accept()
      .WillOnce(Return(EFI_NOT_READY))  // Receive() #1
      .WillOnce(Return(EFI_SUCCESS));   // Receive() #2

  ASSERT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_boot_services_.services(), &kTestAddress, kTestPort));
  ASSERT_EQ(TCP6_RESULT_SUCCESS, tcp6_accept(&socket));

  EXPECT_EQ(TCP6_RESULT_PENDING, tcp6_read(&socket, data.data(), data.size()));
  EXPECT_EQ(TCP6_RESULT_SUCCESS, tcp6_read(&socket, data.data(), data.size()));
}

TEST_F(TcpTest, ReadPartial) {
  tcp6_socket socket = {};
  std::array<uint8_t, 8> data;

  EXPECT_CALL(mock_client_protocol_, Receive)
      .WillOnce([&data](efi_tcp6_io_token* token) {
        token->Packet.RxData->DataLength = 6;
        token->Packet.RxData->FragmentTable[0].FragmentLength = 6;
        token->Packet.RxData->FragmentTable[0].FragmentBuffer = data.data();
        return EFI_SUCCESS;
      })
      .WillOnce([&data](efi_tcp6_io_token* token) {
        token->Packet.RxData->DataLength = 2;
        token->Packet.RxData->FragmentTable[0].FragmentLength = 2;
        token->Packet.RxData->FragmentTable[0].FragmentBuffer = data.data() + 6;
        return EFI_SUCCESS;
      });

  ASSERT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_boot_services_.services(), &kTestAddress, kTestPort));
  ASSERT_EQ(TCP6_RESULT_SUCCESS, tcp6_accept(&socket));

  // When we see a partial read we try again immediately, so we should only
  // have to call tcp6_read() once, but this is an implementation detail so
  // may change in the future.
  EXPECT_EQ(TCP6_RESULT_SUCCESS, tcp6_read(&socket, data.data(), data.size()));
}

TEST_F(TcpTest, ReadFailCreateEvent) {
  tcp6_socket socket = {};
  std::array<uint8_t, 8> data;

  EXPECT_CALL(mock_boot_services_, CreateEvent)
      .WillOnce(Return(EFI_SUCCESS))            // Accept()
      .WillOnce(Return(EFI_OUT_OF_RESOURCES));  // Receive()

  ASSERT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_boot_services_.services(), &kTestAddress, kTestPort));
  ASSERT_EQ(TCP6_RESULT_SUCCESS, tcp6_accept(&socket));

  EXPECT_EQ(TCP6_RESULT_ERROR, tcp6_read(&socket, data.data(), data.size()));
}

TEST_F(TcpTest, ReadFailReceive) {
  tcp6_socket socket = {};
  std::array<uint8_t, 8> data;

  EXPECT_CALL(mock_client_protocol_, Receive).WillOnce(Return(EFI_OUT_OF_RESOURCES));

  ASSERT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_boot_services_.services(), &kTestAddress, kTestPort));
  ASSERT_EQ(TCP6_RESULT_SUCCESS, tcp6_accept(&socket));

  EXPECT_EQ(TCP6_RESULT_ERROR, tcp6_read(&socket, data.data(), data.size()));
}

TEST_F(TcpTest, ReadFailCheckEvent) {
  tcp6_socket socket = {};
  std::array<uint8_t, 8> data;

  EXPECT_CALL(mock_boot_services_, CheckEvent)
      .WillOnce(Return(EFI_SUCCESS))            // Accept()
      .WillOnce(Return(EFI_OUT_OF_RESOURCES));  // Receive()

  ASSERT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_boot_services_.services(), &kTestAddress, kTestPort));
  ASSERT_EQ(TCP6_RESULT_SUCCESS, tcp6_accept(&socket));

  EXPECT_EQ(TCP6_RESULT_ERROR, tcp6_read(&socket, data.data(), data.size()));
}

TEST_F(TcpTest, ReadFailCompletionError) {
  tcp6_socket socket = {};
  std::array<uint8_t, 8> data;

  EXPECT_CALL(mock_client_protocol_, Receive).WillOnce([](efi_tcp6_io_token* token) {
    token->CompletionToken.Status = EFI_OUT_OF_RESOURCES;
    return EFI_SUCCESS;
  });

  ASSERT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_boot_services_.services(), &kTestAddress, kTestPort));
  ASSERT_EQ(TCP6_RESULT_SUCCESS, tcp6_accept(&socket));

  EXPECT_EQ(TCP6_RESULT_ERROR, tcp6_read(&socket, data.data(), data.size()));
}

TEST_F(TcpTest, ReadFailDisconnectFin) {
  tcp6_socket socket = {};
  std::array<uint8_t, 8> data;

  EXPECT_CALL(mock_client_protocol_, Receive).WillOnce([](efi_tcp6_io_token* token) {
    token->CompletionToken.Status = EFI_CONNECTION_FIN;
    return EFI_SUCCESS;
  });

  ASSERT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_boot_services_.services(), &kTestAddress, kTestPort));
  ASSERT_EQ(TCP6_RESULT_SUCCESS, tcp6_accept(&socket));

  EXPECT_EQ(TCP6_RESULT_DISCONNECTED, tcp6_read(&socket, data.data(), data.size()));
}

TEST_F(TcpTest, ReadFailDisconnectReset) {
  tcp6_socket socket = {};
  std::array<uint8_t, 8> data;

  EXPECT_CALL(mock_client_protocol_, Receive).WillOnce([](efi_tcp6_io_token* token) {
    token->CompletionToken.Status = EFI_CONNECTION_RESET;
    return EFI_SUCCESS;
  });

  ASSERT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_boot_services_.services(), &kTestAddress, kTestPort));
  ASSERT_EQ(TCP6_RESULT_SUCCESS, tcp6_accept(&socket));

  EXPECT_EQ(TCP6_RESULT_DISCONNECTED, tcp6_read(&socket, data.data(), data.size()));
}

TEST_F(TcpTest, ReadFailOverflow) {
  tcp6_socket socket = {};
  std::array<uint8_t, 8> data;

  // Receive() returns success, but gives us more data than expected.
  EXPECT_CALL(mock_client_protocol_, Receive).WillOnce([](efi_tcp6_io_token* token) {
    token->Packet.RxData->DataLength = 10;
    token->Packet.RxData->FragmentTable[0].FragmentLength = 10;
    return EFI_SUCCESS;
  });

  ASSERT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_boot_services_.services(), &kTestAddress, kTestPort));
  ASSERT_EQ(TCP6_RESULT_SUCCESS, tcp6_accept(&socket));

  EXPECT_EQ(TCP6_RESULT_ERROR, tcp6_read(&socket, data.data(), data.size()));
}

TEST_F(TcpTest, Write) {
  tcp6_socket socket = {};
  std::array<uint8_t, 8> data;

  // Make sure we pass the expected parameters through.
  EXPECT_CALL(mock_client_protocol_, Transmit).WillOnce([&data](efi_tcp6_io_token* token) {
    EXPECT_TRUE(token->Packet.TxData->Push);
    EXPECT_FALSE(token->Packet.TxData->Urgent);
    EXPECT_EQ(token->Packet.TxData->DataLength, data.size());
    EXPECT_EQ(token->Packet.TxData->FragmentCount, 1u);
    EXPECT_EQ(token->Packet.TxData->FragmentTable[0].FragmentLength, data.size());
    EXPECT_EQ(token->Packet.TxData->FragmentTable[0].FragmentBuffer, data.data());
    return EFI_SUCCESS;
  });

  // Make sure we call Poll() each time we read, for performance.
  EXPECT_CALL(mock_client_protocol_, Poll);

  ASSERT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_boot_services_.services(), &kTestAddress, kTestPort));
  ASSERT_EQ(TCP6_RESULT_SUCCESS, tcp6_accept(&socket));

  EXPECT_EQ(TCP6_RESULT_SUCCESS, tcp6_write(&socket, data.data(), data.size()));
}

TEST_F(TcpTest, WriteFailCreateEvent) {
  tcp6_socket socket = {};
  std::array<uint8_t, 8> data;

  EXPECT_CALL(mock_boot_services_, CreateEvent)
      .WillOnce(Return(EFI_SUCCESS))            // Accept()
      .WillOnce(Return(EFI_OUT_OF_RESOURCES));  // Transmit()

  ASSERT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_boot_services_.services(), &kTestAddress, kTestPort));
  ASSERT_EQ(TCP6_RESULT_SUCCESS, tcp6_accept(&socket));

  EXPECT_EQ(TCP6_RESULT_ERROR, tcp6_write(&socket, data.data(), data.size()));
}

TEST_F(TcpTest, WriteFailTransmit) {
  tcp6_socket socket = {};
  std::array<uint8_t, 8> data;

  EXPECT_CALL(mock_client_protocol_, Transmit).WillOnce(Return(EFI_OUT_OF_RESOURCES));

  ASSERT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_boot_services_.services(), &kTestAddress, kTestPort));
  ASSERT_EQ(TCP6_RESULT_SUCCESS, tcp6_accept(&socket));

  EXPECT_EQ(TCP6_RESULT_ERROR, tcp6_write(&socket, data.data(), data.size()));
}

TEST_F(TcpTest, WriteFailCheckEvent) {
  tcp6_socket socket = {};
  std::array<uint8_t, 8> data;

  EXPECT_CALL(mock_boot_services_, CheckEvent)
      .WillOnce(Return(EFI_SUCCESS))            // Accept()
      .WillOnce(Return(EFI_OUT_OF_RESOURCES));  // Transmit()

  ASSERT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_boot_services_.services(), &kTestAddress, kTestPort));
  ASSERT_EQ(TCP6_RESULT_SUCCESS, tcp6_accept(&socket));

  EXPECT_EQ(TCP6_RESULT_ERROR, tcp6_write(&socket, data.data(), data.size()));
}

TEST_F(TcpTest, WriteFailCompletionError) {
  tcp6_socket socket = {};
  std::array<uint8_t, 8> data;

  EXPECT_CALL(mock_client_protocol_, Transmit).WillOnce([](efi_tcp6_io_token* token) {
    token->CompletionToken.Status = EFI_OUT_OF_RESOURCES;
    return EFI_SUCCESS;
  });

  ASSERT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_boot_services_.services(), &kTestAddress, kTestPort));
  ASSERT_EQ(TCP6_RESULT_SUCCESS, tcp6_accept(&socket));

  EXPECT_EQ(TCP6_RESULT_ERROR, tcp6_write(&socket, data.data(), data.size()));
}

TEST_F(TcpTest, WriteFailDisconnectFin) {
  tcp6_socket socket = {};
  std::array<uint8_t, 8> data;

  EXPECT_CALL(mock_client_protocol_, Transmit).WillOnce([](efi_tcp6_io_token* token) {
    token->CompletionToken.Status = EFI_CONNECTION_FIN;
    return EFI_SUCCESS;
  });

  ASSERT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_boot_services_.services(), &kTestAddress, kTestPort));
  ASSERT_EQ(TCP6_RESULT_SUCCESS, tcp6_accept(&socket));

  EXPECT_EQ(TCP6_RESULT_DISCONNECTED, tcp6_write(&socket, data.data(), data.size()));
}

TEST_F(TcpTest, WriteFailDisconnectReset) {
  tcp6_socket socket = {};
  std::array<uint8_t, 8> data;

  EXPECT_CALL(mock_client_protocol_, Transmit).WillOnce([](efi_tcp6_io_token* token) {
    token->CompletionToken.Status = EFI_CONNECTION_RESET;
    return EFI_SUCCESS;
  });

  ASSERT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_boot_services_.services(), &kTestAddress, kTestPort));
  ASSERT_EQ(TCP6_RESULT_SUCCESS, tcp6_accept(&socket));

  EXPECT_EQ(TCP6_RESULT_DISCONNECTED, tcp6_write(&socket, data.data(), data.size()));
}

TEST_F(TcpTest, WriteFailPartial) {
  tcp6_socket socket = {};
  std::array<uint8_t, 8> data;

  // Transmit() returns success, but gives us less data than expected.
  EXPECT_CALL(mock_client_protocol_, Transmit).WillOnce([](efi_tcp6_io_token* token) {
    token->Packet.TxData->DataLength = 4;
    token->Packet.TxData->FragmentTable[0].FragmentLength = 4;
    return EFI_SUCCESS;
  });

  ASSERT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_boot_services_.services(), &kTestAddress, kTestPort));
  ASSERT_EQ(TCP6_RESULT_SUCCESS, tcp6_accept(&socket));

  EXPECT_EQ(TCP6_RESULT_ERROR, tcp6_write(&socket, data.data(), data.size()));
}

TEST_F(TcpTest, Disconnect) {
  tcp6_socket socket = {};

  ExpectDisconnect();

  ASSERT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_boot_services_.services(), &kTestAddress, kTestPort));
  ASSERT_EQ(TCP6_RESULT_SUCCESS, tcp6_accept(&socket));

  EXPECT_EQ(TCP6_RESULT_SUCCESS, tcp6_disconnect(&socket));
}

TEST_F(TcpTest, DisconnectTwice) {
  tcp6_socket socket = {};

  // We should only try to disconnect once, the second should be a no-op.
  ExpectDisconnect();

  ASSERT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_boot_services_.services(), &kTestAddress, kTestPort));
  ASSERT_EQ(TCP6_RESULT_SUCCESS, tcp6_accept(&socket));

  EXPECT_EQ(TCP6_RESULT_SUCCESS, tcp6_disconnect(&socket));
  EXPECT_EQ(TCP6_RESULT_SUCCESS, tcp6_disconnect(&socket));
}

TEST_F(TcpTest, DisconnectPending) {
  tcp6_socket socket = {};

  // Accept is ready the first time, but disconnect isn't.
  EXPECT_CALL(mock_boot_services_, CheckEvent(kTestEvent))
      .WillOnce(Return(EFI_SUCCESS))    // Accept()
      .WillOnce(Return(EFI_NOT_READY))  // Close() #1
      .WillOnce(Return(EFI_SUCCESS));   // Close() #2
  ExpectDisconnect();

  ASSERT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_boot_services_.services(), &kTestAddress, kTestPort));
  ASSERT_EQ(TCP6_RESULT_SUCCESS, tcp6_accept(&socket));

  EXPECT_EQ(TCP6_RESULT_PENDING, tcp6_disconnect(&socket));
  EXPECT_EQ(TCP6_RESULT_SUCCESS, tcp6_disconnect(&socket));
}

TEST_F(TcpTest, Close) {
  tcp6_socket socket = {};

  ExpectServerClose();

  EXPECT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_boot_services_.services(), &kTestAddress, kTestPort));
  EXPECT_EQ(TCP6_RESULT_SUCCESS, tcp6_close(&socket));

  EXPECT_EQ(socket.binding_protocol, nullptr);
  EXPECT_EQ(socket.server_protocol, nullptr);
  EXPECT_EQ(socket.client_protocol, nullptr);
}

TEST_F(TcpTest, CloseWithClient) {
  tcp6_socket socket = {};

  InSequence sequence;
  ExpectDisconnect();
  ExpectServerClose();

  EXPECT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_boot_services_.services(), &kTestAddress, kTestPort));
  EXPECT_EQ(TCP6_RESULT_SUCCESS, tcp6_accept(&socket));
  EXPECT_EQ(TCP6_RESULT_SUCCESS, tcp6_close(&socket));
}

TEST_F(TcpTest, CloseTwice) {
  tcp6_socket socket = {};

  // All these functions should still only be called once, closing the socket
  // a second time should be a no-op.
  ExpectServerClose();

  EXPECT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_boot_services_.services(), &kTestAddress, kTestPort));
  EXPECT_EQ(TCP6_RESULT_SUCCESS, tcp6_close(&socket));
  EXPECT_EQ(TCP6_RESULT_SUCCESS, tcp6_close(&socket));
}

TEST_F(TcpTest, ClosePending) {
  tcp6_socket socket = {};

  // Have the close event not be ready on the first check.
  EXPECT_CALL(mock_boot_services_, CheckEvent(kTestEvent))
      .WillOnce(Return(EFI_NOT_READY))  // Close() #1
      .WillOnce(Return(EFI_SUCCESS));   // Close() #2

  // All the members should still be closed exactly once each.
  ExpectServerClose();

  EXPECT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_boot_services_.services(), &kTestAddress, kTestPort));
  EXPECT_EQ(TCP6_RESULT_PENDING, tcp6_close(&socket));
  EXPECT_EQ(TCP6_RESULT_SUCCESS, tcp6_close(&socket));
}

}  // namespace
