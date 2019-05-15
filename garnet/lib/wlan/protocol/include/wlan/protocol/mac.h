// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_WLAN_PROTOCOL_INCLUDE_WLAN_PROTOCOL_MAC_H_
#define GARNET_LIB_WLAN_PROTOCOL_INCLUDE_WLAN_PROTOCOL_MAC_H_

#include <ddk/protocol/ethernet.h>
#include <wlan/protocol/info.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

typedef struct wlanmac_info {
    // Interface capabilities information
    wlan_info_t ifc_info;
} wlanmac_info_t;

enum {
    // The FCS for the received frame was invalid.
    WLAN_RX_INFO_FLAGS_FCS_INVALID = (1 << 0),
    // Padding was added after the MAC header to align the frame body to 4 bytes.
    WLAN_RX_INFO_FLAGS_FRAME_BODY_PADDING_4 = (1 << 1),
    // Bits 2-31 reserved
};

// LINT.IfChange
typedef int8_t wlan_dBm_t;
typedef int16_t wlan_dBmh_t;
typedef int8_t wlan_dB_t;
typedef int16_t wlan_dBh_t;

#define WLAN_RSSI_DBM_MIN (-97)
#define WLAN_RSSI_DBM_MAX (-10)
#define WLAN_RCPI_DBMH_MIN (-97 * 2)
#define WLAN_RCPI_DBMH_MAX (-10 * 2)
#define WLAN_RSNI_DBH_MIN (1)
#define WLAN_RSNI_DBH_MAX (60 * 2)

#define WLAN_RSSI_DBM_INVALID (0)
#define WLAN_RCPI_DBMH_INVALID (0)
#define WLAN_RSNI_DBH_INVALID (0)
// LINT.ThenChange(//src/connectivity/wlan/lib/common/cpp/include/wlan/common/energy.h)

typedef struct wlan_rx_info {
    // Receive flags. These represent boolean flags as opposed to enums or value-based info which
    // are represented below. Values should be taken from the WLAN_RX_INFO_FLAGS_* enum.
    uint32_t rx_flags;

    // Bitmask indicating which of the following fields are valid in this struct. Reserved flags
    // must be zero.
    uint32_t valid_fields;
    // The PHY format of the device at the time of the operation.
    uint16_t phy;
    // The data rate of the device, measured in units of 0.5 Mb/s.
    uint32_t data_rate;
    // The channel of the device at the time of the operation. This field must be included.
    wlan_channel_t chan;
    // The modulation and coding scheme index of the device at the time of the operation. Depends
    // on the PHY format and channel width.
    uint8_t mcs;

    // Received Signal Strength Indicator.
    wlan_dBm_t rssi_dbm;
    // Received Channel Power Indicator, in 0.5 dBm. IEEE Std 802.11-2016, 17.3.10.7.
    // Do not use encoding in 15.4.6.6
    wlan_dBmh_t rcpi_dbmh;
    // Signal-to-Noise Ratio, in 0.5 dB.
    wlan_dBh_t snr_dbh;
} wlan_rx_info_t;

enum {
    WLAN_TX_INFO_FLAGS_PROTECTED = (1 << 0),
    // For rate control: indicate an important data frame, such as EAPOL, which should be sent
    // _reliably_ rather than fast, and is exempt from rate probing
    WLAN_TX_INFO_FLAGS_FAVOR_RELIABILITY = (1 << 1),
    // Indicate that this packet should be sent out with QoS header when possible (11n+).
    // TODO(WLAN-1002): remove this when MLME supports QoS tag.
    WLAN_TX_INFO_FLAGS_QOS = (1 << 2),
};

enum {
    WLAN_TX_INFO_VALID_DATA_RATE = (1 << 0),
    WLAN_TX_INFO_VALID_TX_VECTOR_IDX = (1 << 1),
    WLAN_TX_INFO_VALID_PHY = (1 << 2),
    WLAN_TX_INFO_VALID_CHAN_WIDTH = (1 << 3),
    WLAN_TX_INFO_VALID_MCS = (1 << 4),
    // Bits 5-31 reserved
};

// TxVector is defined in //src/connectivity/wlan/lib/common/cpp/tx_vector.h
typedef uint16_t tx_vec_idx_t;

typedef struct wlan_tx_info {
    // Transmit flags. These represent boolean options as opposed to enums or other value-based
    // info which are represented below. Values should be taken from the WLAN_TX_INFO_FLAGS_* enum.
    uint32_t tx_flags;

    // Bitmask indicating which of the following fields are valid in this struct. Reserved flags
    // must be zero. Values for fields not indicated by a flag may be chosen at the discretion of
    // the wlanmac driver.
    uint32_t valid_fields;
    // Will be sent back in wlan_tx_status_t if Minstrel is enabled for the device, indicated by
    // WLAN_TX_INFO_VALID_TX_VECTOR_IDX.
    tx_vec_idx_t tx_vector_idx;
    // The PHY format to be used to transmit this packet.
    uint16_t phy;
    // The channel width to be used to transmit this packet.
    uint8_t cbw;
    // The modulation and coding scheme index for this packet. Depends on the PHY format and
    // channel width.
    uint8_t mcs;
} wlan_tx_info_t;

