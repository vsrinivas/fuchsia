// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::utils::skip,
    bitfield::bitfield,
    byteorder::{BigEndian, ByteOrder, LittleEndian},
    num::{One, Unsigned},
    zerocopy::{AsBytes, ByteSlice, FromBytes, LayoutVerified, Unaligned},
};

type MacAddr = [u8; 6];
pub const BCAST_ADDR: MacAddr = [0xFF; 6];

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
    pub addr1: MacAddr,
    pub addr2: MacAddr,
    pub addr3: MacAddr,
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

    /// Returns the length in bytes of a mgmt header including all its fixed and optional
    /// fields (if they are present).
    pub fn len(has_ht_ctrl: bool) -> usize {
        let mut bytes = std::mem::size_of::<DataHdr>();
        bytes += if has_ht_ctrl { std::mem::size_of::<RawHtControl>() } else { 0 };
        bytes
    }
}

// IEEE Std 802.11-2016, 9.3.2.1
#[repr(C, packed)]
pub struct DataHdr {
    pub frame_ctrl: [u8; 2],
    pub duration: [u8; 2],
    pub addr1: MacAddr,
    pub addr2: MacAddr,
    pub addr3: MacAddr,
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

    /// Returns the length in bytes of a data header including all its fixed and optional
    /// fields (if they are present).
    pub fn len(has_addr4: bool, has_qos_ctrl: bool, has_ht_ctrl: bool) -> usize {
        let mut bytes = std::mem::size_of::<DataHdr>();
        bytes += if has_addr4 { std::mem::size_of::<MacAddr>() } else { 0 };
        bytes += if has_qos_ctrl { std::mem::size_of::<RawQosControl>() } else { 0 };
        bytes += if has_ht_ctrl { std::mem::size_of::<RawHtControl>() } else { 0 };
        bytes
    }
}

type RawHtControl = [u8; 4];
type RawQosControl = [u8; 2];

pub enum MacFrame<B> {
    Mgmt {
        // Management Header: fixed fields
        mgmt_hdr: LayoutVerified<B, MgmtHdr>,
        // Management Header: optional fields
        ht_ctrl: Option<LayoutVerified<B, RawHtControl>>,
        // Body
        body: B,
    },
    Data {
        // Data Header: fixed fields
        data_hdr: LayoutVerified<B, DataHdr>,
        // Data Header: optional fields
        addr4: Option<LayoutVerified<B, MacAddr>>,
        qos_ctrl: Option<LayoutVerified<B, RawQosControl>>,
        ht_ctrl: Option<LayoutVerified<B, RawHtControl>>,
        // Body
        body: B,
    },
    Unsupported {
        type_: u16,
    },
}

