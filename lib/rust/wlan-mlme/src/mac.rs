// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::utils::skip,
    bitfield::bitfield,
    byteorder::{ByteOrder, LittleEndian},
    zerocopy::{AsBytes, ByteSlice, FromBytes, LayoutVerified, Unaligned},
};

pub const BCAST_ADDR: [u8; 6] = [0xFF; 6];

// IEEE Std 802.11-2016, 9.2.4.1.3
// Frame types:
const FRAME_TYPE_MGMT: u16 = 0;
const FRAME_TYPE_DATA: u16 = 2;
// Management subtypes:
const MGMT_SUBTYPE_ASSOC_RESP: u16 = 0x01;
const MGMT_SUBTYPE_BEACON: u16 = 0x08;
const MGMT_SUBTYPE_AUTH: u16 = 0x0B;
// Data subtypes:
const DATA_SUBTYPE_DATA: u16 = 0x00;
const DATA_SUBTYPE_QOS_DATA: u16 = 0x08;

// IEEE Std 802.11-2016, 9.2.4.1.3, Table 9-1
const BITMASK_QOS: u16 = 1 << 3;

// IEEE Std 802.11-2016, 9.2.4.1.1
bitfield! {
    #[derive(PartialEq)]
    pub struct FrameControl(u16);
    impl Debug;

    pub protocol_version, set_protocol_version: 1, 0;
    pub frame_type, set_frame_type: 3, 2;
    pub frame_subtype, set_frame_subtype: 7, 4;
    pub to_ds, set_to_ds: 8;
    pub from_ds, set_from_ds: 9;
    pub more_frag, set_more_frag: 19;
    pub retry, set_retry: 11;
    pub pwr_mgmt, set_pwr_mgmt: 12;
    pub more_data, set_more_data: 13;
    pub protected, set_protected: 14;
    pub htc_order, set_htc_order: 15;

    pub value, _: 15,0;
}

impl FrameControl {
    pub fn from_bytes(bytes: &[u8]) -> Option<FrameControl> {
        if bytes.len() < 2 {
            None
        } else {
            Some(FrameControl(LittleEndian::read_u16(bytes)))
        }
    }
}

// IEEE Std 802.11-2016, 9.2.4.4
bitfield! {
    pub struct SequenceControl(u16);
    impl Debug;

    pub frag_num, set_frag_num: 3, 0;
    pub seq_num, set_seq_num: 15, 4;

    pub value, _: 15,0;
}

impl SequenceControl {
    pub fn from_bytes(bytes: &[u8]) -> Option<SequenceControl> {
        if bytes.len() < 2 {
            None
        } else {
            Some(SequenceControl(LittleEndian::read_u16(bytes)))
        }
    }
}

// IEEE Std 802.11-2016, 9.2.4.6
bitfield! {
    #[derive(PartialEq)]
    pub struct HtControl(u32);
    impl Debug;

    pub vht, set_vht: 0;
    // Structure of this middle section is defined in 9.2.4.6.2 for HT,
    // and 9.2.4.6.3 for VHT.
    pub middle, set_middle: 29, 1;
    pub ac_constraint, set_ac_constraint: 30;
    pub rdg_more_ppdu, setrdg_more_ppdu: 31;

    pub value, _: 31,0;
}

impl HtControl {
    pub fn from_bytes(bytes: &[u8]) -> Option<HtControl> {
        if bytes.len() < 4 {
            None
        } else {
            Some(HtControl(LittleEndian::read_u32(bytes)))
        }
    }
}

// IEEE Std 802.11-2016, 9.4.1.4
bitfield! {
    pub struct CapabilityInfo(u16);
    impl Debug;

    pub ess, set_ess: 0;
    pub ibss, set_ibss: 1;
    pub cf_pollable, set_cf_pollable: 2;
    pub cf_poll_req, set_cf_poll_req: 3;
    pub privacy, set_privacy: 4;
    pub short_preamble, set_short_preamble: 5;
    // bit 6-7 reserved
    pub spectrum_mgmt, set_spectrum_mgmt: 8;
    pub qos, set_qos: 9;
    pub short_slot_time, set_short_slot_time: 10;
    pub apsd, set_apsd: 11;
    pub radio_msmt, set_radio_msmt: 12;
    // bit 13 reserved
    pub delayed_block_ack, set_delayed_block_ack: 14;
    pub immediate_block_ack, set_immediate_block_ack: 15;

    pub value, _: 15, 0;
}

