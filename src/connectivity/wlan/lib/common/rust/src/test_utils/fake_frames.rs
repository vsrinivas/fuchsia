// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::mac::*;

pub const EAPOL_PDU: &[u8] = &[5, 5, 5, 5, 5, 5, 5, 5];

pub fn make_mgmt_frame(ht_ctrl: bool) -> Vec<u8> {
    #[rustfmt::skip]
        let mut bytes = vec![
        1, if ht_ctrl { 128 } else { 1 }, // fc
        2, 2, // duration
        3, 3, 3, 3, 3, 3, // addr1
        4, 4, 4, 4, 4, 4, // addr2
        5, 5, 5, 5, 5, 5, // addr3
        6, 6, // sequence control
    ];
    if ht_ctrl {
        bytes.extend_from_slice(&[8, 8, 8, 8]);
    }
    bytes.extend_from_slice(&[9, 9, 9]);
    bytes
}

pub fn make_data_hdr(
    addr4: Option<[u8; 6]>,
    qos_ctrl: [u8; 2],
    ht_ctrl: Option<[u8; 4]>,
) -> Vec<u8> {
    let mut fc = FrameControl(0);
    fc.set_frame_type(FrameType::DATA);
    fc.set_data_subtype(DataSubtype(0).with_qos(true));
    fc.set_from_ds(addr4.is_some());
    fc.set_to_ds(addr4.is_some());
    fc.set_htc_order(ht_ctrl.is_some());
    let fc = fc.0.to_le_bytes();

    #[rustfmt::skip]
        let mut bytes = vec![
        // Data Header
        fc[0], fc[1], // fc
        2, 2, // duration
        3, 3, 3, 3, 3, 3, // addr1
        4, 4, 4, 4, 4, 4, // addr2
        5, 5, 5, 5, 5, 5, // addr3
        6, 6, // sequence control
    ];

    if let Some(addr4) = addr4 {
        bytes.extend_from_slice(&addr4);
    }

    bytes.extend_from_slice(&qos_ctrl);

    if let Some(ht_ctrl) = ht_ctrl {
        bytes.extend_from_slice(&ht_ctrl);
    }
    bytes
}

pub fn make_data_frame_single_llc(addr4: Option<[u8; 6]>, ht_ctrl: Option<[u8; 4]>) -> Vec<u8> {
    let qos_ctrl = [1, 1];
    let mut bytes = make_data_hdr(addr4, qos_ctrl, ht_ctrl);
    #[rustfmt::skip]
        bytes.extend_from_slice(&[
        // LLC Header
        7, 7, 7, // DSAP, SSAP & control
        8, 8, 8, // OUI
        9, 10, // eth type
        // Trailing bytes
        11, 11, 11,
    ]);
    bytes
}

pub fn make_null_data_frame() -> Vec<u8> {
    let fc = FrameControl(0)
        .with_frame_type(FrameType::DATA)
        .with_data_subtype(DataSubtype(0).with_null(true))
        .with_to_ds(true);
    let fc = fc.0.to_le_bytes();

    #[rustfmt::skip]
    let bytes = vec![
        fc[0], fc[1], // FC
        2, 2, // duration
        3, 3, 3, 3, 3, 3, // addr1
        4, 4, 4, 4, 4, 4, // addr2
        5, 5, 5, 5, 5, 5, // addr3
        6, 6, // sequence control
    ];
    bytes
}

pub fn make_data_frame_with_padding() -> Vec<u8> {
    let mut bytes = make_data_hdr(None, [1, 1], None);
    #[rustfmt::skip]
        bytes.extend(vec![
        // Padding
        2, 2,
        // LLC Header
        7, 7, 7, // DSAP, SSAP & control
        8, 8, 8, // OUI
        9, 10, //eth type
        11, 11, 11, 11, 11, // payload
    ]);
    bytes
}

#[rustfmt::skip]
pub const MSDU_1_LLC_HDR : &[u8] = &[
    0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00, 0x08, 0x00,
];