impl<B: ByteSlice> MacFrame<B> {
    /// If `body_aligned` is |true| the frame's body is expected to be 4 byte aligned.
    pub fn parse(bytes: B, body_aligned: bool) -> Option<MacFrame<B>> {
        let fc = FrameControl::from_bytes(&bytes[..])?;
        match fc.frame_type() {
            FRAME_TYPE_MGMT => {
                // Parse fixed header fields:
                let (mgmt_hdr, body) = LayoutVerified::new_unaligned_from_prefix(bytes)?;

                // Parse optional header fields:
                let (ht_ctrl, body) = parse_ht_ctrl_if_present(&fc, body)?;

                // Skip optional padding if body alignment is used.
                let body = if body_aligned {
                    let full_hdr_len = MgmtHdr::len(ht_ctrl.is_some());
                    skip_body_alignment_padding(full_hdr_len, body)?
                } else {
                    body
                };

                Some(MacFrame::Mgmt {
                    mgmt_hdr,
                    ht_ctrl,
                    body,
                })
            }
            FRAME_TYPE_DATA => {
                // Parse fixed header fields:
                let (data_hdr, body) = LayoutVerified::new_unaligned_from_prefix(bytes)?;

                // Parse optional header fields:
                let (addr4, body) = parse_addr4_if_present(&fc, body)?;
                let (qos_ctrl, body) = parse_qos_if_present(&fc, body)?;
                let (ht_ctrl, body) = parse_ht_ctrl_if_present(&fc, body)?;

                // Skip optional padding if body alignment is used.
                let body = if body_aligned {
                    let full_hdr_len = DataHdr::len(addr4.is_some(), qos_ctrl.is_some(),
                                                    ht_ctrl.is_some());
                    skip_body_alignment_padding(full_hdr_len, body)?
                } else  {
                    body
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


/// Returns |None| if parsing fails. Otherwise returns |Some(tuple)| with `tuple` holding a
/// `MacAddr` if it is present and the remaining bytes.
fn parse_addr4_if_present<B: ByteSlice>(fc: &FrameControl,  bytes: B)
    -> Option<(Option<LayoutVerified<B, MacAddr>>, B)>
{
    if fc.to_ds() && fc.from_ds() {
        let (addr4, bytes) = LayoutVerified::new_unaligned_from_prefix(bytes)?;
        Some((Some(addr4), bytes))
    } else {
        Some((None, bytes))
    }
}


/// Returns |None| if parsing fails. Otherwise returns |Some(tuple)| with `tuple` holding the
/// `QosControl` if it is present and the remaining bytes.
fn parse_qos_if_present<B: ByteSlice>(fc: &FrameControl,  bytes: B)
    -> Option<(Option<LayoutVerified<B, RawQosControl>>, B)>
{
    if fc.frame_subtype() & BITMASK_QOS != 0 {
        let (qos_ctrl, bytes) = LayoutVerified::new_unaligned_from_prefix(bytes)?;
        Some((Some(qos_ctrl), bytes))
    } else {
        Some((None, bytes))
    }
}

/// Returns |None| if parsing fails. Otherwise returns |Some(tuple)| with `tuple` holding the
/// `HtControl` if it is present and the remaining bytes.
fn parse_ht_ctrl_if_present<B: ByteSlice>(fc: &FrameControl,  bytes: B)
                                          -> Option<(Option<LayoutVerified<B, RawHtControl>>, B)>
{
    if fc.htc_order() {
        let (ht_ctrl, bytes) = LayoutVerified::new_unaligned_from_prefix(bytes)?;
        Some((Some(ht_ctrl), bytes))
    } else {
        Some((None, bytes))
    }
}

/// Skips optional padding required for body alignment.
fn skip_body_alignment_padding<B: ByteSlice>(hdr_len: usize,  bytes: B) -> Option<B> {
    const OPTIONAL_BODY_ALIGNMENT_BYTES: usize = 4;

    let padded_len = round_up(hdr_len, OPTIONAL_BODY_ALIGNMENT_BYTES);
    let padding = padded_len - hdr_len;
    skip(bytes, padding)
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
        bcn_hdr: LayoutVerified<B, BeaconHdr>,
        elements: B,
    },
    Authentication {
        auth_hdr: LayoutVerified<B, AuthHdr>,
        elements: B,
    },
    AssociationResp {
        assoc_resp_hdr: LayoutVerified<B, AssocRespHdr>,
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
                let (bcn_hdr, elements) = LayoutVerified::new_unaligned_from_prefix(bytes)?;
                Some(MgmtSubtype::Beacon { bcn_hdr, elements })
            }
            MGMT_SUBTYPE_AUTH => {
                let (auth_hdr, elements) = LayoutVerified::new_unaligned_from_prefix(bytes)?;
                Some(MgmtSubtype::Authentication { auth_hdr, elements })
            }
            MGMT_SUBTYPE_ASSOC_RESP => {
                let (assoc_resp_hdr, elements) = LayoutVerified::new_unaligned_from_prefix(bytes)?;
                Some(MgmtSubtype::AssociationResp { assoc_resp_hdr, elements })
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

// IEEE Std 802.11-2016, 9.3.2.2.2
#[repr(C, packed)]
pub struct AmsduSubframeHdr {
    // Note this is the same as the IEEE 802.3 frame format.
    pub da: MacAddr,
    pub sa: MacAddr,
    pub msdu_len_be: [u8; 2], // In network byte order (big endian).
}
// Safe: see macro explanation.
unsafe_impl_zerocopy_traits!(AmsduSubframeHdr);

impl AmsduSubframeHdr {
    pub fn msdu_len(&self) -> u16 {
        BigEndian::read_u16(&self.msdu_len_be)
    }
}

/// Iterates through an A-MSDU frame and provides access to
/// individual MSDUs.
/// The reader expects the byte stream to start with an
/// `AmsduSubframeHdr`.
pub struct AmsduReader<'a>(&'a [u8]);

impl<'a> AmsduReader<'a> {
    pub fn has_remaining(&self) -> bool {
        !self.0.is_empty()
    }

    pub fn remaining(&self) -> &'a [u8] {
        self.0
    }
}

impl<'a> Iterator for AmsduReader<'a> {
    type Item = &'a [u8];

    fn next(&mut self) -> Option<Self::Item> {
        let (amsdu_subframe_hdr, msdu) =
            LayoutVerified::<_, AmsduSubframeHdr>::new_unaligned_from_prefix(&self.0[..])?;
        let msdu_len = amsdu_subframe_hdr.msdu_len() as usize;
        if msdu.len() < msdu_len {
            // A-MSDU subframe header is valid, but MSDU doesn't fit into the buffer.
            None
        } else {
            let (msdu, next_padded) = msdu.split_at(msdu_len);

            // Padding following the last MSDU is optional.
            if next_padded.is_empty() {
                self.0 = next_padded;
                Some(msdu)
            } else {
                let base_len = std::mem::size_of::<AmsduSubframeHdr>() + msdu_len;
                let padded_len = round_up(base_len, 4);
                let padding_len = padded_len - base_len;
                if next_padded.len() < padding_len {
                    // Corrupted: buffer to short to hold padding.
                    None
                } else {
                    let (_padding, next) = next_padded.split_at(padding_len);
                    self.0 = next;
                    Some(msdu)
                }
            }
        }
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

fn round_up<T: Unsigned + Copy>(value: T, multiple: T) -> T {
    let overshoot = value + multiple - T::one();
    overshoot - overshoot % multiple
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::mem::transmute;

    fn make_mgmt_frame(ht_ctrl: bool) -> Vec<u8> {
        #[cfg_attr(rustfmt, rustfmt_skip)]
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
            // Trailing bytes
            10, 10, 10,
        ]);
        bytes
    }

    fn make_data_frame_with_padding() -> Vec<u8> {
        let mut fc = FrameControl(0);
        fc.set_frame_type(FRAME_TYPE_DATA);
        fc.set_frame_subtype(DATA_SUBTYPE_QOS_DATA);
        let fc: [u8; 2] = unsafe { transmute(fc.value().to_le()) };

        #[cfg_attr(rustfmt, rustfmt_skip)]
        let bytes = vec![
            // Data Header
            fc[0], fc[1], // fc
            2, 2, // duration
            3, 3, 3, 3, 3, 3, // addr1
            4, 4, 4, 4, 4, 4, // addr2
            5, 5, 5, 5, 5, 5, // addr3
            6, 6, // sequence control
            // QoS Control
            1, 1,
            // Padding
            2, 2,
            // Body
            7, 7, 7,
        ];
        bytes
    }

    #[test]
    fn mgmt_hdr_len() {
        assert_eq!(MgmtHdr::len(false), 24);
        assert_eq!(MgmtHdr::len(true), 28);
    }

    #[test]
    fn data_hdr_len() {
        assert_eq!(DataHdr::len(false, false, false), 24);
        assert_eq!(DataHdr::len(true, false, false), 30);
        assert_eq!(DataHdr::len(false, true, false), 26);
        assert_eq!(DataHdr::len(false, false, true), 28);
        assert_eq!(DataHdr::len(true, true, false), 32);
        assert_eq!(DataHdr::len(false, true, true), 30);
        assert_eq!(DataHdr::len(true, false, true), 34);
        assert_eq!(DataHdr::len(true, true, true), 36);
    }

    #[test]
    fn parse_mgmt_frame() {
        let bytes = make_mgmt_frame(false);
        match MacFrame::parse(&bytes[..], false) {
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
    fn parse_mgmt_frame_too_short_unsupported() {
        // Valid MGMT header must have a minium length of 24 bytes.
        assert!(MacFrame::parse(&[0; 22][..], false).is_none());

        // Unsupported frame type.
        match MacFrame::parse(&[0xFF; 24][..], false) {
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
            Some(MgmtSubtype::Beacon { bcn_hdr, elements }) => {
                assert_eq!(0x0101010101010101, bcn_hdr.timestamp());
                assert_eq!(0x0202, bcn_hdr.beacon_interval());
                assert_eq!(0x0303, bcn_hdr.capabilities());
                assert_eq!(&[0, 5, 1, 2, 3, 4, 5], &elements[..]);
            }
            _ => panic!("failed parsing beacon frame"),
        };
    }

    #[test]
    fn parse_data_frame() {
        let bytes = make_data_frame(None, None);
        match MacFrame::parse(&bytes[..], false) {
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
    fn parse_data_frame_with_padding() {
        let bytes = make_data_frame_with_padding();
        match MacFrame::parse(&bytes[..], true) {
            Some(MacFrame::Data { qos_ctrl, body, .. }) => {
                assert_eq!([1, 1], *qos_ctrl.expect("qos_ctrl not present"));
                assert_eq!(&[7, 7, 7], &body[..]);
            }
            _ => panic!("failed parsing data frame"),
        };
    }

    #[test]
    fn parse_llc_with_addr4_ht_ctrl() {
        let bytes = make_data_frame(Some([1, 2, 3, 4, 5, 6]), Some([4, 3, 2, 1]));
        match MacFrame::parse(&bytes[..], false) {
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

    #[test]
    fn parse_amsdu() {
        #[cfg_attr(rustfmt, rustfmt_skip)]
        let first_msdu = vec![
            // LLC header
            0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00, 0x08, 0x00,
            // Payload
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
        #[cfg_attr(rustfmt, rustfmt_skip)]
        let second_msdu = vec![
            // LLC header
            0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00, 0x08, 0x00,
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

        #[cfg_attr(rustfmt, rustfmt_skip)]
        let mut amsdu_frame = vec![
            // A-MSDU Subframe #1
            0x78, 0x8a, 0x20, 0x0d, 0x67, 0x03, 0xb4, 0xf7,
            0xa1, 0xbe, 0xb9, 0xab,
            0x00, 0x74, // MSDU length
        ];
        amsdu_frame.extend(&first_msdu[..]);
        amsdu_frame.extend(vec![
            // Padding
            0x00, 0x00, // A-MSDU Subframe #2
            0x78, 0x8a, 0x20, 0x0d, 0x67, 0x03, 0xb4, 0xf7, 0xa1, 0xbe, 0xb9, 0xab, 0x00,
            0x66, // MSDU length
        ]);
        amsdu_frame.extend(&second_msdu[..]);

        let mut found_msdus = (false, false);
        let mut rdr = AmsduReader(&amsdu_frame[..]);
        for msdu in &mut rdr {
            match found_msdus {
                (false, false) => {
                    assert_eq!(msdu, &first_msdu[..]);
                    found_msdus = (true, false);
                }
                (true, false) => {
                    assert_eq!(msdu, &second_msdu[..]);
                    found_msdus = (true, true);
                }
                _ => panic!("unexpected MSDU: {:x?}", msdu),
            }
        }
        assert_eq!((true, true), found_msdus);
    }
}
