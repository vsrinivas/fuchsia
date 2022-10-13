// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_ETHERNET_DRIVERS_GVNIC_ABI_H_
#define SRC_CONNECTIVITY_ETHERNET_DRIVERS_GVNIC_ABI_H_

#include <netinet/if_ether.h>
#include <zircon/compiler.h>

#include <cstddef>

#include "src/connectivity/ethernet/drivers/gvnic/bigendian.h"

// Compute the size of a field of a struct without an instantition.
#define sizeof_field(s, f) sizeof(((s *)0)->f)

// Fixed Configuration Registers in Bar 0
struct __PACKED GvnicRegisters {
  BigEndian<uint32_t> dev_status;
  BigEndian<uint32_t> drv_status;
  BigEndian<uint32_t> max_tx_queues;
  BigEndian<uint32_t> max_rx_queues;
  BigEndian<uint32_t> admin_queue_pfn;
  BigEndian<uint32_t> admin_queue_doorbell;
  BigEndian<uint32_t> admin_queue_counter;
  uint8_t padding[2];
  BigEndian<uint8_t> dma_mask;
  BigEndian<uint8_t> driver_version;
  BigEndian<uint64_t> admin_queue_base_address;
  BigEndian<uint32_t> admin_queue_length;
};
static_assert(sizeof(GvnicRegisters) == 44);

// Since we will be reading and writing individual struct members, and relying on their offsets, it
// is vital that these struct members are the correct size and offset. Check that the offsets and
// size of each member matches the docs.
#define assert_offset_size(o, s, f) \
  static_assert(offsetof(GvnicRegisters, f) == (o) && sizeof_field(GvnicRegisters, f) == (s))
assert_offset_size(0x00, 4, dev_status);
assert_offset_size(0x04, 4, drv_status);
assert_offset_size(0x08, 4, max_tx_queues);
assert_offset_size(0x0C, 4, max_rx_queues);
assert_offset_size(0x10, 4, admin_queue_pfn);
assert_offset_size(0x14, 4, admin_queue_doorbell);
assert_offset_size(0x18, 4, admin_queue_counter);
assert_offset_size(0x1C, 2, padding);
assert_offset_size(0x1E, 1, dma_mask);
assert_offset_size(0x1f, 1, driver_version);
assert_offset_size(0x20, 8, admin_queue_base_address);
assert_offset_size(0x28, 4, admin_queue_length);
#undef assert_offset_size

//
// Common definitions
//

#define PCI_VENDOR_ID_GOOGLE 0x1AE0u
#define PCI_DEV_ID_GVNIC 0x0042u

#define GVNIC_REGISTER_BAR (0)
#define GVNIC_MSIX_BAR (1)
#define GVNIC_DOORBELL_BAR (2)

// Device Status Register bits
#define GVNIC_DEVICE_STATUS_RESET (0x1u << (1))
#define GVNIC_DEVICE_STATUS_LINK_STATUS (0x1u << (2))
#define GVNIC_DEVICE_STATUS_REPORT_STATS (0x1u << (3))
#define GVNIC_DEVICE_STATUS_DEVICE_IS_RESET (0x1u << (4))

// Driver Status Register bits
#define GVNIC_DRIVER_STATUS_RUN (0x1u << (0))
#define GVNIC_DRIVER_STATUS_RESET (0x1u << (1))

// Interrupt Doorbell bits
#define GVNIC_IRQ_ACK (0x1u << (31))
#define GVNIC_IRQ_MASK (0x1u << (30))
#define GVNIC_IRQ_EVENT (0x1u << (29))
//
// Admin queue definitions
//

// The size (in octets) of the admin queue buffer.
#define GVNIC_ADMINQ_SIZE (4096)
// The length (number of elements) of the admin queue buffer.
#define GVNIC_ADMINQ_LEN (GVNIC_ADMINQ_SIZE / sizeof(GvnicAdminqEntry))

// Some of these enum values are missing intentionally.
enum GveAdminqOpcodes : uint32_t {
  GVNIC_ADMINQ_DESCRIBE_DEVICE = 0x1,
  GVNIC_ADMINQ_CONFIGURE_DEVICE_RESOURCES = 0x2,
  GVNIC_ADMINQ_REGISTER_PAGE_LIST = 0x3,
  GVNIC_ADMINQ_UNREGISTER_PAGE_LIST = 0x4,
  GVNIC_ADMINQ_CREATE_TX_QUEUE = 0x5,
  GVNIC_ADMINQ_CREATE_RX_QUEUE = 0x6,
  GVNIC_ADMINQ_DESTROY_TX_QUEUE = 0x7,
  GVNIC_ADMINQ_DESTROY_RX_QUEUE = 0x8,
  GVNIC_ADMINQ_DECONFIGURE_DEVICE_RESOURCES = 0x9,
  GVNIC_ADMINQ_SET_DRIVER_PARAMETER = 0xB,
  GVNIC_ADMINQ_REPORT_STATS = 0xC,
  GVNIC_ADMINQ_REPORT_LINK_SPEED = 0xD,
  GVNIC_ADMINQ_GET_PTYPE_MAP = 0xE,
};

