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

#include "tcp_test.h"

using ::efi::MatchGuid;
using ::testing::_;
using ::testing::InSequence;
using ::testing::IsEmpty;
using ::testing::Return;
using ::testing::Unused;

const efi_handle MockTcp::kTcpBindingHandle = reinterpret_cast<efi_handle>(0x10);
const efi_handle MockTcp::kTcpServerHandle = reinterpret_cast<efi_handle>(0x20);
const efi_handle MockTcp::kTcpClientHandle = reinterpret_cast<efi_handle>(0x30);

// Events are used heavily in the TCP API, give each one a unique value so that
// we can more easily track events across multiple calls.
efi_handle NewTestEvent() {
  static uintptr_t start_value = 0x100;
  return reinterpret_cast<efi_event>(start_value++);
}

MockTcp::MockTcp() {
  // For many functions, the default behavior of returning 0 (EFI_SUCCESS)
  // works without any explicit mocking.
  static_assert(EFI_SUCCESS == 0, "Fix default mocking");

  // It's important to create non-null events, since the TCP code checks
  // against null to determine if the event is pending or not.
  ON_CALL(mock_boot_services_, CreateEvent)
      .WillByDefault([this](Unused, Unused, Unused, Unused, efi_event* event) {
        *event = NewTestEvent();
        created_events_.insert(*event);
        return EFI_SUCCESS;
      });
  ON_CALL(mock_boot_services_, CloseEvent).WillByDefault([this](efi_event event) {
    EXPECT_EQ(created_events_.erase(event), 1u);
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

MockTcp::~MockTcp() { EXPECT_THAT(created_events_, IsEmpty()); }

// Adds expectations that the socket server and binding protocols are closed.
//
// This isn't necessary for proper functionality, it only adds checks that all
// the members are closed out if a test wants to specifically look for that.
void MockTcp::ExpectServerClose() {
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
void MockTcp::ExpectDisconnect() {
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
efi_status MockTcp::AllocateHandleBuffer(size_t* num_handles, efi_handle** buf,
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

namespace {

const efi_ipv6_addr kTestAddress = {.addr = {0x01, 0x23, 0x45, 0x67}};
const uint16_t kTestPort = 12345;

TEST(TcpTest, Open) {
  MockTcp mock_tcp;
  tcp6_socket socket = {};

  // Verify that we're passing the correct IP address/port.
  EXPECT_CALL(mock_tcp.server_protocol(), Configure)
      .WillOnce([](efi_tcp6_config_data* config_data) {
        EXPECT_EQ(0, memcmp(&config_data->AccessPoint.StationAddress, &kTestAddress,
                            sizeof(kTestAddress)));
        EXPECT_EQ(config_data->AccessPoint.StationPort, kTestPort);
        return EFI_SUCCESS;
      });

  EXPECT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_tcp.boot_services().services(), &kTestAddress, kTestPort));
  EXPECT_EQ(socket.binding_handle, MockTcp::kTcpBindingHandle);
  EXPECT_EQ(socket.binding_protocol, mock_tcp.binding_protocol().protocol());
  EXPECT_EQ(socket.server_handle, MockTcp::kTcpServerHandle);
  EXPECT_EQ(socket.server_protocol, mock_tcp.server_protocol().protocol());
}

TEST(TcpTest, OpenMultipleBindingHandles) {
  MockTcp mock_tcp;
  tcp6_socket socket = {};

  // Currently if LocateHandleBuffer() gives multiple handles, we should just
  // default to using the first.
  EXPECT_CALL(
      mock_tcp.boot_services(),
      LocateHandleBuffer(ByProtocol, MatchGuid(EFI_TCP6_SERVICE_BINDING_PROTOCOL_GUID), _, _, _))
      .WillOnce([&mock_tcp](Unused, Unused, Unused, size_t* num_handles, efi_handle** buf) {
        return mock_tcp.AllocateHandleBuffer(
            num_handles, buf, {MockTcp::kTcpBindingHandle, MockTcp::kTcpServerHandle});
      });

  EXPECT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_tcp.boot_services().services(), &kTestAddress, kTestPort));

  EXPECT_EQ(socket.binding_handle, MockTcp::kTcpBindingHandle);
}

TEST(TcpTest, OpenFailLocateBindingHandleError) {
  MockTcp mock_tcp;
  tcp6_socket socket = {};

  EXPECT_CALL(
      mock_tcp.boot_services(),
      LocateHandleBuffer(ByProtocol, MatchGuid(EFI_TCP6_SERVICE_BINDING_PROTOCOL_GUID), _, _, _))
      .WillOnce(Return(EFI_NOT_FOUND));

  EXPECT_EQ(TCP6_RESULT_ERROR,
            tcp6_open(&socket, mock_tcp.boot_services().services(), &kTestAddress, kTestPort));
}

TEST(TcpTest, OpenFailLocateBindingHandleZeroHandles) {
  MockTcp mock_tcp;
  tcp6_socket socket = {};

  EXPECT_CALL(
      mock_tcp.boot_services(),
      LocateHandleBuffer(ByProtocol, MatchGuid(EFI_TCP6_SERVICE_BINDING_PROTOCOL_GUID), _, _, _))
      .WillOnce([&mock_tcp](Unused, Unused, Unused, size_t* num_handles, efi_handle** buf) {
        return mock_tcp.AllocateHandleBuffer(num_handles, buf, {});
      });

  EXPECT_EQ(TCP6_RESULT_ERROR,
            tcp6_open(&socket, mock_tcp.boot_services().services(), &kTestAddress, kTestPort));
}

TEST(TcpTest, OpenFailOpenBindingProtocol) {
  MockTcp mock_tcp;
  tcp6_socket socket = {};

  EXPECT_CALL(mock_tcp.boot_services(), OpenProtocol(MockTcp::kTcpBindingHandle, _, _, _, _, _))
      .WillOnce(Return(EFI_UNSUPPORTED));
  // If we fail to open the binding protocol, we should not attempt to close it.
  EXPECT_CALL(mock_tcp.boot_services(), CloseProtocol(MockTcp::kTcpBindingHandle, _, _, _))
      .Times(0);

  EXPECT_EQ(TCP6_RESULT_ERROR,
            tcp6_open(&socket, mock_tcp.boot_services().services(), &kTestAddress, kTestPort));
}

TEST(TcpTest, OpenFailCreateServerHandle) {
  MockTcp mock_tcp;
  tcp6_socket socket = {};

  EXPECT_CALL(mock_tcp.binding_protocol(), CreateChild).WillOnce(Return(EFI_OUT_OF_RESOURCES));
  // We successfully opened the binding protocol so we should close it, but
  // not try to destroy the child handle since it never got created.
  EXPECT_CALL(mock_tcp.boot_services(), CloseProtocol(MockTcp::kTcpBindingHandle, _, _, _));
  EXPECT_CALL(mock_tcp.binding_protocol(), DestroyChild).Times(0);

  EXPECT_EQ(TCP6_RESULT_ERROR,
            tcp6_open(&socket, mock_tcp.boot_services().services(), &kTestAddress, kTestPort));
}

TEST(TcpTest, OpenFailOpenServerProtocol) {
  MockTcp mock_tcp;
  tcp6_socket socket = {};

  EXPECT_CALL(mock_tcp.boot_services(), OpenProtocol(MockTcp::kTcpBindingHandle, _, _, _, _, _));
  EXPECT_CALL(mock_tcp.boot_services(), OpenProtocol(MockTcp::kTcpServerHandle, _, _, _, _, _))
      .WillOnce(Return(EFI_UNSUPPORTED));
  // We should close the binding protocol and server handle, but not the server
  // protocol.
  EXPECT_CALL(mock_tcp.boot_services(), CloseProtocol(MockTcp::kTcpBindingHandle, _, _, _));
  EXPECT_CALL(mock_tcp.binding_protocol(), DestroyChild);
  EXPECT_CALL(mock_tcp.boot_services(), CloseProtocol(MockTcp::kTcpServerHandle, _, _, _)).Times(0);

  EXPECT_EQ(TCP6_RESULT_ERROR,
            tcp6_open(&socket, mock_tcp.boot_services().services(), &kTestAddress, kTestPort));
}

TEST(TcpTest, OpenFailConfig) {
  MockTcp mock_tcp;
  tcp6_socket socket = {};

  EXPECT_CALL(mock_tcp.server_protocol(), Configure).WillOnce(Return(EFI_INVALID_PARAMETER));
  // We should close everything out.
  mock_tcp.ExpectServerClose();

  EXPECT_EQ(TCP6_RESULT_ERROR,
            tcp6_open(&socket, mock_tcp.boot_services().services(), &kTestAddress, kTestPort));
}

TEST(TcpTest, Accept) {
  MockTcp mock_tcp;
  tcp6_socket socket = {};

  ASSERT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_tcp.boot_services().services(), &kTestAddress, kTestPort));
  // No client should be set until tcp6_accept().
  EXPECT_EQ(socket.client_handle, nullptr);
  EXPECT_EQ(socket.client_protocol, nullptr);

  EXPECT_EQ(TCP6_RESULT_SUCCESS, tcp6_accept(&socket));
  EXPECT_EQ(socket.client_handle, MockTcp::kTcpClientHandle);
  EXPECT_EQ(socket.client_protocol, mock_tcp.client_protocol().protocol());
}

TEST(TcpTest, AcceptPending) {
  MockTcp mock_tcp;
  tcp6_socket socket = {};

  EXPECT_CALL(mock_tcp.boot_services(), CheckEvent)
      .WillOnce(Return(EFI_NOT_READY))  // Accept() #1
      .WillOnce(Return(EFI_SUCCESS));   // Accept() #2

  ASSERT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_tcp.boot_services().services(), &kTestAddress, kTestPort));

  EXPECT_EQ(TCP6_RESULT_PENDING, tcp6_accept(&socket));
  EXPECT_EQ(TCP6_RESULT_SUCCESS, tcp6_accept(&socket));
  EXPECT_EQ(socket.client_handle, MockTcp::kTcpClientHandle);
  EXPECT_EQ(socket.client_protocol, mock_tcp.client_protocol().protocol());
}

