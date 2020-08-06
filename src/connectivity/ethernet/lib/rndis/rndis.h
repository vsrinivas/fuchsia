// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_ETHERNET_LIB_RNDIS_RNDIS_H_
#define SRC_CONNECTIVITY_ETHERNET_LIB_RNDIS_RNDIS_H_

#include <stdint.h>
#include <zircon/compiler.h>

// clang-format off

#define RNDIS_MAJOR_VERSION             0x00000001
#define RNDIS_MINOR_VERSION             0x00000000
#define RNDIS_MAX_XFER_SIZE             0x00004000

// Messages
#define RNDIS_PACKET_MSG                0x00000001
#define RNDIS_INITIALIZE_MSG            0x00000002
#define RNDIS_HALT_MSG                  0x00000003
#define RNDIS_QUERY_MSG                 0x00000004
#define RNDIS_SET_MSG                   0x00000005
#define RNDIS_RESET_MSG                 0x00000006
#define RNDIS_INDICATE_STATUS_MSG       0x00000007
#define RNDIS_KEEPALIVE_MSG             0x00000008
#define RNDIS_INITIALIZE_CMPLT          0x80000002
#define RNDIS_QUERY_CMPLT               0x80000004
#define RNDIS_SET_CMPLT                 0x80000005
#define RNDIS_RESET_CMPLT               0x80000006
#define RNDIS_KEEPALIVE_CMPLT           0x80000008

// Statuses
#define RNDIS_STATUS_SUCCESS            0x00000000
#define RNDIS_STATUS_FAILURE            0xC0000001
#define RNDIS_STATUS_INVALID_DATA       0xC0010015
#define RNDIS_STATUS_NOT_SUPPORTED      0xC00000BB
#define RNDIS_STATUS_MEDIA_CONNECT      0x4001000B
#define RNDIS_STATUS_MEDIA_DISCONNECT   0x4001000C

// General OIDs
#define OID_GEN_SUPPORTED_LIST          0x00010101
#define OID_GEN_HARDWARE_STATUS         0x00010102
#define OID_GEN_MEDIA_SUPPORTED         0x00010103
#define OID_GEN_MEDIA_IN_USE            0x00010104
#define OID_GEN_MAXIMUM_LOOKAHEAD       0x00010105
#define OID_GEN_MAXIMUM_FRAME_SIZE      0x00010106
#define OID_GEN_LINK_SPEED              0x00010107
#define OID_GEN_TRANSMIT_BUFFER_SPACE   0x00010108
#define OID_GEN_RECEIVE_BUFFER_SPACE    0x00010109
#define OID_GEN_TRANSMIT_BLOCK_SIZE     0x0001010A
#define OID_GEN_RECEIVE_BLOCK_SIZE      0x0001010B
#define OID_GEN_VENDOR_ID               0x0001010C
#define OID_GEN_VENDOR_DESCRIPTION      0x0001010D
#define OID_GEN_CURRENT_PACKET_FILTER   0x0001010E
#define OID_GEN_CURRENT_LOOKAHEAD       0x0001010F
#define OID_GEN_DRIVER_VERSION          0x00010110
#define OID_GEN_MAXIMUM_TOTAL_SIZE      0x00010111
#define OID_GEN_PROTOCOL_OPTIONS        0x00010112
#define OID_GEN_MAC_OPTIONS             0x00010113
#define OID_GEN_MEDIA_CONNECT_STATUS    0x00010114
#define OID_GEN_MAXIMUM_SEND_PACKETS    0x00010115
#define OID_GEN_VENDOR_DRIVER_VERSION   0x00010116
#define OID_GEN_SUPPORTED_GUIDS         0x00010117
#define OID_GEN_NETWORK_LAYER_ADDRESSES 0x00010118
#define OID_GEN_TRANSPORT_HEADER_OFFSET 0x00010119
#define OID_GEN_PHYSICAL_MEDIUM         0x00010202
#define OID_GEN_MACHINE_NAME            0x0001021A
#define OID_GEN_RNDIS_CONFIG_PARAMETER  0x0001021B
#define OID_GEN_VLAN_ID                 0x0001021C

// General statistical OIDs
#define OID_GEN_XMIT_OK                 0x00020101
#define OID_GEN_RCV_OK                  0x00020102
#define OID_GEN_XMIT_ERROR              0x00020103
#define OID_GEN_RCV_ERROR               0x00020104
#define OID_GEN_RCV_NO_BUFFER           0x00020105
#define OID_GEN_DIRECTED_BYTES_XMIT     0x00020201
#define OID_GEN_DIRECTED_FRAMES_XMIT    0x00020202
#define OID_GEN_MULTICAST_BYTES_XMIT    0x00020203
#define OID_GEN_MULTICAST_FRAMES_XMIT   0x00020204
#define OID_GEN_BROADCAST_BYTES_XMIT    0x00020205
#define OID_GEN_BROADCAST_FRAMES_XMIT   0x00020206
#define OID_GEN_DIRECTED_BYTES_RCV      0x00020207
#define OID_GEN_DIRECTED_FRAMES_RCV     0x00020208
#define OID_GEN_MULTICAST_BYTES_RCV     0x00020209
#define OID_GEN_MULTICAST_FRAMES_RCV    0x0002020A
#define OID_GEN_BROADCAST_BYTES_RCV     0x0002020B
#define OID_GEN_BROADCAST_FRAMES_RCV    0x0002020C

// 802.3 OIDs
#define OID_802_3_PERMANENT_ADDRESS     0x01010101
#define OID_802_3_CURRENT_ADDRESS       0x01010102
#define OID_802_3_MULTICAST_LIST        0x01010103
#define OID_802_3_MAXIMUM_LIST_SIZE     0x01010104
#define OID_802_3_MAC_OPTIONS           0x01010105