impl CapabilityInfo {
    pub fn from_bytes(bytes: &[u8]) -> Option<CapabilityInfo> {
        if bytes.len() < 4 {
            None
        } else {
            Some(CapabilityInfo(LittleEndian::read_u16(bytes)))
        }
    }
}

// IEEE Std 802.11-2016, 9.3.3.2
#[repr(C, packed)]
pub struct MgmtHdr {
    pub frame_ctrl: [u8; 2],
    pub duration: [u8; 2],
    pub addr1: [u8; 6],
    pub addr2: [u8; 6],
    pub addr3: [u8; 6],
    pub seq_ctrl: [u8; 2],
}
// Safe: see macro explanation.
unsafe_impl_zerocopy_traits!(MgmtHdr);

impl MgmtHdr {
    pub fn frame_ctrl(&self) -> u16 {
        LittleEndian::read_u16(&self.frame_ctrl)
    }

    pub fn duration(&self) -> u16 {
        LittleEndian::read_u16(&self.duration)
    }

    pub fn seq_ctrl(&self) -> u16 {
        LittleEndian::read_u16(&self.seq_ctrl)
    }
}

// IEEE Std 802.11-2016, 9.3.2.1
#[repr(C, packed)]
pub struct DataHdr {
    pub frame_ctrl: [u8; 2],
    pub duration: [u8; 2],
    pub addr1: [u8; 6],
    pub addr2: [u8; 6],
    pub addr3: [u8; 6],
    pub seq_ctrl: [u8; 2],
}
// Safe: see macro explanation.
unsafe_impl_zerocopy_traits!(DataHdr);

impl DataHdr {
    pub fn frame_ctrl(&self) -> u16 {
        LittleEndian::read_u16(&self.frame_ctrl)
    }

    pub fn duration(&self) -> u16 {
        LittleEndian::read_u16(&self.duration)
    }

    pub fn seq_ctrl(&self) -> u16 {
        LittleEndian::read_u16(&self.seq_ctrl)
    }
}

pub enum MacFrame<B> {
    Mgmt {
        mgmt_hdr: LayoutVerified<B, MgmtHdr>,
        ht_ctrl: Option<LayoutVerified<B, [u8; 4]>>,
        body: B,
    },
    Data {
        data_hdr: LayoutVerified<B, DataHdr>,
        addr4: Option<LayoutVerified<B, [u8; 6]>>,
        qos_ctrl: Option<LayoutVerified<B, [u8; 2]>>,
        ht_ctrl: Option<LayoutVerified<B, [u8; 4]>>,
        body: B,
    },
    Unsupported {
        type_: u16,
    },
}

impl<B: ByteSlice> MacFrame<B> {
    /// Padding is optional and follows after the MAC header.
    pub fn parse(bytes: B, padding: usize) -> Option<MacFrame<B>> {
        let fc = FrameControl::from_bytes(&bytes[..])?;
        match fc.frame_type() {
            FRAME_TYPE_MGMT => {
                let (mgmt_hdr, body) = LayoutVerified::new_unaligned_from_prefix(bytes)?;
                let body = skip(body, padding)?;

                // HT Control field is optional.
                let (ht_ctrl, body) = if fc.htc_order() {
                    let (ht_ctrl, body) = LayoutVerified::new_unaligned_from_prefix(body)?;
                    (Some(ht_ctrl), body)
                } else {
                    (None, body)
                };

                Some(MacFrame::Mgmt {
                    mgmt_hdr,
                    ht_ctrl,
                    body,
                })
            }
            FRAME_TYPE_DATA => {
                let (data_hdr, body) = LayoutVerified::new_unaligned_from_prefix(bytes)?;
                let body = skip(body, padding)?;

                // addr4 field is optional.
                let (addr4, body) = if fc.to_ds() && fc.from_ds() {
                    let (addr4, body) = LayoutVerified::new_unaligned_from_prefix(body)?;
                    (Some(addr4), body)
                } else {
                    (None, body)
                };

                // QoS Control field is optional.
                let has_qos_ctrl = fc.frame_subtype() & BITMASK_QOS != 0;
                let (qos_ctrl, body) = if has_qos_ctrl {
                    let (qos_ctrl, body) = LayoutVerified::new_unaligned_from_prefix(body)?;
                    (Some(qos_ctrl), body)
                } else {
                    (None, body)
                };

                // HT Control field is optional.
                let (ht_ctrl, body) = if fc.htc_order() {
                    let (ht_ctrl, body) = LayoutVerified::new_unaligned_from_prefix(body)?;
                    (Some(ht_ctrl), body)
                } else {
                    (None, body)
                };

                Some(MacFrame::Data {
                    data_hdr,
                    addr4,
                    qos_ctrl,
                    ht_ctrl,
                    body,
                })
            }
            type_ => Some(MacFrame::Unsupported { type_ }),
        }
    }
}