TEST(TcpTest, AcceptFailCreateEvent) {
  MockTcp mock_tcp;
  tcp6_socket socket = {};

  EXPECT_CALL(mock_tcp.boot_services(), CreateEvent).WillOnce(Return(EFI_OUT_OF_RESOURCES));

  ASSERT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_tcp.boot_services().services(), &kTestAddress, kTestPort));

  EXPECT_EQ(TCP6_RESULT_ERROR, tcp6_accept(&socket));
}

TEST(TcpTest, AcceptFailAccept) {
  MockTcp mock_tcp;
  tcp6_socket socket = {};

  EXPECT_CALL(mock_tcp.server_protocol(), Accept).WillOnce(Return(EFI_OUT_OF_RESOURCES));

  ASSERT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_tcp.boot_services().services(), &kTestAddress, kTestPort));

  EXPECT_EQ(TCP6_RESULT_ERROR, tcp6_accept(&socket));
}

TEST(TcpTest, AcceptFailCheckEvent) {
  MockTcp mock_tcp;
  tcp6_socket socket = {};

  EXPECT_CALL(mock_tcp.boot_services(), CheckEvent).WillOnce(Return(EFI_OUT_OF_RESOURCES));

  ASSERT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_tcp.boot_services().services(), &kTestAddress, kTestPort));

  EXPECT_EQ(TCP6_RESULT_ERROR, tcp6_accept(&socket));
}

