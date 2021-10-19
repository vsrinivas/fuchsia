// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tcp.h"

#include <lib/efi/testing/mock_service_binding.h>
#include <lib/efi/testing/mock_tcp6.h>
#include <lib/efi/testing/stub_boot_services.h>

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
const efi_handle kTestEvent = reinterpret_cast<efi_event>(0x100);
const efi_ipv6_addr kTestAddress = {.addr = {0x01, 0x23, 0x45, 0x67}};
const uint16_t kTestPort = 12345;

// Test fixture to handle common setup/teardown.
//
// Configures mocks such that opening and closing a tcp6_socket will succeed.
// Tests can use EXPECT_CALL() to override default behavior if needed.
class TcpTest : public Test {
 public:
  void SetUp() override {
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

    // It's important to create non-null events, since the TCP code checks
    // against null to determine if the event is pending or not.
    ON_CALL(mock_boot_services_, CreateEvent)
        .WillByDefault([](Unused, Unused, Unused, Unused, efi_event* event) {
          *event = kTestEvent;
          return EFI_SUCCESS;
        });

    // For tcp6_close(), the default behavior of returning 0 (EFI_SUCCESS)
    // works without any explicit mocking.
    static_assert(EFI_SUCCESS == 0, "Fix tcp6_close() mocking");
  }

  // Adds expectations that all the socket members will be closed.
  // This isn't necessary for proper functionality, it only adds checks that all
  // the members are closed out if a test wants to specifically look for that.
  void ExpectSocketClose() {
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
  ExpectSocketClose();

  EXPECT_EQ(TCP6_RESULT_ERROR,
            tcp6_open(&socket, mock_boot_services_.services(), &kTestAddress, kTestPort));
}

TEST_F(TcpTest, Close) {
  tcp6_socket socket = {};

  ExpectSocketClose();

  EXPECT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_boot_services_.services(), &kTestAddress, kTestPort));
  EXPECT_EQ(TCP6_RESULT_SUCCESS, tcp6_close(&socket));

  EXPECT_EQ(socket.binding_protocol, nullptr);
  EXPECT_EQ(socket.server_protocol, nullptr);
}

TEST_F(TcpTest, CloseTwice) {
  tcp6_socket socket = {};

  // All these functions should still only be called once, closing the socket
  // a second time should be a no-op.
  ExpectSocketClose();

  EXPECT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_boot_services_.services(), &kTestAddress, kTestPort));
  EXPECT_EQ(TCP6_RESULT_SUCCESS, tcp6_close(&socket));
  EXPECT_EQ(TCP6_RESULT_SUCCESS, tcp6_close(&socket));
}

TEST_F(TcpTest, ClosePending) {
  tcp6_socket socket = {};

  // Have the close event not be ready on the first check.
  EXPECT_CALL(mock_boot_services_, CheckEvent(kTestEvent))
      .WillOnce(Return(EFI_NOT_READY))
      .WillOnce(Return(EFI_SUCCESS));

  // All the members should still be closed exactly once each.
  ExpectSocketClose();

  EXPECT_EQ(TCP6_RESULT_SUCCESS,
            tcp6_open(&socket, mock_boot_services_.services(), &kTestAddress, kTestPort));
  EXPECT_EQ(TCP6_RESULT_PENDING, tcp6_close(&socket));
  EXPECT_EQ(TCP6_RESULT_SUCCESS, tcp6_close(&socket));
}

}  // namespace