#define GVNIC_ADMINQ_DEVICE_DESCRIPTOR_VERSION 1

struct __PACKED GvnicAdminqDescribeDevice {
  // Points to an allocated GvnicDeviceDescriptor. Filled by device
  BigEndian<uint64_t> device_descriptor_addr;
  BigEndian<uint32_t> device_descriptor_version;
  BigEndian<uint32_t> available_length;
};
static_assert(sizeof(GvnicAdminqDescribeDevice) == 16);

struct __PACKED GvnicDeviceDescriptor {
  // Maximum number of pages that may be registered by the guest across all queue page lists.
  BigEndian<uint64_t> max_registered_pages;
  uint8_t reserved1[2];
  // Number of entries in each Tx queue, in units of descriptors. Must be a power of two.
  BigEndian<uint16_t> tx_queue_size;
  // Number of entries in each Rx queue (Rx descriptor/data ring pair). Must be a power of two.
  BigEndian<uint16_t> rx_queue_size;
  // The default number of queues.
  BigEndian<uint16_t> default_num_queues;
  // MTU supported by the device.
  BigEndian<uint16_t> mtu;
  // Number of 32 bit completion event counters that must be allocated by the guest.
  BigEndian<uint16_t> event_counters;
  // Number of pages per qpl for TX queues.
  BigEndian<uint16_t> tx_pages_per_qpl;
  // Number of pages per qpl for RX queues.
  BigEndian<uint16_t> rx_pages_per_qpl;
  // MAC address of the NIC.
  uint8_t mac[ETH_ALEN];
  // Number of additional device options trailing the device descriptor.
  BigEndian<uint16_t> num_device_options;
  // The total length of the device descriptor in bytes, including any trailing DeviceOption fields.
  BigEndian<uint16_t> total_length;

  uint8_t reserved2[6];
};
static_assert(sizeof(GvnicDeviceDescriptor) == 40);

struct __PACKED GvnicDeviceOption {
  BigEndian<uint16_t> option_id;
  BigEndian<uint16_t> option_length;
  BigEndian<uint32_t> required_features_mask;
};
static_assert(sizeof(GvnicDeviceOption) == 8);

#define GVNIC_DEV_OPT_ID_RAW_ADDRESSING 0x1
#define GVNIC_DEV_OPT_LEN_RAW_ADDRESSING 0x0
#define GVNIC_DEV_OPT_FEAT_MASK_RAW_ADDRESSING 0x0

struct __PACKED GvnicAdminqConfigureDeviceResources {
  BigEndian<uint64_t> counter_array;
  BigEndian<uint64_t> irq_db_addr_base;
  BigEndian<uint32_t> num_counters;
  BigEndian<uint32_t> num_irq_dbs;
  BigEndian<uint32_t> irq_db_stride;
  BigEndian<uint32_t> ntfy_blk_msix_base_idx;
  uint8_t queue_format;
};
static_assert(sizeof(GvnicAdminqConfigureDeviceResources) == 33);

enum GveQueueFormat : uint8_t {
  GVNIC_UNSPECIFIED_FORMAT = 0x0,
  GVNIC_GQI_RDA_FORMAT = 0x1,  // RDA = Raw DMA Address
  GVNIC_GQI_QPL_FORMAT = 0x2,  // QPL = Queue Page List
  GVNIC_DQO_RDA_FORMAT = 0x3,  // DQO is for a future card
  GVNIC_DQO_QPL_FORMAT = 0x4,
};

#define RDA_PAGE_LIST_ID 0xFFFFFFFF

struct __PACKED GvnicAdminqRegisterPageList {
  BigEndian<uint32_t> page_list_id;
  BigEndian<uint32_t> num_pages;
  BigEndian<uint64_t> page_list_address;
  BigEndian<uint64_t> page_size;
};
static_assert(sizeof(GvnicAdminqRegisterPageList) == 24);

struct __PACKED GvnicAdminqUnregisterPageList {
  BigEndian<uint32_t> page_list_id;
};
static_assert(sizeof(GvnicAdminqUnregisterPageList) == 4);