TEST(TcpTest, AcceptFailStatusError) {
  MockTcp mock_tcp;
  tcp6_socket socket = {};

  // The accept event completes, but with an error status.
  EXPECT_CALL(mock_tcp.server_protocol(), Accept).WillOnce([](efi_tcp6_listen_token* listen_token) {
    listen_token->CompletionToken.Status = EFI_OUT_OF_RESOURCES;
    return EFI_SUCCESS;
  });

  ASSERT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_tcp.boot_services().services(), &kTestAddress, kTestPort));

  EXPECT_EQ(TCP6_RESULT_ERROR, tcp6_accept(&socket));
}

TEST(TcpTest, AcceptFailOpenClientProtocol) {
  MockTcp mock_tcp;
  tcp6_socket socket = {};

  EXPECT_CALL(mock_tcp.boot_services(), OpenProtocol(MockTcp::kTcpBindingHandle, _, _, _, _, _));
  EXPECT_CALL(mock_tcp.boot_services(), OpenProtocol(MockTcp::kTcpServerHandle, _, _, _, _, _));
  EXPECT_CALL(mock_tcp.boot_services(), OpenProtocol(MockTcp::kTcpClientHandle, _, _, _, _, _))
      .WillOnce(Return(EFI_UNSUPPORTED));

  ASSERT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_tcp.boot_services().services(), &kTestAddress, kTestPort));

  EXPECT_EQ(TCP6_RESULT_ERROR, tcp6_accept(&socket));
}