#[rustfmt::skip]
pub const MSDU_1_PAYLOAD : &[u8] = &[
    0x33, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x01, 0x02, 0x03, 0x04,
];

#[rustfmt::skip]
pub const MSDU_2_LLC_HDR : &[u8] = &[
    0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00, 0x08, 0x01,
];

#[rustfmt::skip]
pub const MSDU_2_PAYLOAD : &[u8] = &[
    // Payload
    0x99, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
];

pub fn make_data_frame_amsdu() -> Vec<u8> {
    let mut qos_ctrl = QosControl(0);
    qos_ctrl.set_amsdu_present(true);
    let mut amsdu_data_frame = make_data_hdr(None, qos_ctrl.0.to_le_bytes(), None);
    #[rustfmt::skip]
    amsdu_data_frame.extend(&[
        // A-MSDU Subframe #1
        0x78, 0x8a, 0x20, 0x0d, 0x67, 0x03, // dst_addr
        0xb4, 0xf7, 0xa1, 0xbe, 0xb9, 0xab, // src_addr
        0x00, 0x74, // MSDU length
    ]);
    amsdu_data_frame.extend(MSDU_1_LLC_HDR);
    amsdu_data_frame.extend(MSDU_1_PAYLOAD);

    #[rustfmt::skip]
    amsdu_data_frame.extend(&[
        // Padding
        0x00, 0x00,
        // A-MSDU Subframe #2
        0x78, 0x8a, 0x20, 0x0d, 0x67, 0x04, // dst_addr
        0xb4, 0xf7, 0xa1, 0xbe, 0xb9, 0xac, // src_addr
        0x00, 0x66, // MSDU length
    ]);
    amsdu_data_frame.extend(MSDU_2_LLC_HDR);
    amsdu_data_frame.extend(MSDU_2_PAYLOAD);
    amsdu_data_frame
}

pub fn make_eapol_frame(addr1: MacAddr) -> (MacAddr, MacAddr, Vec<u8>) {
    #[rustfmt::skip]
    let mut frame = vec![
        // Data header:
        0b0000_10_00, 0b000000_1_0, // FC
        0, 0, // Duration
        6, 6, 6, 6, 6, 6, // addr1
        7, 7, 7, 7, 7, 7, // addr2
        7, 7, 7, 7, 7, 7, // addr3
        0x10, 0, // Sequence Control
        // LLC header:
        0xaa, 0xaa, 0x03, // dsap ssap ctrl
        0x00, 0x00, 0x00, // oui
        0x88, 0x8E, // protocol id (EAPOL)
    ];
    // overwrite addr1
    frame[4..10].copy_from_slice(&addr1);
    // EAPOL frame:
    frame.extend(EAPOL_PDU);

    // (src, dst, data frame)
    ([7; 6], addr1, frame)
}

pub fn make_data_frame_amsdu_padding_too_short() -> Vec<u8> {
    let mut qos_ctrl = QosControl(0);
    qos_ctrl.set_amsdu_present(true);
    let mut amsdu_data_frame = make_data_hdr(None, qos_ctrl.0.to_le_bytes(), None);
    #[rustfmt::skip]
        amsdu_data_frame.extend(&[
        // A-MSDU Subframe #1
        0x78, 0x8a, 0x20, 0x0d, 0x67, 0x03, // dst_addr
        0xb4, 0xf7, 0xa1, 0xbe, 0xb9, 0xab, // src_addr
        0x00, 0x74, // MSDU length
    ]);
    amsdu_data_frame.extend(MSDU_1_LLC_HDR);
    amsdu_data_frame.extend(MSDU_1_PAYLOAD);

    #[rustfmt::skip]
    amsdu_data_frame.extend(&[
        // Padding is shorter than needed (1 vs 2)
        0x00,
        // A-MSDU Subframe #2
        0x78, 0x8a, 0x20, 0x0d, 0x67, 0x04, // dst_addr
        0xb4, 0xf7, 0xa1, 0xbe, 0xb9, 0xac, // src_addr
        0x00, 0x66, // MSDU length
    ]);
    amsdu_data_frame.extend(MSDU_2_LLC_HDR);
    amsdu_data_frame.extend(MSDU_2_PAYLOAD);
    amsdu_data_frame
}