struct __PACKED GvnicQueueResources {
  // gVNIC BAR2 is a flat array of 32-bit big endian doorbells.  db_index is the index in BAR2 of a
  // queue's doorbell.  db_index is network byte order.
  BigEndian<uint32_t> db_index;
  // counter_index is the index in a per-NIC guest-allocated counter array of a queue's event
  // counter.  counter_index is in network byte order.
  BigEndian<uint32_t> counter_index;
  // Padding bytes to push us out to a cacheline ensuing QueueResources structs are
  // cacheline-independent of each other.
  uint8_t reserved[56];
};
static_assert(sizeof(GvnicQueueResources) == 64);

struct __PACKED GvnicAdminqCreateTxQueue {
  BigEndian<uint32_t> queue_id;
  uint8_t reserved[4];
  BigEndian<uint64_t> queue_resources_addr;
  BigEndian<uint64_t> tx_ring_addr;
  BigEndian<uint32_t> queue_page_list_id;
  BigEndian<uint32_t> ntfy_id;
  BigEndian<uint64_t> tx_comp_ring_addr;
  BigEndian<uint16_t> tx_ring_size;
};
static_assert(sizeof(GvnicAdminqCreateTxQueue) == 42);

struct __PACKED GvnicAdminqCreateRxQueue {
  BigEndian<uint32_t> queue_id;
  BigEndian<uint32_t> slice;
  uint8_t reserved[4];
  BigEndian<uint32_t> ntfy_id;
  BigEndian<uint64_t> queue_resources_addr;
  BigEndian<uint64_t> rx_desc_ring_addr;
  BigEndian<uint64_t> rx_data_ring_addr;
  BigEndian<uint32_t> queue_page_list_id;
  BigEndian<uint16_t> rx_ring_size;
  BigEndian<uint16_t> packet_buffer_size;
};
static_assert(sizeof(GvnicAdminqCreateRxQueue) == 48);

struct __PACKED GvnicAdminqDestroyTransmitQueue {
  BigEndian<uint32_t> queue_id;
};
static_assert(sizeof(GvnicAdminqDestroyTransmitQueue) == 4);

struct __PACKED GvnicAdminqDestroyReceiveQueue {
  BigEndian<uint32_t> queue_id;
};
static_assert(sizeof(GvnicAdminqDestroyReceiveQueue) == 4);

struct __PACKED GvnicAdminqDeconfigureDeviceResources {};
// No size assert here, since structs always have a size of at least 1 byte

struct __PACKED GvnicAdminqSetDriverParameter {
  BigEndian<uint32_t> parameter_type;
  uint8_t padding[4];
  BigEndian<uint64_t> parameter_value;
};

struct __PACKED GvnicAdminqReportStats {
  BigEndian<uint64_t> stats_report_len;
  BigEndian<uint64_t> stats_report_addr;
  BigEndian<uint64_t> interval_ms;
};

struct __PACKED GvnicAdminqReportLinkSpeed {
  BigEndian<uint64_t> link_speed_address;
};

struct __PACKED GvnicAdminqLinkSpeed {
  BigEndian<uint64_t> link_speed;
};

struct __PACKED GvnicAdminqGetPtypeMap {
  BigEndian<uint64_t> ptype_map_len;
  BigEndian<uint64_t> ptype_map_addr;
};

struct __PACKED GvnicAdminqEntry {
  BigEndian<uint32_t> opcode;
  BigEndian<uint32_t> status;
  union {
    GvnicAdminqDescribeDevice describe_device;
    GvnicAdminqConfigureDeviceResources device_resources;
    GvnicAdminqRegisterPageList register_page_list;
    GvnicAdminqUnregisterPageList unregister_page_list;
    GvnicAdminqCreateTxQueue create_tx_queue;
    GvnicAdminqCreateRxQueue create_rx_queue;
    GvnicAdminqDestroyTransmitQueue destroy_transmit_queue;
    GvnicAdminqDestroyReceiveQueue destroy_receive_queue;
    GvnicAdminqDeconfigureDeviceResources deconfigure_device_resources;
    GvnicAdminqSetDriverParameter set_driver_param;
    GvnicAdminqReportStats report_stats;
    GvnicAdminqReportLinkSpeed report_link_speed;
    GvnicAdminqGetPtypeMap get_ptype_map;

    char padding[56];
  };
};
static_assert(sizeof(GvnicAdminqEntry) == 64);