TEST(TcpTest, Read) {
  MockTcp mock_tcp;
  tcp6_socket socket = {};
  std::array<uint8_t, 8> data;

  // Make sure we pass the expected parameters through.
  EXPECT_CALL(mock_tcp.client_protocol(), Receive).WillOnce([&data](efi_tcp6_io_token* token) {
    EXPECT_FALSE(token->Packet.RxData->UrgentFlag);
    EXPECT_EQ(token->Packet.RxData->DataLength, data.size());
    EXPECT_EQ(token->Packet.RxData->FragmentCount, 1u);
    EXPECT_EQ(token->Packet.RxData->FragmentTable[0].FragmentLength, data.size());
    EXPECT_EQ(token->Packet.RxData->FragmentTable[0].FragmentBuffer, data.data());
    return EFI_SUCCESS;
  });

  // Make sure we call Poll() each time we read, for performance.
  EXPECT_CALL(mock_tcp.client_protocol(), Poll);

  ASSERT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_tcp.boot_services().services(), &kTestAddress, kTestPort));
  ASSERT_EQ(TCP6_RESULT_SUCCESS, tcp6_accept(&socket));

  EXPECT_EQ(TCP6_RESULT_SUCCESS, tcp6_read(&socket, data.data(), data.size()));
}

TEST(TcpTest, ReadPending) {
  MockTcp mock_tcp;
  tcp6_socket socket = {};
  std::array<uint8_t, 8> data;

  // Read isn't ready the first time.
  EXPECT_CALL(mock_tcp.boot_services(), CheckEvent)
      .WillOnce(Return(EFI_SUCCESS))    // Accept()
      .WillOnce(Return(EFI_NOT_READY))  // Receive() #1
      .WillOnce(Return(EFI_SUCCESS));   // Receive() #2

  ASSERT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_tcp.boot_services().services(), &kTestAddress, kTestPort));
  ASSERT_EQ(TCP6_RESULT_SUCCESS, tcp6_accept(&socket));

  EXPECT_EQ(TCP6_RESULT_PENDING, tcp6_read(&socket, data.data(), data.size()));
  EXPECT_EQ(TCP6_RESULT_SUCCESS, tcp6_read(&socket, data.data(), data.size()));
}

TEST(TcpTest, ReadPartial) {
  MockTcp mock_tcp;
  tcp6_socket socket = {};
  std::array<uint8_t, 8> data;

  EXPECT_CALL(mock_tcp.client_protocol(), Receive)
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
            tcp6_open(&socket, mock_tcp.boot_services().services(), &kTestAddress, kTestPort));
  ASSERT_EQ(TCP6_RESULT_SUCCESS, tcp6_accept(&socket));

  // When we see a partial read we try again immediately, so we should only
  // have to call tcp6_read() once, but this is an implementation detail so
  // may change in the future.
  EXPECT_EQ(TCP6_RESULT_SUCCESS, tcp6_read(&socket, data.data(), data.size()));
}