pub fn fake_wpa1_ie_body(enhanced: bool) -> Vec<u8> {
    let cipher = if enhanced { 0x4 } else { 0x2 }; // unicast cipher is TKIP or CCMP-128
    vec![
        0x01, 0x00, // WPA version
        0x00, 0x50, 0xf2, 0x02, // multicast cipher: TKIP
        0x01, 0x00, 0x00, 0x50, 0xf2, cipher, // 1 unicast cipher
        0x01, 0x00, 0x00, 0x50, 0xf2, 0x02, // 1 AKM: PSK
    ]
}

pub fn fake_wpa1_ie(enhanced: bool) -> Vec<u8> {
    let mut ie = vec![
        0xdd, 0x16, 0x00, 0x50, 0xf2, // IE header
        0x01, // MSFT specific IE type (WPA)
    ];
    ie.append(&mut fake_wpa1_ie_body(enhanced));
    ie
}

pub fn fake_wpa2_rsne() -> Vec<u8> {
    vec![
        48, 18, // Element header
        1, 0, // Version
        0x00, 0x0F, 0xAC, 4, // Group Cipher: CCMP-128
        1, 0, 0x00, 0x0F, 0xAC, 4, // 1 Pairwise Cipher: CCMP-128
        1, 0, 0x00, 0x0F, 0xAC, 2, // 1 AKM: PSK
    ]
}

pub fn fake_wpa2_legacy_rsne() -> Vec<u8> {
    vec![
        48, 18, // Element header
        1, 0, // Version
        0x00, 0x0F, 0xAC, 2, // Group Cipher: TKIP
        1, 0, 0x00, 0x0F, 0xAC, 2, // 1 Pairwise Cipher: TKIP
        1, 0, 0x00, 0x0F, 0xAC, 2, // 1 AKM: PSK
    ]
}

pub fn fake_wpa2_mixed_rsne() -> Vec<u8> {
    vec![
        48, 18, // Element header
        1, 0, // Version
        0x00, 0x0F, 0xAC, 2, // Group Cipher: TKIP
        2, 0, 0x00, 0x0F, 0xAC, 2, 0x00, 0x0F, 0xAC, 4, // 2 Pairwise Ciphers: TKIP, CCMP-128
        1, 0, 0x00, 0x0F, 0xAC, 2, // 1 AKM: PSK
    ]
}

pub fn fake_wpa2_wpa3_rsne() -> Vec<u8> {
    vec![
        48, 18, // Element header
        1, 0, // Version
        0x00, 0x0F, 0xAC, 4, // Group Cipher: CCMP-128
        1, 0, 0x00, 0x0F, 0xAC, 4, // 1 Pairwise Cipher: CCMP-128
        2, 0, 0x00, 0x0F, 0xAC, 8, 0x00, 0x0F, 0xAC, 2, // 2 AKM: SAE, PSK
        0x8C, 0x00, // RSN capabilities: MFP capable, 16 PTKSA replay counters
    ]
}

// Valid except for management frame protection (MFP) required flag being set to true
pub fn invalid_wpa2_wpa3_rsne() -> Vec<u8> {
    vec![
        48, 18, // Element header
        1, 0, // Version
        0x00, 0x0F, 0xAC, 4, // Group Cipher: CCMP-128
        1, 0, 0x00, 0x0F, 0xAC, 4, // 1 Pairwise Cipher: CCMP-128
        2, 0, 0x00, 0x0F, 0xAC, 8, 0x00, 0x0F, 0xAC, 2, // 2 AKM: SAE, PSK
        0xCC, 0x00, // RSN capabilities: MFP capable + required, 16 PTKSA replay counters
    ]
}

