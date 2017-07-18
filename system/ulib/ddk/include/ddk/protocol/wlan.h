// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/ethernet.h>
#include <magenta/compiler.h>
#include <magenta/types.h>

__BEGIN_CDECLS;

typedef struct wlan_channel {
    uint16_t channel_num;
    // etc
} wlan_channel_t;

enum {
    WLAN_RX_INFO_PHY_PRESENT = (1 << 0),
    WLAN_RX_INFO_DATA_RATE_PRESENT = (1 << 1),
    WLAN_RX_INFO_CHAN_WIDTH_PRESENT = (1 << 2),
    WLAN_RX_INFO_MOD_PRESENT = (1 << 3),
    WLAN_RX_INFO_RSSI_PRESENT = (1 << 4),
    WLAN_RX_INFO_RCPI_PRESENT = (1 << 5),
    WLAN_RX_INFO_SNR_PRESENT = (1 << 6),
    // Bits 7-31 reserved
};

enum {
    WLAN_PHY_CCK,
    WLAN_PHY_OFDM,
    WLAN_PHY_HT_MIXED,
    WLAN_PHY_HT_GREENFIELD,
    WLAN_PHY_VHT,
};

enum {
    WLAN_CHAN_WIDTH_5MHZ,
    WLAN_CHAN_WIDTH_10MHZ,
    WLAN_CHAN_WIDTH_20MHZ,
    WLAN_CHAN_WIDTH_40MHZ,
    WLAN_CHAN_WIDTH_80MHZ,
    WLAN_CHAN_WIDTH_160MHZ,
    WLAN_CHAN_WIDTH_80_80MHZ,
};

typedef struct wlan_rx_info {
    // Flags indicating which fields are valid in this struct. Reserved flags must be zero.
    uint32_t flags;
    // The PHY format of the device at the time of the operation.
    uint16_t phy;
    // The channel width of the device.
    uint16_t chan_width;
    // The data rate of the device, measured in units of 0.5 Mb/s.
    uint32_t data_rate;
    // The channel of the device at the time of the operation. This field must be included.
    wlan_channel_t chan;
    // The modulation index of the device at the time of the operation. Depends
    // on the PHY format and channel width.
    uint8_t mod;
    // The RSSI measured by the device. No units.
    uint8_t rssi;
    // The RCPI (IEEE Std 802.11-2016, 17.3.10.7) measured by the device.
    uint8_t rcpi;
    // The SNR measured by the device, in 0.5 dBm
    uint8_t snr;
} wlan_rx_info_t;

enum {
    WLAN_RX_FLAGS_FCS_INVALID = (1 << 0),
};

typedef struct wlanmac_ifc {
    // Report the status of the wlanmac device.
    void (*status)(void* cookie, uint32_t status);

    // Submit received data to the next driver. info must not be NULL.
    void (*recv)(void* cookie, uint32_t flags, const void* data, size_t length,
                 wlan_rx_info_t* info);
} wlanmac_ifc_t;

typedef struct wlanmac_protocol_ops {
    // Obtain information about the device and supported features
    // Safe to call at any time.
    // TODO: create wlanmac_info_t for wlan-specific info and copy the relevant
    // ethernet fields into ethmac_info_t before passing up the stack
    mx_status_t (*query)(void* ctx, uint32_t options, ethmac_info_t* info);

    // Start wlanmac running with ifc_virt
    // Callbacks on ifc may be invoked from now until stop() is called
    mx_status_t (*start)(void* ctx, wlanmac_ifc_t* ifc, void* cookie);

    // Shut down a running wlanmac
    // Safe to call if the wlanmac is already stopped.
    void (*stop)(void* ctx);

    // Queue the data for transmit
    void (*tx)(void* ctx, uint32_t options, const void* data, size_t length);

    // Set the radio channel
    mx_status_t (*set_channel)(void* ctx, uint32_t options, wlan_channel_t* chan);
} wlanmac_protocol_ops_t;

typedef struct wlanmac_protocol {
    wlanmac_protocol_ops_t* ops;
    void* ctx;
} wlanmac_protocol_t;

__END_CDECLS;