TEST(TcpTest, ReadFailCreateEvent) {
  MockTcp mock_tcp;
  tcp6_socket socket = {};
  std::array<uint8_t, 8> data;

  EXPECT_CALL(mock_tcp.boot_services(), CreateEvent)
      .WillOnce(Return(EFI_SUCCESS))            // Accept()
      .WillOnce(Return(EFI_OUT_OF_RESOURCES));  // Receive()

  ASSERT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_tcp.boot_services().services(), &kTestAddress, kTestPort));
  ASSERT_EQ(TCP6_RESULT_SUCCESS, tcp6_accept(&socket));

  EXPECT_EQ(TCP6_RESULT_ERROR, tcp6_read(&socket, data.data(), data.size()));
}

TEST(TcpTest, ReadFailReceive) {
  MockTcp mock_tcp;
  tcp6_socket socket = {};
  std::array<uint8_t, 8> data;

  EXPECT_CALL(mock_tcp.client_protocol(), Receive).WillOnce(Return(EFI_OUT_OF_RESOURCES));

  ASSERT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_tcp.boot_services().services(), &kTestAddress, kTestPort));
  ASSERT_EQ(TCP6_RESULT_SUCCESS, tcp6_accept(&socket));

  EXPECT_EQ(TCP6_RESULT_ERROR, tcp6_read(&socket, data.data(), data.size()));
}

TEST(TcpTest, ReadFailCheckEvent) {
  MockTcp mock_tcp;
  tcp6_socket socket = {};
  std::array<uint8_t, 8> data;

  EXPECT_CALL(mock_tcp.boot_services(), CheckEvent)
      .WillOnce(Return(EFI_SUCCESS))            // Accept()
      .WillOnce(Return(EFI_OUT_OF_RESOURCES));  // Receive()

  ASSERT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_tcp.boot_services().services(), &kTestAddress, kTestPort));
  ASSERT_EQ(TCP6_RESULT_SUCCESS, tcp6_accept(&socket));

  EXPECT_EQ(TCP6_RESULT_ERROR, tcp6_read(&socket, data.data(), data.size()));
}

TEST(TcpTest, ReadFailCompletionError) {
  MockTcp mock_tcp;
  tcp6_socket socket = {};
  std::array<uint8_t, 8> data;

  EXPECT_CALL(mock_tcp.client_protocol(), Receive).WillOnce([](efi_tcp6_io_token* token) {
    token->CompletionToken.Status = EFI_OUT_OF_RESOURCES;
    return EFI_SUCCESS;
  });

  ASSERT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_tcp.boot_services().services(), &kTestAddress, kTestPort));
  ASSERT_EQ(TCP6_RESULT_SUCCESS, tcp6_accept(&socket));

  EXPECT_EQ(TCP6_RESULT_ERROR, tcp6_read(&socket, data.data(), data.size()));
}

TEST(TcpTest, ReadFailDisconnectFin) {
  MockTcp mock_tcp;
  tcp6_socket socket = {};
  std::array<uint8_t, 8> data;

  EXPECT_CALL(mock_tcp.client_protocol(), Receive).WillOnce([](efi_tcp6_io_token* token) {
    token->CompletionToken.Status = EFI_CONNECTION_FIN;
    return EFI_SUCCESS;
  });

  ASSERT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_tcp.boot_services().services(), &kTestAddress, kTestPort));
  ASSERT_EQ(TCP6_RESULT_SUCCESS, tcp6_accept(&socket));

  EXPECT_EQ(TCP6_RESULT_DISCONNECTED, tcp6_read(&socket, data.data(), data.size()));
}

TEST(TcpTest, ReadFailDisconnectReset) {
  MockTcp mock_tcp;
  tcp6_socket socket = {};
  std::array<uint8_t, 8> data;

  EXPECT_CALL(mock_tcp.client_protocol(), Receive).WillOnce([](efi_tcp6_io_token* token) {
    token->CompletionToken.Status = EFI_CONNECTION_RESET;
    return EFI_SUCCESS;
  });

  ASSERT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_tcp.boot_services().services(), &kTestAddress, kTestPort));
  ASSERT_EQ(TCP6_RESULT_SUCCESS, tcp6_accept(&socket));

  EXPECT_EQ(TCP6_RESULT_DISCONNECTED, tcp6_read(&socket, data.data(), data.size()));
}