enum GvnicAdminqStatus : uint32_t {
  GVNIC_ADMINQ_STATUS_UNSET = 0,
  GVNIC_ADMINQ_STATUS_PASSED = 1,
  GVNIC_ADMINQ_STATUS_ABORTED = 0xFFFFFFF0,
  GVNIC_ADMINQ_STATUS_ALREADY_EXISTS = 0xFFFFFFF1,
  GVNIC_ADMINQ_STATUS_CANCELLED = 0xFFFFFFF2,
  GVNIC_ADMINQ_STATUS_DATA_LOSS = 0xFFFFFFF3,
  GVNIC_ADMINQ_STATUS_DEADLINE_EXCEEDED = 0xFFFFFFF4,
  GVNIC_ADMINQ_STATUS_FAILED_PRECONDITION = 0xFFFFFFF5,
  GVNIC_ADMINQ_STATUS_INTERNAL_ERROR = 0xFFFFFFF6,
  GVNIC_ADMINQ_STATUS_INVALID_ARGUMENT = 0xFFFFFFF7,
  GVNIC_ADMINQ_STATUS_NOT_FOUND = 0xFFFFFFF8,
  GVNIC_ADMINQ_STATUS_OUT_OF_RANGE = 0xFFFFFFF9,
  GVNIC_ADMINQ_STATUS_PERMISSION_DENIED = 0xFFFFFFFA,
  GVNIC_ADMINQ_STATUS_UNAUTHENTICATED = 0xFFFFFFFB,
  GVNIC_ADMINQ_STATUS_RESOURCES_EXHAUSTED = 0xFFFFFFFC,
  GVNIC_ADMINQ_STATUS_UNAVAILBLE = 0xFFFFFFFD,
  GVNIC_ADMINQ_STATUS_UNIMPLEMENTED = 0xFFFFFFFE,
  GVNIC_ADMINQ_STATUS_UNKNOWN_ERROR = 0xFFFFFFFF,
};

//
// Transmit/receive descriptors
//

// Transmit Descriptor Types
#define GVNIC_TXD_STD (0x0u << 4)   // Sandard
#define GVNIC_TXD_TSO (0x1u << 4)   // TSO Packet
#define GVNIC_TXD_SEG (0x2u << 4)   // Segment
#define GVNIC_TXD_META (0x3u << 4)  // Metadata

#define GVNIC_TXF_L4CSUM (0x1u << (0))  // Need csum offload
#define GVNIC_TXF_TSTAMP (0x1u << (2))  // Timestamp required

struct __PACKED GvnicTxPktDesc {
  uint8_t type_flags;
  uint8_t checksum_offset;  // In shorts (not bytes)
  uint8_t l4_offset;        // In shorts (not bytes)
  uint8_t descriptor_count;
  BigEndian<uint16_t> len;       // In bytes
  BigEndian<uint16_t> seg_len;   // In bytes
  BigEndian<uint64_t> seg_addr;  // Must be even
};
static_assert(sizeof(GvnicTxPktDesc) == 16);

struct __PACKED GvnicTxSegDesc {
  uint8_t type_flags;
  uint8_t l3_offset;  // In shorts (not bytes)
  BigEndian<uint16_t> reserved;
  BigEndian<uint16_t> mss;
  BigEndian<uint16_t> seg_len;   // In bytes
  BigEndian<uint64_t> seg_addr;  // Must be even
};
static_assert(sizeof(GvnicTxSegDesc) == 16);

union gvnic_tx_desc {
  GvnicTxPktDesc pkt;
  GvnicTxSegDesc seg;
};

#define GVNIC_RX_FLAG_FRAG (0x1u << (6))
#define GVNIC_RX_FLAG_IPV4 (0x1u << (7))
#define GVNIC_RX_FLAG_IPV6 (0x1u << (8))
#define GVNIC_RX_FLAG_TCP (0x1u << (9))
#define GVNIC_RX_FLAG_UDP (0x1u << (10))
#define GVNIC_RX_FLAG_ERR (0x1u << (11))  // Packet Error Detected
#define GVNIC_RX_FLAG_CONT (0x1u << (13))
#define GVNIC_RX_FLAG_SEQ 0x7u

// The rx data is padded with extra bytes at the start.
#define GVNIC_RX_PADDING 2

struct __PACKED GvnicRxDesc {
  uint8_t padding[48];
  BigEndian<uint32_t> rss_hash;   // Can be ignored.
  BigEndian<uint16_t> gro_mss;    // Unused.
  BigEndian<uint16_t> reserved;   // Reserved.
  uint8_t header_length;          // Unused.
  uint8_t packet_data_offset;     // Unused.
  BigEndian<uint16_t> checksum;   // Can be ignored.
  BigEndian<uint16_t> length;     // Length of the packet in bytes.  (Including 2 byte padding)
  BigEndian<uint16_t> flags_seq;  // Useful for GVNIC_RX_FLAG_ERR only.
};
static_assert(sizeof(GvnicRxDesc) == 64);

#endif  // SRC_CONNECTIVITY_ETHERNET_DRIVERS_GVNIC_ABI_H_
