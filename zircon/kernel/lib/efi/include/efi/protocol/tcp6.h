// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_KERNEL_LIB_EFI_INCLUDE_EFI_PROTOCOL_TCP6_H_
#define ZIRCON_KERNEL_LIB_EFI_INCLUDE_EFI_PROTOCOL_TCP6_H_

#include <stdbool.h>

#include <efi/protocol/ip6.h>
#include <efi/protocol/managed-network.h>
#include <efi/protocol/simple-network.h>
#include <efi/types.h>

#define EFI_TCP6_SERVICE_BINDING_PROTOCOL_GUID                                     \
  {                                                                                \
    0xec20eb79, 0x6c1a, 0x4664, { 0x9a, 0x0d, 0xd2, 0xe4, 0xcc, 0x16, 0xd6, 0x64 } \
  }

#define EFI_TCP6_PROTOCOL_GUID                                                     \
  {                                                                                \
    0x46e44855, 0xbd60, 0x4ab7, { 0xab, 0x0d, 0xa6, 0x79, 0xb9, 0x44, 0x7d, 0x77 } \
  }

typedef struct {
  efi_ipv6_addr StationAddress;
  uint16_t StationPort;
  efi_ipv6_addr RemoteAddress;
  uint16_t RemotePort;
  bool ActiveFlag;
} efi_tcp6_access_point;

typedef struct {
  uint32_t ReceiveBufferSize;
  uint32_t SendBufferSize;
  uint32_t MaxSynBackLog;
  uint32_t ConnectionTimeout;
  uint32_t DataRetries;
  uint32_t FinTimeout;
  uint32_t TimeWaitTimeout;
  uint32_t KeepAliveProbes;
  uint32_t KeepAliveTime;
  uint32_t KeepAliveInterval;
  bool EnableNagle;
  bool EnableTimeStamp;
  bool EnableWindowScaling;
  bool EnableSelectiveAck;
  bool EnablePathMtuDiscovery;
} efi_tcp6_option;

typedef struct {
  uint8_t TrafficClass;
  uint8_t HopLimit;
  efi_tcp6_access_point AccessPoint;
  efi_tcp6_option* ControlOption;
} efi_tcp6_config_data;

typedef enum {
  Tcp6StateClosed = 0,
  Tcp6StateListen = 1,
  Tcp6StateSynSent = 2,
  Tcp6StateSynReceived = 3,
  Tcp6StateEstablished = 4,
  Tcp6StateFinWait1 = 5,
  Tcp6StateFinWait2 = 6,
  Tcp6StateClosing = 7,
  Tcp6StateTimeWait = 8,
  Tcp6StateCloseWait = 9,
  Tcp6StateLastAck = 10
} efi_tcp6_connection_state;

typedef struct {
  efi_event Event;
  efi_status Status;
} efi_tcp6_completion_token;

typedef struct {
  efi_tcp6_completion_token CompletionToken;
} efi_tcp6_connection_token;

typedef struct {
  efi_tcp6_completion_token CompletionToken;
  efi_handle NewChildHandle;
} efi_tcp6_listen_token;

typedef struct {
  uint32_t FragmentLength;
  void* FragmentBuffer;
} efi_tcp6_fragment_data;

typedef struct {
  bool UrgentFlag;
  uint32_t DataLength;
  uint32_t FragmentCount;
  efi_tcp6_fragment_data FragmentTable[1];
} efi_tcp6_receive_data;

typedef struct {
  bool Push;
  bool Urgent;
  uint32_t DataLength;
  uint32_t FragmentCount;
  efi_tcp6_fragment_data FragmentTable[1];
} efi_tcp6_transmit_data;

typedef struct {
  efi_tcp6_completion_token CompletionToken;
  union {
    efi_tcp6_receive_data* RxData;
    efi_tcp6_transmit_data* TxData;
  } Packet;
} efi_tcp6_io_token;

typedef struct {
  efi_tcp6_completion_token CompletionToken;
  bool AbortOnClose;
} efi_tcp6_close_token;

typedef struct efi_tcp6_protocol {
  efi_status (*GetModeData)(struct efi_tcp6_protocol* self, efi_tcp6_connection_state* tcp6_state,
                            efi_tcp6_config_data* tcp6_config_data,
                            efi_ip6_mode_data* ip6_mode_data,
                            efi_managed_network_config_data* mnp_config_data,
                            efi_simple_network_mode* snp_mode_data) EFIAPI;

  efi_status (*Configure)(struct efi_tcp6_protocol* self,
                          efi_tcp6_config_data* tcp6_config_data) EFIAPI;

  efi_status (*Connect)(struct efi_tcp6_protocol* self,
                        efi_tcp6_connection_token* connection_token) EFIAPI;

  efi_status (*Accept)(struct efi_tcp6_protocol* self, efi_tcp6_listen_token* listen_token) EFIAPI;

  efi_status (*Transmit)(struct efi_tcp6_protocol* self, efi_tcp6_io_token* token) EFIAPI;

  efi_status (*Receive)(struct efi_tcp6_protocol* self, efi_tcp6_io_token* token) EFIAPI;

  efi_status (*Close)(struct efi_tcp6_protocol* self, efi_tcp6_close_token* close_token) EFIAPI;

  efi_status (*Cancel)(struct efi_tcp6_protocol* self, efi_tcp6_completion_token* token) EFIAPI;

  efi_status (*Poll)(struct efi_tcp6_protocol* self) EFIAPI;
} efi_tcp6_protocol;

#endif  // ZIRCON_KERNEL_LIB_EFI_INCLUDE_EFI_PROTOCOL_TCP6_H_