TEST(TcpTest, ReadFailOverflow) {
  MockTcp mock_tcp;
  tcp6_socket socket = {};
  std::array<uint8_t, 8> data;

  // Receive() returns success, but gives us more data than expected.
  EXPECT_CALL(mock_tcp.client_protocol(), Receive).WillOnce([](efi_tcp6_io_token* token) {
    token->Packet.RxData->DataLength = 10;
    token->Packet.RxData->FragmentTable[0].FragmentLength = 10;
    return EFI_SUCCESS;
  });

  ASSERT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_tcp.boot_services().services(), &kTestAddress, kTestPort));
  ASSERT_EQ(TCP6_RESULT_SUCCESS, tcp6_accept(&socket));

  EXPECT_EQ(TCP6_RESULT_ERROR, tcp6_read(&socket, data.data(), data.size()));
}

TEST(TcpTest, Write) {
  MockTcp mock_tcp;
  tcp6_socket socket = {};
  std::array<uint8_t, 8> data;

  // Make sure we pass the expected parameters through.
  EXPECT_CALL(mock_tcp.client_protocol(), Transmit).WillOnce([&data](efi_tcp6_io_token* token) {
    EXPECT_TRUE(token->Packet.TxData->Push);
    EXPECT_FALSE(token->Packet.TxData->Urgent);
    EXPECT_EQ(token->Packet.TxData->DataLength, data.size());
    EXPECT_EQ(token->Packet.TxData->FragmentCount, 1u);
    EXPECT_EQ(token->Packet.TxData->FragmentTable[0].FragmentLength, data.size());
    EXPECT_EQ(token->Packet.TxData->FragmentTable[0].FragmentBuffer, data.data());
    return EFI_SUCCESS;
  });

  // Make sure we call Poll() each time we read, for performance.
  EXPECT_CALL(mock_tcp.client_protocol(), Poll);

  ASSERT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_tcp.boot_services().services(), &kTestAddress, kTestPort));
  ASSERT_EQ(TCP6_RESULT_SUCCESS, tcp6_accept(&socket));

  EXPECT_EQ(TCP6_RESULT_SUCCESS, tcp6_write(&socket, data.data(), data.size()));
}

TEST(TcpTest, WriteFailCreateEvent) {
  MockTcp mock_tcp;
  tcp6_socket socket = {};
  std::array<uint8_t, 8> data;

  EXPECT_CALL(mock_tcp.boot_services(), CreateEvent)
      .WillOnce(Return(EFI_SUCCESS))            // Accept()
      .WillOnce(Return(EFI_OUT_OF_RESOURCES));  // Transmit()

  ASSERT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_tcp.boot_services().services(), &kTestAddress, kTestPort));
  ASSERT_EQ(TCP6_RESULT_SUCCESS, tcp6_accept(&socket));

  EXPECT_EQ(TCP6_RESULT_ERROR, tcp6_write(&socket, data.data(), data.size()));
}

TEST(TcpTest, WriteFailTransmit) {
  MockTcp mock_tcp;
  tcp6_socket socket = {};
  std::array<uint8_t, 8> data;

  EXPECT_CALL(mock_tcp.client_protocol(), Transmit).WillOnce(Return(EFI_OUT_OF_RESOURCES));

  ASSERT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_tcp.boot_services().services(), &kTestAddress, kTestPort));
  ASSERT_EQ(TCP6_RESULT_SUCCESS, tcp6_accept(&socket));

  EXPECT_EQ(TCP6_RESULT_ERROR, tcp6_write(&socket, data.data(), data.size()));
}