#define OID_802_3_RCV_ERROR_ALIGNMENT   0x01020101
#define OID_802_3_XMIT_ONE_COLLISION    0x01020102
#define OID_802_3_XMIT_MORE_COLLISIONS  0x01020103

// Filter options
#define RNDIS_PACKET_TYPE_DIRECTED        0x00000001
#define RNDIS_PACKET_TYPE_MULTICAST       0x00000002
#define RNDIS_PACKET_TYPE_ALL_MULTICAST   0x00000004
#define RNDIS_PACKET_TYPE_BROADCAST       0x00000008
#define RNDIS_PACKET_TYPE_SOURCE_ROUTING  0x00000010
#define RNDIS_PACKET_TYPE_PROMISCUOUS     0x00000020
#define RNDIS_PACKET_TYPE_SMT             0x00000040
#define RNDIS_PACKET_TYPE_ALL_LOCAL       0x00000080
#define RNDIS_PACKET_TYPE_GROUP           0x00001000
#define RNDIS_PACKET_TYPE_ALL_FUNCTIONAL  0x00002000
#define RNDIS_PACKET_TYPE_FUNCTIONAL      0x00004000
#define RNDIS_PACKET_TYPE_MAC_FRAME       0x00008000

#define RNDIS_SET_INFO_BUFFER_LENGTH      0x00000014

#define RNDIS_MAX_DATA_SIZE (ETH_FRAME_MAX_SIZE)
#define RNDIS_BUFFER_SIZE (RNDIS_MAX_DATA_SIZE + sizeof(rndis_packet_header))
#define RNDIS_QUERY_BUFFER_OFFSET 20
#define RNDIS_CONTROL_TIMEOUT ZX_SEC(5)
#define RNDIS_CONTROL_BUFFER_SIZE 1024

// Flags
#define RNDIS_DF_CONNECTIONLESS           0x00000001
#define RNDIS_DF_CONNECTION_ORIENTED      0x00000002

// Mediums
#define RNDIS_MEDIUM_802_3                0x00000000

// Hardware statuses
#define RNDIS_HW_STATUS_READY             0
#define RNDIS_HW_STATUS_INITIALIZING      1
#define RNDIS_HW_STATUS_RESET             2
#define RNDIS_HW_STATUS_CLOSING           3
#define RNDIS_HW_STATUS_NOT_READY         4

// clang-format on

typedef struct {
  uint32_t msg_type;
  uint32_t msg_length;
  uint32_t request_id;
} __PACKED rndis_header;

typedef struct {
  uint32_t msg_type;
  uint32_t msg_length;
  uint32_t request_id;
  uint32_t status;
} __PACKED rndis_header_complete;

typedef struct {
  uint32_t msg_type;
  uint32_t msg_length;
  uint32_t request_id;
  uint32_t major_version;
  uint32_t minor_version;
  uint32_t max_xfer_size;
} __PACKED rndis_init;

typedef struct {
  uint32_t msg_type;
  uint32_t msg_length;
  uint32_t request_id;
  uint32_t status;
  uint32_t major_version;
  uint32_t minor_version;
  uint32_t device_flags;
  uint32_t medium;
  uint32_t max_packets_per_xfer;
  uint32_t max_xfer_size;
  uint32_t packet_alignment;
  uint32_t reserved0;
  uint32_t reserved1;
} __PACKED rndis_init_complete;

typedef struct {
  uint32_t msg_type;
  uint32_t msg_length;
  uint32_t status;
  uint32_t addressing_reset;
} __PACKED rndis_reset_complete;

typedef struct {
  uint32_t msg_type;
  uint32_t msg_length;
  uint32_t request_id;
  uint32_t oid;
  uint32_t info_buffer_length;
  uint32_t info_buffer_offset;
  uint32_t reserved;
} __PACKED rndis_query;

typedef struct {
  uint32_t msg_type;
  uint32_t msg_length;
  uint32_t request_id;
  uint32_t status;
  uint32_t info_buffer_length;
  uint32_t info_buffer_offset;
} __PACKED rndis_query_complete;

typedef struct {
  uint32_t msg_type;
  uint32_t msg_length;
  uint32_t request_id;
  uint32_t oid;
  uint32_t info_buffer_length;
  uint32_t info_buffer_offset;
  uint32_t reserved;
} __PACKED rndis_set;

typedef struct {
  uint32_t msg_type;
  uint32_t msg_length;
  uint32_t status;
  uint32_t status_buffer_length;
  uint32_t status_buffer_offset;
} __PACKED rndis_indicate_status;

typedef struct {
  uint32_t diagnostic_status;
  uint32_t error_offset;
} __PACKED rndis_diagnostic_info;

typedef struct {
  uint32_t msg_type;
  uint32_t msg_length;
  uint32_t request_id;
  uint32_t status;
} __PACKED rndis_set_complete;

typedef struct {
  uint32_t msg_type;
  uint32_t msg_length;
  uint32_t data_offset;
  uint32_t data_length;
  uint32_t oob_data_offset;
  uint32_t oob_data_length;
  uint32_t num_oob_elements;
  uint32_t per_packet_info_offset;
  uint32_t per_packet_info_length;
  uint32_t reserved0;
  uint32_t reserved1;
} __PACKED rndis_packet_header;

typedef struct {
  uint32_t notification;
  uint32_t reserved;
} __PACKED rndis_notification;

#endif  // SRC_CONNECTIVITY_ETHERNET_LIB_RNDIS_RNDIS_H_