pub fn fake_wpa3_rsne() -> Vec<u8> {
    vec![
        48, 18, // Element header
        1, 0, // Version
        0x00, 0x0F, 0xAC, 4, // Group Cipher: CCMP-128
        1, 0, 0x00, 0x0F, 0xAC, 4, // 1 Pairwise Cipher: CCMP-128
        1, 0, 0x00, 0x0F, 0xAC, 8, // 1 AKM: SAE
        0xCC, 0x00, // RSN capabilities: MFP capable + required, 16 PTKSA replay counters
    ]
}

// Valid except for management frame protection (MFP) required flag not being set
pub fn invalid_wpa3_rsne() -> Vec<u8> {
    vec![
        48, 18, // Element header
        1, 0, // Version
        0x00, 0x0F, 0xAC, 4, // Group Cipher: CCMP-128
        1, 0, 0x00, 0x0F, 0xAC, 4, // 1 Pairwise Cipher: CCMP-128
        1, 0, 0x00, 0x0F, 0xAC, 8, // 1 AKM: SAE
        0x8C, 0x00, // RSN capabilities: MFP capable, 16 PTKSA replay counters
    ]
}

pub fn fake_wpa2_enterprise_rsne() -> Vec<u8> {
    vec![
        48, 18, // Element header
        1, 0, // Version
        0x00, 0x0F, 0xAC, 4, // Group Cipher: CCMP-128
        1, 0, 0x00, 0x0F, 0xAC, 4, // 1 Pairwise Cipher: CCMP-128
        1, 0, 0x00, 0x0F, 0xAC, 1, // 1 AKM: EAP (802.1X)
    ]
}

pub fn fake_wpa3_enterprise_192_bit_rsne() -> Vec<u8> {
    vec![
        48, 18, // Element header
        1, 0, // Version
        0x00, 0x0F, 0xAC, 9, // Group Cipher: GCMP-256
        1, 0, 0x00, 0x0F, 0xAC, 9, // 1 Pairwise Cipher: GCMP-256
        1, 0, 0x00, 0x0F, 0xAC, 12, // 1 AKM: EAP-SUITEB-SHA384 (HMAC-SHA-384)
        0xCC, 0x00, // RSN capabilities: MFP capable + required, 16 PTKSA replay counters
        0x00, 0x00, // 0 PMKID
        0x00, 0x0F, 0xAC, 12, // Group Management Cipher: BIP-Gfxbug.dev/12616 (BIP-GCMP-256)
    ]
}

// Invalid due to group management not being specified (thus defaulting to BIP-CMAC-128, which
// is not part of WPA3 Enterprise 192-bit)
pub fn invalid_wpa3_enterprise_192_bit_rsne() -> Vec<u8> {
    vec![
        48, 18, // Element header
        1, 0, // Version
        0x00, 0x0F, 0xAC, 9, // Group Cipher: GCMP-256
        1, 0, 0x00, 0x0F, 0xAC, 9, // 1 Pairwise Cipher: GCMP-256
        1, 0, 0x00, 0x0F, 0xAC, 12, // 1 AKM: EAP-SUITEB-SHA384 (HMAC-SHA-384)
        0xCC, 0x00, // RSN capabilities: MFP capable + required, 16 PTKSA replay counters
    ]
}

// RSNE with AKM that we can't classify into a protection type
pub fn fake_unknown_rsne() -> Vec<u8> {
    vec![
        48, 18, // Element header
        1, 0, // Version
        0x00, 0x0F, 0xAC, 4, // Group Cipher: CCMP-128
        1, 0, 0x00, 0x0F, 0xAC, 4, // 1 Pairwise Cipher: CCMP-128
        1, 0, 0x00, 0x0F, 0xAC, 7, // 1 AKM: TDLS
    ]
}