// IEEE Std 802.11-2016, 9.3.3.3
#[repr(C, packed)]
pub struct BeaconHdr {
    pub timestamp: [u8; 8],
    pub beacon_interval: [u8; 2],
    // IEEE Std 802.11-2016, 9.4.1.4
    pub capabilities: [u8; 2],
}
// Safe: see macro explanation.
unsafe_impl_zerocopy_traits!(BeaconHdr);

impl BeaconHdr {
    pub fn timestamp(&self) -> u64 {
        LittleEndian::read_u64(&self.timestamp)
    }

    pub fn beacon_interval(&self) -> u16 {
        LittleEndian::read_u16(&self.beacon_interval)
    }

    pub fn capabilities(&self) -> u16 {
        LittleEndian::read_u16(&self.capabilities)
    }
}

// IEEE Std 802.11-2016, 9.3.3.12
#[repr(C, packed)]
pub struct AuthHdr {
    pub auth_alg_num: [u8; 2],
    pub auth_txn_seq_num: [u8; 2],
    pub status_code: [u8; 2],
}
// Safe: see macro explanation.
unsafe_impl_zerocopy_traits!(AuthHdr);

impl AuthHdr {
    pub fn auth_alg_num(&self) -> u16 {
        LittleEndian::read_u16(&self.auth_alg_num)
    }

    pub fn auth_txn_seq_num(&self) -> u16 {
        LittleEndian::read_u16(&self.auth_txn_seq_num)
    }

    pub fn status_code(&self) -> u16 {
        LittleEndian::read_u16(&self.status_code)
    }
}

// IEEE Std 802.11-2016, 9.3.3.6
#[repr(C, packed)]
pub struct AssocRespHdr {
    // IEEE Std 802.11-2016, 9.4.1.4
    pub capabilities: [u8; 2],
    pub status_code: [u8; 2],
    pub aid: [u8; 2],
}
// Safe: see macro explanation.
unsafe_impl_zerocopy_traits!(AssocRespHdr);

impl AssocRespHdr {
    pub fn capabilities(&self) -> u16 {
        LittleEndian::read_u16(&self.capabilities)
    }

    pub fn status_code(&self) -> u16 {
        LittleEndian::read_u16(&self.status_code)
    }

    pub fn aid(&self) -> u16 {
        LittleEndian::read_u16(&self.aid)
    }
}

pub enum MgmtSubtype<B> {
    Beacon {
        hdr: LayoutVerified<B, BeaconHdr>,
        elements: B,
    },
    Authentication {
        hdr: LayoutVerified<B, AuthHdr>,
        elements: B,
    },
    AssociationResp {
        hdr: LayoutVerified<B, AssocRespHdr>,
        elements: B,
    },
    Unsupported {
        subtype: u16,
    },
}

impl<B: ByteSlice> MgmtSubtype<B> {
    pub fn parse(subtype: u16, bytes: B) -> Option<MgmtSubtype<B>> {
        match subtype {
            MGMT_SUBTYPE_BEACON => {
                let (hdr, elements) = LayoutVerified::new_unaligned_from_prefix(bytes)?;
                Some(MgmtSubtype::Beacon { hdr, elements })
            }
            MGMT_SUBTYPE_AUTH => {
                let (hdr, elements) = LayoutVerified::new_unaligned_from_prefix(bytes)?;
                Some(MgmtSubtype::Authentication { hdr, elements })
            }
            MGMT_SUBTYPE_ASSOC_RESP => {
                let (hdr, elements) = LayoutVerified::new_unaligned_from_prefix(bytes)?;
                Some(MgmtSubtype::AssociationResp { hdr, elements })
            }
            subtype => Some(MgmtSubtype::Unsupported { subtype }),
        }
    }
}