#define WLAN_TX_VECTOR_IDX_INVALID 0
#define WLAN_TX_STATUS_MAX_ENTRY 8

typedef struct wlan_tx_status_entry {
    tx_vec_idx_t tx_vector_idx;
    // Number of total attempts with this specific tx vector, including successful attempts.
    // DDK assumes the number of attempts per packet will not exceed 255. (usually <= 8)
    uint8_t attempts;
} __PACKED wlan_tx_status_entry_t;

typedef struct wlan_tx_status {
    // up to 8 different tx_vector for one PPDU frame.
    // WLAN_TX_VECTOR_IDX_INVALID indicates no more entries.
    wlan_tx_status_entry_t tx_status_entry[WLAN_TX_STATUS_MAX_ENTRY];
    // Destination mac address, or addr1 in packet header.
    uint8_t peer_addr[6];
    // Outcome of packet transmission. True iff ACK was received from peer.
    bool success;
} __PACKED wlan_tx_status_t;

enum {
    WLAN_PROTECTION_NONE = 0,
    WLAN_PROTECTION_RX = 1,
    WLAN_PROTECTION_TX = 2,
    WLAN_PROTECTION_RX_TX = 3,
};

enum {
    WLAN_KEY_TYPE_PAIRWISE = 1,
    WLAN_KEY_TYPE_GROUP = 2,
    WLAN_KEY_TYPE_IGTK = 3,
    WLAN_KEY_TYPE_PEER = 4,
};

typedef struct wlan_key_config {
    // The BSSID for which this key is relevant.
    uint8_t bssid;
    // Which path to protect: None, TX, RX, or TX and RX.
    // See WLAN_PROTECTION_*.
    uint8_t protection;
    // IEEE Cipher suite selector.
    // See IEEE Std 802.11-2016, 9.4.2.25.2, Table 9-131
    uint8_t cipher_oui[3];
    uint8_t cipher_type;
    // Whether this key is a pairwise, group or peer key.
    // See WLAN_KEY_TYPE_*.
    uint8_t key_type;
    // The peer MAC address for pairwise and peer keys.
    // For group keys this value is always the broadcast address. 
    uint8_t peer_addr[6];
    // Index for rotating keys, e.g. group keys. 
    // This value is always 0 for key types which aren't rotating, e.g. pairwise keys.
    uint8_t key_idx;
    // Length of the supplied key.
    uint8_t key_len;
    // They key's actual bytes.
    uint8_t key[32];
    // Receive Sequence Counter for group keys only.
    // In all other cases the RSC will be 0.
    uint64_t rsc;
} wlan_key_config_t;

