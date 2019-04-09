// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::mac::*;

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
