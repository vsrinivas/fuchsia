// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/ethernet.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

typedef struct wlan_channel {
    uint16_t channel_num;
    // etc
} wlan_channel_t;

enum {
    WLAN_RX_INFO_VALID_PHY = (1 << 0),
    WLAN_RX_INFO_VALID_DATA_RATE = (1 << 1),
    WLAN_RX_INFO_VALID_CHAN_WIDTH = (1 << 2),
    WLAN_RX_INFO_VALID_MCS = (1 << 3),
    WLAN_RX_INFO_VALID_RSSI = (1 << 4),
    WLAN_RX_INFO_VALID_RCPI = (1 << 5),
    WLAN_RX_INFO_VALID_SNR = (1 << 6),
    // Bits 7-31 reserved

    // Deprecated enum names
    WLAN_RX_INFO_PHY_PRESENT = (1 << 0),
    WLAN_RX_INFO_DATA_RATE_PRESENT = (1 << 1),
    WLAN_RX_INFO_CHAN_WIDTH_PRESENT = (1 << 2),
    WLAN_RX_INFO_MOD_PRESENT = (1 << 3),
    WLAN_RX_INFO_RSSI_PRESENT = (1 << 4),
    WLAN_RX_INFO_RCPI_PRESENT = (1 << 5),
    WLAN_RX_INFO_SNR_PRESENT = (1 << 6),
};

enum {
    WLAN_PHY_CCK = 1,
    WLAN_PHY_OFDM = 2,
    WLAN_PHY_HT_MIXED = 3,
    WLAN_PHY_HT_GREENFIELD = 4,
    WLAN_PHY_VHT = 5,
};

enum {
    WLAN_CHAN_WIDTH_5MHZ = 1,
    WLAN_CHAN_WIDTH_10MHZ = 2,
    WLAN_CHAN_WIDTH_20MHZ = 3,
    WLAN_CHAN_WIDTH_40MHZ = 4,
    WLAN_CHAN_WIDTH_80MHZ = 5,
    WLAN_CHAN_WIDTH_160MHZ = 6,
    WLAN_CHAN_WIDTH_80_80MHZ = 7,
};

enum {
    WLAN_BSS_TYPE_INFRASTRUCTURE = 1,
    WLAN_BSS_TYPE_IBSS = 2,
};

enum {
    WLAN_RX_INFO_FLAGS_FCS_INVALID = (1 << 0),
};

typedef struct wlan_rx_info {
    // Deprecated field; use present_flags instead.
    uint32_t flags;
    // Receive flags. These represent boolean flags as opposed to enums or value-based info which
    // are represented below. Values should be taken from the WLAN_RX_INFO_FLAGS_* enum.
    uint32_t rx_flags;

    // Bitmask indicating which of the following fields are valid in this struct. Reserved flags
    // must be zero.
    uint32_t valid_fields;
    // The PHY format of the device at the time of the operation.
    uint16_t phy;
    // The channel width of the device.
    uint16_t chan_width;
    // The data rate of the device, measured in units of 0.5 Mb/s.
    uint32_t data_rate;
    // The channel of the device at the time of the operation. This field must be included.
    wlan_channel_t chan;
    // The modulation and coding scheme index of the device at the time of the operation. Depends
    // on the PHY format and channel width.
    uint8_t mcs;
    // Deprecated field; use mcs instead.
    uint8_t mod;
    // The RSSI measured by the device. No units.
    uint8_t rssi;
    // The RCPI (IEEE Std 802.11-2016, 17.3.10.7) measured by the device.
    uint8_t rcpi;
    // The SNR measured by the device, in 0.5 dBm
    uint8_t snr;
} wlan_rx_info_t;

enum {
    WLAN_TX_INFO_FLAGS_PROTECTED = (1 << 0),
};

enum {
    WLAN_TX_INFO_VALID_PHY = (1 << 0),
    WLAN_TX_INFO_VALID_DATA_RATE = (1 << 1),
    WLAN_TX_INFO_VALID_CHAN_WIDTH = (1 << 2),
    WLAN_TX_INFO_VALID_MCS = (1 << 3),
    // Bits 4-31 reserved
};

typedef struct wlan_tx_info {
    // Transmit flags. These represent boolean options as opposed to enums or other value-based
    // info which are represented below. Values should be taken from the WLAN_TX_INFO_FLAGS_* enum.
    uint32_t tx_flags;

    // Bitmask indicating which of the following fields are valid in this struct. Reserved flags
    // must be zero. Values for fields not indicated by a flag may be chosen at the discretion of
    // the wlanmac driver.
    uint32_t valid_fields;
    // The PHY format to be used to transmit this packet.
    uint16_t phy;
    // The channel width to be used to transmit this packet.
    uint16_t chan_width;
    // The data rate to be used to transmit this packet, measured in units of 0.5 Mb/s.
    uint32_t data_rate;
    // The modulation and coding scheme index for this packet. Depends on the PHY format and
    // channel width.
    uint8_t mcs;
} wlan_tx_info_t;