typedef struct wlan_tx_packet {
    // Leading bytes of the packet to transmit. Any 802.11 frame headers must be in the packet_head.
    ethmac_netbuf_t packet_head;
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

enum {
    WLAN_INDICATION_PRE_TBTT = 1,
    WLAN_INDICATION_BCN_TX_COMPLETE = 2,
    WLAN_INDICATION_HW_SCAN_COMPLETE = 3,
    WLAN_INDICATION_HW_SCAN_ABORTED = 4,
};

enum {
    WLAN_HW_SCAN_TYPE_ACTIVE = 1,
    WLAN_HW_SCAN_TYPE_PASSIVE = 2,
};

typedef struct wlan_hw_scan_config {
    uint8_t scan_type;  // WLAN_HW_SCAN_TYPE_*
    // Number of channels in the |channels| array. Must be at least 1
    uint8_t num_channels;
    // Channel numbers to scan
    uint8_t channels[WLAN_CHANNELS_MAX_LEN];
    // SSID for directed probe requests
    wlan_ssid_t ssid;
} wlan_hw_scan_config_t;

enum {
    WLAN_HW_SCAN_SUCCESS = 0,
    WLAN_HW_SCAN_ABORTED = 1,
};

typedef struct wlan_hw_scan_result {
    // Either WLAN_HW_SCAN_SUCCESS or WLAN_HW_SCAN_ABORTED
    uint8_t code;
} wlan_hw_scan_result_t;

// Includes the information about beacon template.
typedef struct wlan_bcn_config {
    // Points to the beacon template. Since this is just the template, some packet content can
    // contain only minimum valid info. They will be changed later by hardware/firmware or software.
    // Note that the driver must copy the packet content into its own memory and cannot rely on
    // the pointers in the struct.
    wlan_tx_packet_t tmpl;

    // TIM offset (in bytes) to the start of |bcn_tmpl|. This points to the first byte of TIM IE,
    // which is the tag ID.
    size_t tim_ele_offset;

    uint16_t beacon_interval;  // in TU
} wlan_bcn_config_t;

typedef struct wlanmac_ifc {
    // Report the status of the wlanmac device.
    void (*status)(void* cookie, uint32_t status);

    // Submit received data to the next driver. info must not be NULL.
    void (*recv)(void* cookie, uint32_t flags, const void* data, size_t length,
                 wlan_rx_info_t* info);

    // complete_tx() is called to return ownership of a packet to the wlan driver.
    // Return status indicates queue state:
    //   ZX_OK: Packet has been enqueued.
    //   Other: Packet could not be enqueued.
    //
    // Upon a return of ZX_OK, the packet has been enqueued, but no information is returned as to
    // the completion state of the transmission itself.
    void (*complete_tx)(void* cookie, wlan_tx_packet_t* packet, zx_status_t status);

    // Reports an indication of a status, state or action to the wlan driver.
    void (*indication)(void* cookie, uint32_t ind);

    // Reports the status of an attempted transmission.
    // * tx_status: contains status info of one transmitted packet to one peer at one specific rate.
    void (*report_tx_status)(void* cookie, const wlan_tx_status_t* tx_status);

    // Reports completion of a hardware scan
    void (*hw_scan_complete)(void* cookie, const wlan_hw_scan_result_t* result);
} wlanmac_ifc_t;

typedef struct wlanmac_protocol_ops {
    // Obtain information about the device and supported features
    // Safe to call at any time.
    zx_status_t (*query)(void* ctx, uint32_t options, wlanmac_info_t* info);

    // Start wlanmac running with ifc_virt
    // Callbacks on ifc may be invoked from now until stop() is called
    zx_status_t (*start)(void* ctx, wlanmac_ifc_t* ifc, zx_handle_t* out_sme_channel, void* cookie);

    // Shut down a running wlanmac
    // Safe to call if the wlanmac is already stopped.
    void (*stop)(void* ctx);

    // Queue the data for transmit. Return status indicates queue state:
    //   ZX_ERR_SHOULD_WAIT: Packet is being enqueued.
    //   ZX_OK: Packet has been enqueued.
    //   Other: Packet could not be enqueued.
    //
    // In the SHOULD_WAIT case the driver takes ownership of the wlan_tx_packet_t and must call
    // complete_tx() to return it once the enqueue is complete. complete_tx() may be used to return
    // the packet before transmission itself completes, and MUST NOT be called from within the
    // queue_tx() implementation.
    //
    // queue_tx() may be called at any time after start() is called including from multiple threads
    // simultaneously.
    zx_status_t (*queue_tx)(void* ctx, uint32_t options, wlan_tx_packet_t* pkt);

    // Set the radio channel
    zx_status_t (*set_channel)(void* ctx, uint32_t options, wlan_channel_t* chan);

    // Configures a BSS which the STA is either joining or managing.
    zx_status_t (*configure_bss)(void* ctx, uint32_t options, wlan_bss_config_t* config);

    // Enables or disables hardware Beaconing.
    // * bcn_cfg: Pass `nullptr` to disable hardware Beacons. Used by hardware beacon offload.
    zx_status_t (*enable_beaconing)(void* ctx, uint32_t options, wlan_bcn_config_t* bcn_cfg);

    // Configures a Beacon frame in hardware to announce the BSS' existence.
    // * pkt: Pass `nullptr` to disable hardware Beacons. Used by software generated beacon.
    // TODO(NET-1565): Rename to update_beacon.
    zx_status_t (*configure_beacon)(void* ctx, uint32_t options, wlan_tx_packet_t* pkt);

    // Specify a key for frame protection.
    zx_status_t (*set_key)(void* ctx, uint32_t options, wlan_key_config_t* key_config);

    // Notifies MAC and PHY parameters negotiated through a successful association
    zx_status_t (*configure_assoc)(void* ctx, uint32_t options, wlan_assoc_ctx_t* assoc_ctx);

    // Notifies MAC and PHY that the peer has been de-associated.
    zx_status_t (*clear_assoc)(void* ctx, uint32_t options, const uint8_t* peer_addr);

    // Initiate a hardware scan
    //
    // Once the scan starts, scan results will be delivered as beacon and probe response frames
    // via the regular rx path.
    //
    // Unless an error is returned immediately, the driver will eventually
    // call wlanmac_ifc->hw_scan_complete()
    zx_status_t (*start_hw_scan)(void* ctx, const wlan_hw_scan_config_t* scan_config);
} wlanmac_protocol_ops_t;

typedef struct wlanmac_protocol {
    wlanmac_protocol_ops_t* ops;
    void* ctx;
} wlanmac_protocol_t;

__END_CDECLS

#endif  // GARNET_LIB_WLAN_PROTOCOL_INCLUDE_WLAN_PROTOCOL_MAC_H_
