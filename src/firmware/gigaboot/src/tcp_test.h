// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Test utilities for mocking out TCP behavior.

#ifndef SRC_FIRMWARE_GIGABOOT_SRC_TCP_TEST_H_
#define SRC_FIRMWARE_GIGABOOT_SRC_TCP_TEST_H_

#include <lib/efi/testing/mock_service_binding.h>
#include <lib/efi/testing/mock_tcp6.h>
#include <lib/efi/testing/stub_boot_services.h>

#include <set>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "tcp.h"

// Wraps EFI TCP protocols in mocks for testing.
//
// Configures mocks such that tcp6_*() functions will succeed by default.
// Tests can use EXPECT_CALL() to override default behavior if needed.
//
// Additionally tracks event create/close calls to make sure every created
// event is also closed.
//
// To use, pass the MockTcp's boot_services protocol table to tcp6_open():
//   MockTcp mock_tcp;
//   tcp6_socket socket;
//   tcp6_open(mock_tcp.boot_services().services(), &socket);
class MockTcp {
 public:
  // Fake objects used by the default mock behavior that can be used by tests
  // to validate behavior.
  static const efi_handle kTcpBindingHandle;
  static const efi_handle kTcpServerHandle;
  static const efi_handle kTcpClientHandle;

  MockTcp();
  ~MockTcp();

  // Accessors for underlying mock objects.
  efi::MockBootServices& boot_services() { return mock_boot_services_; }
  efi::MockServiceBindingProtocol& binding_protocol() { return mock_binding_protocol_; }
  efi::MockTcp6Protocol& server_protocol() { return mock_server_protocol_; }
  efi::MockTcp6Protocol& client_protocol() { return mock_client_protocol_; }

  // Adds expectations that the socket server and binding protocols are closed.
  //
  // This isn't necessary for proper functionality, it only adds checks that all
  // the members are closed out if a test wants to specifically look for that.
  void ExpectServerClose();

  // Adds expectations that the socket client is disconnected.
  //
  // This isn't necessary for proper functionality, it only adds checks that all
  // the members are closed out if a test wants to specifically look for that.
  void ExpectDisconnect();

  // Allocates a handle buffer and sets it to the given contents.
  // Useful for mocking LocateHandleBuffer().
  efi_status AllocateHandleBuffer(size_t* num_handles, efi_handle** buf,
                                  std::vector<efi_handle> handles);

 private:
  // NiceMock so that we can use ON_CALL/WillByDefault without it spamming
  // a bunch of "uninteresting call" messages.
  ::testing::NiceMock<efi::MockBootServices> mock_boot_services_;
  ::testing::NiceMock<efi::MockServiceBindingProtocol> mock_binding_protocol_;
  ::testing::NiceMock<efi::MockTcp6Protocol> mock_server_protocol_;
  ::testing::NiceMock<efi::MockTcp6Protocol> mock_client_protocol_;

  // Track created events so we can make sure we close them all.
  std::set<efi_event> created_events_;
};

#endif  // SRC_FIRMWARE_GIGABOOT_SRC_TCP_TEST_H_