// TODO(hahnr): use explicit enum values
enum {
    WLAN_PROTECTION_NONE,
    WLAN_PROTECTION_RX,
    WLAN_PROTECTION_TX,
    WLAN_PROTECTION_RX_TX,
};

// TODO(hahnr): use explicit enum values
enum {
    WLAN_KEY_TYPE_PAIRWISE,
    WLAN_KEY_TYPE_GROUP,
    WLAN_KEY_TYPE_IGTK,
    WLAN_KEY_TYPE_PEER,
};

typedef struct wlan_key_config {
    uint8_t protection;
    uint8_t cipher_oui[3];
    uint8_t cipher_type;
    uint8_t peer_addr[6];
    uint8_t key_type;
    uint8_t key_len;
    uint8_t key_idx;
    uint8_t key[32];
} wlan_key_config_t;

typedef struct wlan_tx_packet {
    // Leading bytes of the packet to transmit. Any 802.11 frame headers must be in the packet_head.
    ethmac_netbuf_t* packet_head;
    // Trailing bytes of the packet to transmit. May be NULL if all bytes to be transmitted are in
    // the packet_head. Typically used to transport ethernet frames from a higher layer.
    ethmac_netbuf_t* packet_tail;
    // If packet_tail is not NULL, the offset into the packet tail that should be used before
    // transmitting. The ethmac_netbuf_t len field will reflect the original packet length without
    // the offset.
    uint16_t tail_offset;
    // Additional data needed to transmit the packet.
    wlan_tx_info_t info;
} wlan_tx_packet_t;

typedef struct wlanmac_ifc {
    // Report the status of the wlanmac device.
    void (*status)(void* cookie, uint32_t status);

    // Submit received data to the next driver. info must not be NULL.
    void (*recv)(void* cookie, uint32_t flags, const void* data, size_t length,
                 wlan_rx_info_t* info);

    // Complete the tx to return the ownership of the packet buffers to the wlan driver.
    void (*complete_tx)(void* cookie, wlan_tx_packet_t* packet, zx_status_t status);
} wlanmac_ifc_t;

typedef struct wlanmac_protocol_ops {
    // Obtain information about the device and supported features
    // Safe to call at any time.
    // TODO: create wlanmac_info_t for wlan-specific info and copy the relevant
    // ethernet fields into ethmac_info_t before passing up the stack
    zx_status_t (*query)(void* ctx, uint32_t options, ethmac_info_t* info);

    // Start wlanmac running with ifc_virt
    // Callbacks on ifc may be invoked from now until stop() is called
    zx_status_t (*start)(void* ctx, wlanmac_ifc_t* ifc, void* cookie);

    // Shut down a running wlanmac
    // Safe to call if the wlanmac is already stopped.
    void (*stop)(void* ctx);

    // Queue the data for transmit (deprecated)
    void (*tx)(void* ctx, uint32_t options, const void* data, size_t length);

    // Queue the data for transmit. Return status indicates disposition:
    //   ZX_ERR_SHOULD_WAIT: Packet is being transmitted
    //   ZX_OK: Packet has been transmitted
    //   Other: Packet could not be transmitted
    //
    // In the SHOULD_WAIT case the driver takes ownership of the wlan_tx_packet_t and must call
    // complete_tx() to return it once the transmission is complete. complete_tx() MUST NOT be
    // called from within the queue_tx() implementation.
    //
    // queue_tx() may be called at any time after start() is called including from multiple threads
    // simultaneously.
    zx_status_t (*queue_tx)(void* ctx, uint32_t options, wlan_tx_packet_t* pkt);

    // Set the radio channel
    zx_status_t (*set_channel)(void* ctx, uint32_t options, wlan_channel_t* chan);

    // Sets the BSS the station is joining
    zx_status_t (*set_bss)(void* ctx, uint32_t options, const uint8_t mac[6], uint8_t type);

    // Specify a key for frame protection. Callee must free allocated key_config.
    zx_status_t (*set_key)(void* ctx, uint32_t options, wlan_key_config_t* key_config);
} wlanmac_protocol_ops_t;

typedef struct wlanmac_protocol {
    wlanmac_protocol_ops_t* ops;
    void* ctx;
} wlanmac_protocol_t;

__END_CDECLS;
