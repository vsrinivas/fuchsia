// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_KERNEL_LIB_EFI_TESTING_INCLUDE_LIB_EFI_TESTING_MOCK_TCP6_H_
#define ZIRCON_KERNEL_LIB_EFI_TESTING_INCLUDE_LIB_EFI_TESTING_MOCK_TCP6_H_

#include <efi/protocol/tcp6.h>
#include <gmock/gmock.h>

#include "mock_protocol_base.h"

namespace efi {

// gmock wrapper for efi_tcp6_protocol.
class MockTcp6Protocol : public MockProtocolBase<MockTcp6Protocol, efi_tcp6_protocol> {
 public:
  MockTcp6Protocol()
      : MockProtocolBase({.GetModeData = Bounce<&MockTcp6Protocol::GetModeData>,
                          .Configure = Bounce<&MockTcp6Protocol::Configure>,
                          .Connect = Bounce<&MockTcp6Protocol::Connect>,
                          .Accept = Bounce<&MockTcp6Protocol::Accept>,
                          .Transmit = Bounce<&MockTcp6Protocol::Transmit>,
                          .Receive = Bounce<&MockTcp6Protocol::Receive>,
                          .Close = Bounce<&MockTcp6Protocol::Close>,
                          .Cancel = Bounce<&MockTcp6Protocol::Cancel>,
                          .Poll = Bounce<&MockTcp6Protocol::Poll>}) {}

  MOCK_METHOD(efi_status, GetModeData,
              (efi_tcp6_connection_state * tcp6_state, efi_tcp6_config_data* tcp6_config_data,
               efi_ip6_mode_data* ip6_mode_data, efi_managed_network_config_data* mnp_config_data,
               efi_simple_network_mode* snp_mode_data));
  MOCK_METHOD(efi_status, Configure, (efi_tcp6_config_data * tcp6_config_data));
  MOCK_METHOD(efi_status, Connect, (efi_tcp6_connection_token * connection_token));
  MOCK_METHOD(efi_status, Accept, (efi_tcp6_listen_token * listen_token));
  MOCK_METHOD(efi_status, Transmit, (efi_tcp6_io_token * token));
  MOCK_METHOD(efi_status, Receive, (efi_tcp6_io_token * token));
  MOCK_METHOD(efi_status, Close, (efi_tcp6_close_token * close_token));
  MOCK_METHOD(efi_status, Cancel, (efi_tcp6_completion_token * token));
  MOCK_METHOD(efi_status, Poll, ());
};

}  // namespace efi

#endif  // ZIRCON_KERNEL_LIB_EFI_TESTING_INCLUDE_LIB_EFI_TESTING_MOCK_TCP6_H_
