// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <ddk/protocol/ethernet.h>

// clang-format off

#define VIRTIO_NET_F_CSUM                   (1u << 0)
#define VIRTIO_NET_F_GUEST_CSUM             (1u << 1)
#define VIRTIO_NET_F_CNTRL_GUEST_OFFLOADS   (1u << 2)
#define VIRTIO_NET_F_MAC                    (1u << 5)
#define VIRTIO_NET_F_GSO                    (1u << 6)
#define VIRTIO_NET_F_GUEST_TSO4             (1u << 7)
#define VIRTIO_NET_F_GUEST_TSO6             (1u << 8)
#define VIRTIO_NET_F_GUEST_ECN              (1u << 9)
#define VIRTIO_NET_F_GUEST_UFO              (1u << 10)
#define VIRTIO_NET_F_HOST_TSO4              (1u << 11)
#define VIRTIO_NET_F_HOST_TSO6              (1u << 12)
#define VIRTIO_NET_F_HOST_ECN               (1u << 13)
#define VIRTIO_NET_F_HOST_UFO               (1u << 14)
#define VIRTIO_NET_F_MRG_RXBUF              (1u << 15)
#define VIRTIO_NET_F_STATUS                 (1u << 16)
#define VIRTIO_NET_F_CTRL_VQ                (1u << 17)
#define VIRTIO_NET_F_CTRL_RX                (1u << 18)
#define VIRTIO_NET_F_CTRL_VLAN              (1u << 19)
#define VIRTIO_NET_F_GUEST_ANNOUNCE         (1u << 21)
#define VIRTIO_NET_F_MQ                     (1u << 22)
#define VIRTIO_NET_F_CTRL_MAC_ADDR          (1u << 23)

#define VIRTIO_NET_HDR_F_NEEDS_CSUM 1u

#define VIRTIO_NET_HDR_GSO_NONE     0u
#define VIRTIO_NET_HDR_GSO_TCPV4    1u
#define VIRTIO_NET_HDR_GSO_UDP      3u
#define VIRTIO_NET_HDR_GSO_TCPV6    4u
#define VIRTIO_NET_HDR_GSO_ECN      0x80u

#define VIRTIO_NET_S_LINK_UP        1u
#define VIRTIO_NET_S_ANNOUNCE       2u

// clang-format on

__BEGIN_CDECLS

typedef struct virtio_net_config {
    uint8_t mac[ETH_MAC_SIZE];
    uint16_t status;
    uint16_t max_virtqueue_pairs;
} __PACKED virtio_net_config_t;

typedef struct virtio_net_hdr {
    uint8_t flags;
    uint8_t gso_type;
    uint16_t hdr_len;
    uint16_t gso_size;
    uint16_t csum_start;
    uint16_t csum_offset;
} __PACKED virtio_net_hdr_t;

__END_CDECLS
