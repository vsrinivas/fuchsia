// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_KERNEL_LIB_EFI_INCLUDE_EFI_PROTOCOL_IP6_H_
#define ZIRCON_KERNEL_LIB_EFI_INCLUDE_EFI_PROTOCOL_IP6_H_

#include <stdbool.h>

#include <efi/types.h>

typedef struct {
  uint8_t DefaultProtocol;
  bool AcceptAnyProtocol;
  bool AcceptIcmpErrors;
  bool AcceptPromiscuous;
  efi_ipv6_addr DestinationAddress;
  efi_ipv6_addr StationAddress;
  uint8_t TrafficClass;
  uint8_t HopLimit;
  uint32_t FlowLabel;
  uint32_t ReceiveTimeout;
  uint32_t TransmitTimeout;
} efi_ip6_config_data;

typedef struct {
  efi_ipv6_addr Address;
  uint8_t PrefixLength;
} efi_ip6_address_info;

typedef struct {
  efi_ipv6_addr Gateway;
  efi_ipv6_addr Destination;
  uint8_t PrefixLength;
} efi_ip6_route_table;

typedef enum {
  EfiNeighborInComplete,
  EfiNeighborReachable,
  EfiNeighborStale,
  EfiNeighborDelay,
  EfiNeighborProbe
} efi_ip6_neighbor_state;

typedef struct {
  efi_ipv6_addr Neighbor;
  efi_mac_addr LinkAddress;
  efi_ip6_neighbor_state State;
} efi_ip6_neighbor_cache;

typedef struct {
  uint8_t Type;
  uint8_t Code;
} efi_ip6_icmp_type;

typedef struct {
  bool IsStarted;
  uint32_t MaxPacketSize;
  efi_ip6_config_data ConfigData;
  bool IsConfigured;
  uint32_t AddressCount;
  efi_ip6_address_info* AddressList;
  uint32_t GroupCount;
  efi_ipv6_addr* GroupTable;
  uint32_t RouteCount;
  efi_ip6_route_table* RouteTable;
  uint32_t NeighborCount;
  efi_ip6_neighbor_cache* NeighborCache;
  uint32_t PrefixCount;
  efi_ip6_address_info* PrefixTable;
  uint32_t IcmpTypeCount;
  efi_ip6_icmp_type* IcmpTypeList;
} efi_ip6_mode_data;

#endif  // ZIRCON_KERNEL_LIB_EFI_INCLUDE_EFI_PROTOCOL_IP6_H_