// IEEE Std 802.2-1998, 3.2
// IETF RFC 1042
#[repr(C, packed)]
pub struct LlcHdr {
    pub dsap: u8,
    pub ssap: u8,
    pub control: u8,
    pub oui: [u8; 3],
    pub protocol_id: [u8; 2],
}
// Safe: see macro explanation.
unsafe_impl_zerocopy_traits!(LlcHdr);

impl LlcHdr {
    pub fn protocol_id(&self) -> u16 {
        LittleEndian::read_u16(&self.protocol_id)
    }
}

pub enum DataSubtype<B> {
    // QoS or regular data type.
    Data {
        is_qos: bool,
        hdr: LayoutVerified<B, LlcHdr>,
        payload: B,
    },
    Unsupported {
        subtype: u16,
    },
}

impl<B: ByteSlice> DataSubtype<B> {
    pub fn parse(subtype: u16, bytes: B) -> Option<DataSubtype<B>> {
        match subtype {
            DATA_SUBTYPE_DATA | DATA_SUBTYPE_QOS_DATA => {
                let (hdr, payload) = LayoutVerified::new_unaligned_from_prefix(bytes)?;
                let is_qos = subtype == DATA_SUBTYPE_QOS_DATA;
                Some(DataSubtype::Data {
                    hdr,
                    is_qos,
                    payload,
                })
            }
            subtype => Some(DataSubtype::Unsupported { subtype }),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::mem::transmute;

    fn make_mgmt_frame(ht_ctrl: bool, padding: bool) -> Vec<u8> {
        #[cfg_attr(rustfmt, rustfmt_skip)]
        let mut bytes = vec![
            1, if ht_ctrl { 128 } else { 1 }, // fc
            2, 2, // duration
            3, 3, 3, 3, 3, 3, // addr1
            4, 4, 4, 4, 4, 4, // addr2
            5, 5, 5, 5, 5, 5, // addr3
            6, 6, // sequence control
        ];
        if padding {
            bytes.extend_from_slice(&[7, 7, 7, 7]);
        }
        if ht_ctrl {
            bytes.extend_from_slice(&[8, 8, 8, 8]);
        }
        bytes.extend_from_slice(&[9, 9, 9]);
        bytes
    }

    fn make_data_frame(addr4: Option<[u8; 6]>, ht_ctrl: Option<[u8; 4]>) -> Vec<u8> {
        let mut fc = FrameControl(0);
        fc.set_frame_type(FRAME_TYPE_DATA);
        fc.set_frame_subtype(DATA_SUBTYPE_QOS_DATA);
        fc.set_from_ds(addr4.is_some());
        fc.set_to_ds(addr4.is_some());
        fc.set_htc_order(ht_ctrl.is_some());
        let fc: [u8; 2] = unsafe { transmute(fc.value().to_le()) };

        #[cfg_attr(rustfmt, rustfmt_skip)]
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

        // QoS Control
        bytes.extend_from_slice(&[1, 1]);

        if let Some(ht_ctrl) = ht_ctrl {
            bytes.extend_from_slice(&ht_ctrl);
        }

        bytes.extend_from_slice(&[
            // LLC Header
            7, 7, 7, // DSAP, SSAP & control
            8, 8, 8, // OUI
            9, 9, // eth type
        ]);

        // Some trailing bytes.
        bytes.extend_from_slice(&[10, 10, 10]);
        bytes
    }

    #[test]
    fn parse_mgmt_frame() {
        let bytes = make_mgmt_frame(false, false);
        match MacFrame::parse(&bytes[..], 0) {
            Some(MacFrame::Mgmt {
                mgmt_hdr,
                ht_ctrl,
                body,
            }) => {
                assert_eq!([1, 1], mgmt_hdr.frame_ctrl);
                assert_eq!([2, 2], mgmt_hdr.duration);
                assert_eq!([3, 3, 3, 3, 3, 3], mgmt_hdr.addr1);
                assert_eq!([4, 4, 4, 4, 4, 4], mgmt_hdr.addr2);
                assert_eq!([5, 5, 5, 5, 5, 5], mgmt_hdr.addr3);
                assert_eq!([6, 6], mgmt_hdr.seq_ctrl);
                assert!(ht_ctrl.is_none());
                assert_eq!(&body[..], &[9, 9, 9]);
            }
            _ => panic!("failed parsing mgmt frame"),
        };
    }

    #[test]
    fn parse_mgmt_frame_with_ht_ctrl_padding() {
        let bytes = make_mgmt_frame(true, true);
        match MacFrame::parse(&bytes[..], 4) {
            Some(MacFrame::Mgmt {
                mgmt_hdr,
                ht_ctrl,
                body,
            }) => {
                assert_eq!([6, 6], mgmt_hdr.seq_ctrl);
                assert_eq!([8, 8, 8, 8], *ht_ctrl.expect("ht_ctrl not present"));
                assert_eq!(&[9, 9, 9], &body[..]);
            }
            _ => panic!("failed parsing mgmt frame"),
        };
    }

    #[test]
    fn parse_mgmt_frame_too_short_unsupported() {
        // Valid MGMT header must have a minium length of 24 bytes.
        assert!(MacFrame::parse(&[0; 22][..], 0).is_none());

        // Unsupported frame type.
        match MacFrame::parse(&[0xFF; 24][..], 0) {
            Some(MacFrame::Unsupported { type_ }) => assert_eq!(3, type_),
            _ => panic!("didn't detect unsupported frame"),
        };
    }

    #[test]
    fn parse_beacon_frame() {
        #[cfg_attr(rustfmt, rustfmt_skip)]
        let bytes = vec![
            1,1,1,1,1,1,1,1, // timestamp
            2,2, // beacon_interval
            3,3, // capabilities
            0,5,1,2,3,4,5 // SSID IE: "12345"
        ];
        match MgmtSubtype::parse(MGMT_SUBTYPE_BEACON, &bytes[..]) {
            Some(MgmtSubtype::Beacon { hdr, elements }) => {
                assert_eq!(0x0101010101010101, hdr.timestamp());
                assert_eq!(0x0202, hdr.beacon_interval());
                assert_eq!(0x0303, hdr.capabilities());
                assert_eq!(&[0, 5, 1, 2, 3, 4, 5], &elements[..]);
            }
            _ => panic!("failed parsing beacon frame"),
        };
    }

    #[test]
    fn parse_data_frame() {
        let bytes = make_data_frame(None, None);
        match MacFrame::parse(&bytes[..], 0) {
            Some(MacFrame::Data {
                data_hdr,
                addr4,
                qos_ctrl,
                ht_ctrl,
                body,
            }) => {
                assert_eq!([0b10001000, 0], data_hdr.frame_ctrl);
                assert_eq!([2, 2], data_hdr.duration);
                assert_eq!([3, 3, 3, 3, 3, 3], data_hdr.addr1);
                assert_eq!([4, 4, 4, 4, 4, 4], data_hdr.addr2);
                assert_eq!([5, 5, 5, 5, 5, 5], data_hdr.addr3);
                assert_eq!([6, 6], data_hdr.seq_ctrl);
                assert!(addr4.is_none());
                match qos_ctrl {
                    None => panic!("qos_ctrl expected to be present"),
                    Some(qos_ctrl) => {
                        assert_eq!(&[1, 1][..], &qos_ctrl[..]);
                    }
                };
                assert!(ht_ctrl.is_none());
                assert_eq!(&body[..], &[7, 7, 7, 8, 8, 8, 9, 9, 10, 10, 10]);
            }
            _ => panic!("failed parsing data frame"),
        };
    }

    #[test]
    fn parse_llc_with_addr4_ht_ctrl() {
        let bytes = make_data_frame(Some([1, 2, 3, 4, 5, 6]), Some([4, 3, 2, 1]));
        match MacFrame::parse(&bytes[..], 0) {
            Some(MacFrame::Data { data_hdr, body, .. }) => {
                let fc = FrameControl(data_hdr.frame_ctrl());
                match DataSubtype::parse(fc.frame_subtype(), &body[..]) {
                    Some(DataSubtype::Data { hdr, payload, is_qos }) => {
                        assert!(is_qos);
                        assert_eq!(7, hdr.dsap);
                        assert_eq!(7, hdr.ssap);
                        assert_eq!(7, hdr.control);
                        assert_eq!([8, 8, 8], hdr.oui);
                        assert_eq!([9, 9], hdr.protocol_id);
                        assert_eq!(&[10, 10, 10], payload);
                    },
                    _ => panic!("failed parsing LLC"),
                }
            }
            _ => panic!("failed parsing data frame"),
        };
    }

}