TEST(TcpTest, WriteFailCheckEvent) {
  MockTcp mock_tcp;
  tcp6_socket socket = {};
  std::array<uint8_t, 8> data;

  EXPECT_CALL(mock_tcp.boot_services(), CheckEvent)
      .WillOnce(Return(EFI_SUCCESS))            // Accept()
      .WillOnce(Return(EFI_OUT_OF_RESOURCES));  // Transmit()

  ASSERT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_tcp.boot_services().services(), &kTestAddress, kTestPort));
  ASSERT_EQ(TCP6_RESULT_SUCCESS, tcp6_accept(&socket));

  EXPECT_EQ(TCP6_RESULT_ERROR, tcp6_write(&socket, data.data(), data.size()));
}

TEST(TcpTest, WriteFailCompletionError) {
  MockTcp mock_tcp;
  tcp6_socket socket = {};
  std::array<uint8_t, 8> data;

  EXPECT_CALL(mock_tcp.client_protocol(), Transmit).WillOnce([](efi_tcp6_io_token* token) {
    token->CompletionToken.Status = EFI_OUT_OF_RESOURCES;
    return EFI_SUCCESS;
  });

  ASSERT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_tcp.boot_services().services(), &kTestAddress, kTestPort));
  ASSERT_EQ(TCP6_RESULT_SUCCESS, tcp6_accept(&socket));

  EXPECT_EQ(TCP6_RESULT_ERROR, tcp6_write(&socket, data.data(), data.size()));
}

TEST(TcpTest, WriteFailDisconnectFin) {
  MockTcp mock_tcp;
  tcp6_socket socket = {};
  std::array<uint8_t, 8> data;

  EXPECT_CALL(mock_tcp.client_protocol(), Transmit).WillOnce([](efi_tcp6_io_token* token) {
    token->CompletionToken.Status = EFI_CONNECTION_FIN;
    return EFI_SUCCESS;
  });

  ASSERT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_tcp.boot_services().services(), &kTestAddress, kTestPort));
  ASSERT_EQ(TCP6_RESULT_SUCCESS, tcp6_accept(&socket));

  EXPECT_EQ(TCP6_RESULT_DISCONNECTED, tcp6_write(&socket, data.data(), data.size()));
}

TEST(TcpTest, WriteFailDisconnectReset) {
  MockTcp mock_tcp;
  tcp6_socket socket = {};
  std::array<uint8_t, 8> data;

  EXPECT_CALL(mock_tcp.client_protocol(), Transmit).WillOnce([](efi_tcp6_io_token* token) {
    token->CompletionToken.Status = EFI_CONNECTION_RESET;
    return EFI_SUCCESS;
  });

  ASSERT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_tcp.boot_services().services(), &kTestAddress, kTestPort));
  ASSERT_EQ(TCP6_RESULT_SUCCESS, tcp6_accept(&socket));

  EXPECT_EQ(TCP6_RESULT_DISCONNECTED, tcp6_write(&socket, data.data(), data.size()));
}

TEST(TcpTest, WriteFailPartial) {
  MockTcp mock_tcp;
  tcp6_socket socket = {};
  std::array<uint8_t, 8> data;

  // Transmit() returns success, but gives us less data than expected.
  EXPECT_CALL(mock_tcp.client_protocol(), Transmit).WillOnce([](efi_tcp6_io_token* token) {
    token->Packet.TxData->DataLength = 4;
    token->Packet.TxData->FragmentTable[0].FragmentLength = 4;
    return EFI_SUCCESS;
  });

  ASSERT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_tcp.boot_services().services(), &kTestAddress, kTestPort));
  ASSERT_EQ(TCP6_RESULT_SUCCESS, tcp6_accept(&socket));

  EXPECT_EQ(TCP6_RESULT_ERROR, tcp6_write(&socket, data.data(), data.size()));
}

TEST(TcpTest, Disconnect) {
  MockTcp mock_tcp;
  tcp6_socket socket = {};

  mock_tcp.ExpectDisconnect();

  ASSERT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_tcp.boot_services().services(), &kTestAddress, kTestPort));
  ASSERT_EQ(TCP6_RESULT_SUCCESS, tcp6_accept(&socket));

  EXPECT_EQ(TCP6_RESULT_SUCCESS, tcp6_disconnect(&socket));
}

TEST(TcpTest, DisconnectTwice) {
  MockTcp mock_tcp;
  tcp6_socket socket = {};

  // We should only try to disconnect once, the second should be a no-op.
  mock_tcp.ExpectDisconnect();

  ASSERT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_tcp.boot_services().services(), &kTestAddress, kTestPort));
  ASSERT_EQ(TCP6_RESULT_SUCCESS, tcp6_accept(&socket));

  EXPECT_EQ(TCP6_RESULT_SUCCESS, tcp6_disconnect(&socket));
  EXPECT_EQ(TCP6_RESULT_SUCCESS, tcp6_disconnect(&socket));
}

TEST(TcpTest, DisconnectPending) {
  MockTcp mock_tcp;
  tcp6_socket socket = {};

  // Accept is ready the first time, but disconnect isn't.
  EXPECT_CALL(mock_tcp.boot_services(), CheckEvent)
      .WillOnce(Return(EFI_SUCCESS))    // Accept()
      .WillOnce(Return(EFI_NOT_READY))  // Close() #1
      .WillOnce(Return(EFI_SUCCESS));   // Close() #2
  mock_tcp.ExpectDisconnect();

  ASSERT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_tcp.boot_services().services(), &kTestAddress, kTestPort));
  ASSERT_EQ(TCP6_RESULT_SUCCESS, tcp6_accept(&socket));

  EXPECT_EQ(TCP6_RESULT_PENDING, tcp6_disconnect(&socket));
  EXPECT_EQ(TCP6_RESULT_SUCCESS, tcp6_disconnect(&socket));
}

TEST(TcpTest, Close) {
  MockTcp mock_tcp;
  tcp6_socket socket = {};

  mock_tcp.ExpectServerClose();

  EXPECT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_tcp.boot_services().services(), &kTestAddress, kTestPort));
  EXPECT_EQ(TCP6_RESULT_SUCCESS, tcp6_close(&socket));

  EXPECT_EQ(socket.binding_protocol, nullptr);
  EXPECT_EQ(socket.server_protocol, nullptr);
  EXPECT_EQ(socket.client_protocol, nullptr);
}

TEST(TcpTest, CloseWithClient) {
  MockTcp mock_tcp;
  tcp6_socket socket = {};

  InSequence sequence;
  mock_tcp.ExpectDisconnect();
  mock_tcp.ExpectServerClose();

  EXPECT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_tcp.boot_services().services(), &kTestAddress, kTestPort));
  EXPECT_EQ(TCP6_RESULT_SUCCESS, tcp6_accept(&socket));
  EXPECT_EQ(TCP6_RESULT_SUCCESS, tcp6_close(&socket));
}

TEST(TcpTest, CloseTwice) {
  MockTcp mock_tcp;
  tcp6_socket socket = {};

  // All these functions should still only be called once, closing the socket
  // a second time should be a no-op.
  mock_tcp.ExpectServerClose();

  EXPECT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_tcp.boot_services().services(), &kTestAddress, kTestPort));
  EXPECT_EQ(TCP6_RESULT_SUCCESS, tcp6_close(&socket));
  EXPECT_EQ(TCP6_RESULT_SUCCESS, tcp6_close(&socket));
}

TEST(TcpTest, ClosePending) {
  MockTcp mock_tcp;
  tcp6_socket socket = {};

  // Have the close event not be ready on the first check.
  EXPECT_CALL(mock_tcp.boot_services(), CheckEvent)
      .WillOnce(Return(EFI_NOT_READY))  // Close() #1
      .WillOnce(Return(EFI_SUCCESS));   // Close() #2

  // All the members should still be closed exactly once each.
  mock_tcp.ExpectServerClose();

  EXPECT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_tcp.boot_services().services(), &kTestAddress, kTestPort));
  EXPECT_EQ(TCP6_RESULT_PENDING, tcp6_close(&socket));
  EXPECT_EQ(TCP6_RESULT_SUCCESS, tcp6_close(&socket));
}

}  // namespace
