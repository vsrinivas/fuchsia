// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{buffer_reader::BufferReader, unaligned_view::UnalignedView},
    byteorder::{BigEndian, ByteOrder, LittleEndian},
    num::Unsigned,
    num_derive::{FromPrimitive, ToPrimitive},
    std::marker::PhantomData,
    wlan_bitfields::bitfields,
    zerocopy::{AsBytes, ByteSlice, FromBytes, LayoutVerified, Unaligned},
};

#[macro_export]
macro_rules! frame_len {
    () => { 0 };
    ($only:ty) => { std::mem::size_of::<$only>() };
    ($first:ty, $($tail:ty),*) => {
        std::mem::size_of::<$first>() + frame_len!($($tail),*)
    };
}

type MacAddr = [u8; 6];
pub const BCAST_ADDR: MacAddr = [0xFF; 6];

// IEEE Std 802.11-2016, 9.2.4.1.3
// Frame types:
pub const FRAME_TYPE_MGMT: u16 = 0;
pub const FRAME_TYPE_DATA: u16 = 2;
// Management subtypes:
pub const MGMT_SUBTYPE_ASSOC_RESP: u16 = 0x01;
pub const MGMT_SUBTYPE_BEACON: u16 = 0x08;
pub const MGMT_SUBTYPE_AUTH: u16 = 0x0B;
pub const MGMT_SUBTYPE_DEAUTH: u16 = 0x0C;
// Data subtypes:
pub const DATA_SUBTYPE_DATA: u16 = 0x00;
pub const DATA_SUBTYPE_NULL_DATA: u16 = 0x04;
pub const DATA_SUBTYPE_QOS_DATA: u16 = 0x08;
pub const DATA_SUBTYPE_NULL_QOS_DATA: u16 = 0x0C;

// IEEE Std 802.11-2016, 9.2.4.1.3, Table 9-1
pub const BITMASK_NULL: u16 = 1 << 2;
pub const BITMASK_QOS: u16 = 1 << 3;

// RFC 1042
pub const LLC_SNAP_EXTENSION: u8 = 0xAA;
pub const LLC_SNAP_UNNUMBERED_INFO: u8 = 0x03;
pub const LLC_SNAP_OUI: [u8; 3] = [0, 0, 0];

// RFC 704, Appendix B.2
// https://www.iana.org/assignments/ieee-802-numbers/ieee-802-numbers.xhtml
pub const ETHER_TYPE_EAPOL: u16 = 0x888E;

pub const MAX_ETH_FRAME_LEN: usize = 2048;

// IEEE Std 802.11-2016, 9.2.4.1.1
#[bitfields(
    0..=1   protocol_version,
    2..=3   frame_type,
    4..=7   frame_subtype,
    8       to_ds,
    9       from_ds,
    10      more_fragments,
    11      retry,
    12      power_mgmt,
    13      more_data,
    14      protected,
    15      htc_order
)]
#[derive(AsBytes, FromBytes, PartialEq, Eq, Clone, Copy, Debug)]
#[repr(C)]
pub struct FrameControl(pub u16);

// IEEE Std 802.11-2016, 9.2.4.4
#[bitfields(
    0..=3   frag_num,
    4..=15  seq_num,
)]
#[derive(AsBytes, FromBytes, PartialEq, Eq, Clone, Copy, Debug)]
#[repr(C)]
pub struct SequenceControl(pub u16);

// IEEE Std 802.11-2016, 9.2.4.6
#[bitfields(
    0       vht,
    1..=29  middle, // see 9.2.4.6.2 for HT and 9.2.4.6.3 for VHT
    30      ac_constraint,
    31      rdg_more_ppdu,
)]
#[repr(C)]
#[derive(AsBytes, FromBytes, Copy, Clone, Debug, PartialEq, Eq)]
pub struct HtControl(pub u32);

type RawCapabilityInfo = [u8; 2];

// IEEE Std 802.11-2016, 9.4.1.4
#[bitfields(
    0       ess,
    1       ibss,
    2       cf_pollable,
    3       cf_poll_req,
    4       privacy,
    5       short_preamble,
    6..=7   _,  // reserved
    8       spectrum_mgmt,
    9       qos,
    10      short_slot_time,
    11      apsd,
    12      radio_measurement,
    13      _,  // reserved
    14      delayed_block_ack,
    15      immediate_block_ack,
)]
pub struct CapabilityInfo(pub u16);

// IEEE Std 802.11-2016, 9.2.4.5.1, Table 9-6
#[bitfields(
    0..=3   tid,
    4       eosp,
    5..=6   ack_policy,
    7       amsdu_present,
    8..=15  high_byte, // interpretation varies
)]
#[repr(C)]
#[derive(AsBytes, FromBytes, Copy, Clone, Debug, PartialEq, Eq)]
pub struct QosControl(pub u16);

#[derive(PartialEq, Eq)]
pub struct Presence<T: ?Sized>(bool, PhantomData<T>);

impl<T: ?Sized> Presence<T> {
    pub fn from_bool(present: bool) -> Self {
        Self(present, PhantomData)
    }
}

pub trait OptionalField {
    const PRESENT: Presence<Self> = Presence::<Self>(true, PhantomData);
    const ABSENT: Presence<Self> = Presence::<Self>(false, PhantomData);
}
impl<T: ?Sized> OptionalField for T {}

// IEEE Std 802.11-2016, 9.3.3.2
#[derive(FromBytes, AsBytes, Unaligned, PartialEq, Eq, Clone, Copy, Debug)]
#[repr(C, packed)]
pub struct MgmtHdr {
    pub frame_ctrl: FrameControl,
    pub duration: u16,
    pub addr1: MacAddr,
    pub addr2: MacAddr,
    pub addr3: MacAddr,
    pub seq_ctrl: SequenceControl,
}

impl MgmtHdr {
    /// Returns the length in bytes of a mgmt header including all its fixed and optional
    /// fields (if they are present).
    pub fn len(has_ht_ctrl: Presence<HtControl>) -> usize {
        let mut bytes = std::mem::size_of::<DataHdr>();
        bytes += match has_ht_ctrl {
            HtControl::PRESENT => std::mem::size_of::<HtControl>(),
            HtControl::ABSENT => 0,
        };
        bytes
    }
}

pub type Addr4 = MacAddr;

// IEEE Std 802.11-2016, 9.3.2.1
#[derive(FromBytes, AsBytes, Unaligned, PartialEq, Eq, Clone, Copy, Debug)]
#[repr(C, packed)]
pub struct DataHdr {
    pub frame_ctrl: FrameControl,
    pub duration: u16,
    pub addr1: MacAddr,
    pub addr2: MacAddr,
    pub addr3: MacAddr,
    pub seq_ctrl: SequenceControl,
}

impl DataHdr {
    /// Returns the length in bytes of a data header including all its fixed and optional
    /// fields (if they are present).
    pub fn len(
        has_addr4: Presence<Addr4>,
        has_qos_ctrl: Presence<QosControl>,
        has_ht_ctrl: Presence<HtControl>,
    ) -> usize {
        let mut bytes = std::mem::size_of::<DataHdr>();
        bytes += match has_addr4 {
            Addr4::PRESENT => std::mem::size_of::<MacAddr>(),
            Addr4::ABSENT => 0,
        };
        bytes += match has_qos_ctrl {
            QosControl::PRESENT => std::mem::size_of::<QosControl>(),
            QosControl::ABSENT => 0,
        };
        bytes += match has_ht_ctrl {
            HtControl::PRESENT => std::mem::size_of::<HtControl>(),
            HtControl::ABSENT => 0,
        };
        bytes
    }
}

// IEEE Std 802.11-2016, Table 9-26 defines DA, SA, RA, TA, BSSID
pub fn data_dst_addr(hdr: &DataHdr) -> MacAddr {
    let fc = hdr.frame_ctrl;
    if fc.to_ds() {
        hdr.addr3
    } else {
        hdr.addr1
    }
}

pub fn data_src_addr(hdr: &DataHdr, addr4: Option<MacAddr>) -> Option<MacAddr> {
    let fc = hdr.frame_ctrl;
    match (fc.to_ds(), fc.from_ds()) {
        (_, false) => Some(hdr.addr2),
        (false, true) => Some(hdr.addr3),
        (true, true) => addr4,
    }
}

pub fn data_transmitter_addr(hdr: &DataHdr) -> MacAddr {
    hdr.addr2
}

pub fn data_receiver_addr(hdr: &DataHdr) -> MacAddr {
    hdr.addr1
}

/// BSSID: basic service set ID
pub fn data_bssid(hdr: &DataHdr) -> Option<MacAddr> {
    let fc = hdr.frame_ctrl;
    match (fc.to_ds(), fc.from_ds()) {
        (false, false) => Some(hdr.addr3),
        (false, true) => Some(hdr.addr2),
        (true, false) => Some(hdr.addr1),
        (true, true) => None,
    }
}

pub enum MacFrame<B> {
    Mgmt {
        // Management Header: fixed fields
        mgmt_hdr: LayoutVerified<B, MgmtHdr>,
        // Management Header: optional fields
        ht_ctrl: Option<UnalignedView<B, HtControl>>,
        // Body
        body: B,
    },
    Data {
        // Data Header: fixed fields
        data_hdr: LayoutVerified<B, DataHdr>,
        // Data Header: optional fields
        addr4: Option<LayoutVerified<B, Addr4>>,
        qos_ctrl: Option<UnalignedView<B, QosControl>>,
        ht_ctrl: Option<UnalignedView<B, HtControl>>,
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
        let mut reader = BufferReader::new(bytes);
        let fc = FrameControl(reader.peek_value()?);
        match fc.frame_type() {
            FRAME_TYPE_MGMT => {
                // Parse fixed header fields
                let mgmt_hdr = reader.read()?;

                // Parse optional header fields
                let ht_ctrl = if fc.htc_order() { Some(reader.read_unaligned()?) } else { None };
                // Skip optional padding if body alignment is used.
                if body_aligned {
                    let full_hdr_len =
                        MgmtHdr::len(Presence::<HtControl>::from_bool(ht_ctrl.is_some()));
                    skip_body_alignment_padding(full_hdr_len, &mut reader)?
                }
                Some(MacFrame::Mgmt { mgmt_hdr, ht_ctrl, body: reader.into_remaining() })
            }
            FRAME_TYPE_DATA => {
                // Parse fixed header fields
                let data_hdr = reader.read()?;

                // Parse optional header fields
                let addr4 = if fc.to_ds() && fc.from_ds() { Some(reader.read()?) } else { None };

                let qos_ctrl = if fc.frame_subtype() & BITMASK_QOS != 0 {
                    Some(reader.read_unaligned()?)
                } else {
                    None
                };

                let ht_ctrl = if fc.htc_order() { Some(reader.read_unaligned()?) } else { None };

                // Skip optional padding if body alignment is used.
                if body_aligned {
                    let full_hdr_len = DataHdr::len(
                        Presence::<Addr4>::from_bool(addr4.is_some()),
                        Presence::<QosControl>::from_bool(qos_ctrl.is_some()),
                        Presence::<HtControl>::from_bool(ht_ctrl.is_some()),
                    );
                    skip_body_alignment_padding(full_hdr_len, &mut reader)?
                };
                Some(MacFrame::Data {
                    data_hdr,
                    addr4,
                    qos_ctrl,
                    ht_ctrl,
                    body: reader.into_remaining(),
                })
            }
            type_ => Some(MacFrame::Unsupported { type_ }),
        }
    }
}

/// Skips optional padding required for body alignment.
fn skip_body_alignment_padding<B: ByteSlice>(
    hdr_len: usize,
    reader: &mut BufferReader<B>,
) -> Option<()> {
    const OPTIONAL_BODY_ALIGNMENT_BYTES: usize = 4;

    let padded_len = round_up(hdr_len, OPTIONAL_BODY_ALIGNMENT_BYTES);
    let padding = padded_len - hdr_len;
    reader.read_bytes(padding).map(|_| ())
}

// IEEE Std 802.11-2016, 9.3.3.3
#[derive(FromBytes, AsBytes, Unaligned)]
#[repr(C, packed)]
pub struct BeaconHdr {
    pub timestamp: [u8; 8],
    pub beacon_interval: [u8; 2],
    // IEEE Std 802.11-2016, 9.4.1.4
    pub capabilities: RawCapabilityInfo,
}

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

// IEEE Std 802.11-2016, 9.4.1.1
#[repr(u16)]
#[derive(Copy, Clone)]
pub enum AuthAlgorithm {
    Open = 0,
    _SharedKey = 1,
    _FastBssTransition = 2,
    Sae = 3,
    // 4-65534 Reserved
    _VendorSpecific = 65535,
}

/// IEEE Std 802.11-2016, 9.4.1.7
#[allow(unused)] // Some ReasonCodes are not used yet.
#[derive(Debug, PartialOrd, PartialEq, FromPrimitive, ToPrimitive)]
#[repr(C)]
pub enum ReasonCode {
    // 0 Reserved
    UnspecifiedReason = 1,
    InvalidAuthentication = 2,
    LeavingNetworkDeauth = 3,
    ReasonInactivity = 4,
    NoMoreStas = 5,
    InvalidClass2Frame = 6,
    InvalidClass3Frame = 7,
    LeavingNetworkDisassoc = 8,
    NotAuthenticated = 9,
    UnacceptablePowerCapability = 10,
    UnacceptableSupportedChannels = 11,
    BssTransitionDisassoc = 12,
    ReasonInvalidElement = 13,
    MicFailure = 14,
    FourwayHandshakeTimeout = 15,
    GkHandshakeTimeout = 16,
    HandshakeElementMismatch = 17,
    ReasonInvalidGroupCipher = 18,
    ReasonInvalidPairwiseCipher = 19,
    ReasonInvalidAkmp = 20,
    UnsupportedRsneVersion = 21,
    InvalidRsneCapabilities = 22,
    Ieee8021XAuthFailed = 23,
    ReasonCipherOutOfPolicy = 24,
    TdlsPeerUnreachable = 25,
    TdlsUnspecifiedReason = 26,
    SspRequestedDisassoc = 27,
    NoSspRoamingAgreement = 28,
    BadCipherOrAkm = 29,
    NotAuthorizedThisLocation = 30,
    ServiceChangePrecludesTs = 31,
    UnspecifiedQosReason = 32,
    NotEnoughBandwidth = 33,
    MissingAcks = 34,
    ExceededTxop = 35,
    StaLeaving = 36,
    EndTsBaDls = 37,
    UnknownTsBa = 38,
    Timeout = 39,
    // 40 - 44 Reserved.
    PeerkeyMismatch = 45,
    PeerInitiated = 46,
    ApInitiated = 47,
    ReasonInvalidFtActionFrameCount = 48,
    ReasonInvalidPmkid = 49,
    ReasonInvalidMde = 50,
    ReasonInvalidFte = 51,
    MeshPeeringCanceled = 52,
    MeshMaxPeers = 53,
    MeshConfigurationPolicyViolation = 54,
    MeshCloseRcvd = 55,
    MeshMaxRetries = 56,
    MeshConfirmTimeout = 57,
    MeshInvalidGtk = 58,
    MeshInconsistentParameters = 59,
    MeshInvalidSecurityCapability = 60,
    MeshPathErrorNoProxyInformation = 61,
    MeshPathErrorNoForwardingInformation = 62,
    MeshPathErrorDestinationUnreachable = 63,
    MacAddressAlreadyExistsInMbss = 64,
    MeshChannelSwitchRegulatoryRequirements = 65,
    MeshChannelSwitchUnspecified = 66,
    // 67-65535 Reserved
}

// IEEE Std 802.11-2016, 9.3.3.12
#[derive(Default, FromBytes, AsBytes, Unaligned)]
#[repr(C, packed)]
pub struct AuthHdr {
    pub auth_alg_num: [u8; 2],
    pub auth_txn_seq_num: [u8; 2],
    pub status_code: [u8; 2],
}

impl AuthHdr {
    pub fn auth_alg_num(&self) -> u16 {
        LittleEndian::read_u16(&self.auth_alg_num)
    }

    pub fn set_auth_alg_num(&mut self, val: u16) {
        LittleEndian::write_u16(&mut self.auth_alg_num, val)
    }

    pub fn auth_txn_seq_num(&self) -> u16 {
        LittleEndian::read_u16(&self.auth_txn_seq_num)
    }

    pub fn set_auth_txn_seq_num(&mut self, val: u16) {
        LittleEndian::write_u16(&mut self.auth_txn_seq_num, val)
    }

    pub fn status_code(&self) -> u16 {
        LittleEndian::read_u16(&self.status_code)
    }

    pub fn set_status_code(&mut self, val: u16) {
        LittleEndian::write_u16(&mut self.status_code, val)
    }
}

// IEEE Std 802.11-2016, 9.3.3.13
#[derive(Default, FromBytes, AsBytes, Unaligned)]
#[repr(C, packed)]
pub struct DeauthHdr {
    pub reason_code: [u8; 2],
}

impl DeauthHdr {
    pub fn reason_code(&self) -> u16 {
        LittleEndian::read_u16(&self.reason_code)
    }

    pub fn set_reason_code(&mut self, val: u16) {
        LittleEndian::write_u16(&mut self.reason_code, val)
    }
}

// IEEE Std 802.11-2016, 9.3.3.6
#[derive(FromBytes, AsBytes, Unaligned)]
#[repr(C, packed)]
pub struct AssocRespHdr {
    // IEEE Std 802.11-2016, 9.4.1.4
    pub capabilities: [u8; 2],
    pub status_code: [u8; 2],
    pub aid: [u8; 2],
}

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
    Beacon { bcn_hdr: LayoutVerified<B, BeaconHdr>, elements: B },
    Authentication { auth_hdr: LayoutVerified<B, AuthHdr>, elements: B },
    AssociationResp { assoc_resp_hdr: LayoutVerified<B, AssocRespHdr>, elements: B },
    Unsupported { subtype: u16 },
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
#[derive(FromBytes, AsBytes, Unaligned)]
#[repr(C, packed)]
#[derive(Default)]
pub struct LlcHdr {
    pub dsap: u8,
    pub ssap: u8,
    pub control: u8,
    pub oui: [u8; 3],
    pub protocol_id_be: [u8; 2], // In network byte order (big endian).
}

impl LlcHdr {
    pub fn protocol_id(&self) -> u16 {
        BigEndian::read_u16(&self.protocol_id_be)
    }

    pub fn set_protocol_id(&mut self, val: u16) {
        BigEndian::write_u16(&mut self.protocol_id_be, val);
    }
}

pub struct LlcFrame<B> {
    pub hdr: LayoutVerified<B, LlcHdr>,
    pub body: B,
}

/// An LLC frame is only valid if it contains enough bytes for header AND at least 1 byte for body
impl<B: ByteSlice> LlcFrame<B> {
    pub fn parse(bytes: B) -> Option<Self> {
        let (hdr, body) = LayoutVerified::new_unaligned_from_prefix(bytes)?;
        if body.is_empty() {
            None
        } else {
            Some(Self { hdr, body })
        }
    }
}

// IEEE Std 802.3-2015, 3.1.1
#[derive(FromBytes, AsBytes, Unaligned)]
#[repr(C, packed)]
pub struct EthernetIIHdr {
    pub da: MacAddr,
    pub sa: MacAddr,
    pub ether_type_be: [u8; 2], // In network byte order (big endian).
}

impl EthernetIIHdr {
    pub fn ether_type(&self) -> u16 {
        BigEndian::read_u16(&self.ether_type_be)
    }
    pub fn set_ether_type(&mut self, val: u16) {
        BigEndian::write_u16(&mut self.ether_type_be, val)
    }
}

// IEEE Std 802.11-2016, 9.3.2.2.2
#[derive(FromBytes, AsBytes, Unaligned)]
#[repr(C, packed)]
pub struct AmsduSubframeHdr {
    // Note this is the same as the IEEE 802.3 frame format.
    pub da: MacAddr,
    pub sa: MacAddr,
    pub msdu_len_be: [u8; 2], // In network byte order (big endian).
}

impl AmsduSubframeHdr {
    pub fn msdu_len(&self) -> u16 {
        BigEndian::read_u16(&self.msdu_len_be)
    }
}

pub struct AmsduSubframe<B> {
    pub hdr: LayoutVerified<B, AmsduSubframeHdr>,
    pub body: B,
}

/// Parse an A-MSDU subframe from the byte stream and advance the cursor in the `BufferReader` if
/// successful. Parsing is only successful if the byte stream starts with a valid subframe.
/// TODO(WLAN-995): The received AMSDU should not be greater than `max_amsdu_len`, specified in
/// HtCapabilities IE of Association. Warn or discard if violated.
impl<B: ByteSlice> AmsduSubframe<B> {
    pub fn parse(buffer_reader: &mut BufferReader<B>) -> Option<Self> {
        let hdr = buffer_reader.read::<AmsduSubframeHdr>()?;
        let msdu_len = hdr.msdu_len() as usize;
        if buffer_reader.bytes_remaining() < msdu_len {
            None
        } else {
            let body = buffer_reader.read_bytes(msdu_len)?;
            let base_len = std::mem::size_of::<AmsduSubframeHdr>() + msdu_len;
            let padded_len = round_up(base_len, 4);
            let padding_len = padded_len - base_len;
            if buffer_reader.bytes_remaining() == 0 {
                Some(Self { hdr, body })
            } else if buffer_reader.bytes_remaining() <= padding_len {
                // The subframe is invalid if EITHER one of the following is true
                // a) there are not enough bytes in the buffer for padding
                // b) the remaining buffer only contains padding bytes
                // IEEE 802.11-2016 9.3.2.2.2 `The last A-MSDU subframe has no padding.`
                None
            } else {
                buffer_reader.read_bytes(padding_len)?;
                Some(Self { hdr, body })
            }
        }
    }
}

pub enum DataFrameBody<B> {
    Llc { llc_frame: B },
    Amsdu { amsdu: B },
}

pub struct Msdu<B> {
    pub dst_addr: MacAddr,
    pub src_addr: MacAddr,
    pub llc_frame: LlcFrame<B>,
}

pub enum MsduIterator<B> {
    Llc { dst_addr: MacAddr, src_addr: MacAddr, body: Option<B> },
    Amsdu(BufferReader<B>),
}

impl<B: ByteSlice> Iterator for MsduIterator<B> {
    type Item = Msdu<B>;
    fn next(&mut self) -> Option<Self::Item> {
        match self {
            MsduIterator::Llc { dst_addr, src_addr, body } => {
                let body = body.take()?;
                let llc_frame = LlcFrame::parse(body)?;
                Some(Msdu { dst_addr: *dst_addr, src_addr: *src_addr, llc_frame })
            }
            MsduIterator::Amsdu(reader) => {
                let AmsduSubframe { hdr, body } = AmsduSubframe::parse(reader)?;
                let llc_frame = LlcFrame::parse(body)?;
                Some(Msdu { dst_addr: hdr.da, src_addr: hdr.sa, llc_frame })
            }
        }
    }
}

pub enum DataSubtype<B> {
    // QoS or regular data type.
    Data(DataFrameBody<B>),
    Unsupported { subtype: u16 },
}

impl<B: ByteSlice> DataSubtype<B> {
    pub fn parse(subtype: u16, qos_ctrl: Option<QosControl>, bytes: B) -> Option<DataSubtype<B>> {
        Some(match subtype {
            DATA_SUBTYPE_DATA => DataSubtype::Data(DataFrameBody::Llc { llc_frame: bytes }),
            DATA_SUBTYPE_QOS_DATA => {
                if qos_ctrl?.amsdu_present() {
                    DataSubtype::Data(DataFrameBody::Amsdu { amsdu: bytes })
                } else {
                    DataSubtype::Data(DataFrameBody::Llc { llc_frame: bytes })
                }
            }
            subtype => DataSubtype::Unsupported { subtype },
        })
    }
}

impl<B: ByteSlice> MsduIterator<B> {
    /// If `body_aligned` is |true| the frame's body is expected to be 4 byte aligned.
    pub fn from_raw_data_frame(data_frame: B, body_aligned: bool) -> Option<MsduIterator<B>> {
        match MacFrame::parse(data_frame, body_aligned)? {
            MacFrame::Data { data_hdr, addr4, qos_ctrl, body, .. } => {
                let fc = data_hdr.frame_ctrl;
                match DataSubtype::parse(fc.frame_subtype(), qos_ctrl.map(|x| x.get()), body)? {
                    DataSubtype::Data(DataFrameBody::Llc { llc_frame }) => {
                        Some(MsduIterator::Llc {
                            dst_addr: data_dst_addr(&data_hdr),
                            // Safe to unwrap because data frame parsing has been successful.
                            src_addr: data_src_addr(&data_hdr, addr4.map(|a| *a)).unwrap(),
                            body: Some(llc_frame),
                        })
                    }
                    DataSubtype::Data(DataFrameBody::Amsdu { amsdu }) => {
                        Some(MsduIterator::Amsdu(BufferReader::new(amsdu)))
                    }
                    _ => None,
                }
            }
            _ => None,
        }
    }
}
/// IEEE Std 802.11-2016, 9.4.1.9, Table 9-46
#[allow(unused)] // Some StatusCodes are not used yet.
#[derive(Debug, PartialOrd, PartialEq, FromPrimitive, ToPrimitive)]
#[repr(C)]
pub enum StatusCode {
    Success = 0,
    Refused = 1,
    TdlsRejectedAlternativeProvided = 2,
    TdlsRejected = 3,
    // 4 Reserved
    SecurityDisabled = 5,
    UnacceptableLifetime = 6,
    NotInSameBss = 7,
    // 8-9 Reserved
    RefusedCapabilitiesMismatch = 10,
    DeniedNoAssociationExists = 11,
    DeniedOtherReason = 12,
    UnsupportedAuthAlgorithm = 13,
    TransactionSequenceError = 14,
    ChallengeFailure = 15,
    RejectedSequenceTimeout = 16,
    DeniedNoMoreStas = 17,
    RefusedBasicRatesMismatch = 18,
    DeniedNoShortPreambleSupport = 19,
    // 20-21 Reserved
    RejectedSpectrumManagementRequired = 22,
    RejectedBadPowerCapability = 23,
    RejectedBadSupportedChannels = 24,
    DeniedNoShortSlotTimeSupport = 25,
    // 26 Reserved
    DeniedNoHtSupport = 27,
    R0khUnreachable = 28,
    DeniedPcoTimeNotSupported = 29,
    RefusedTemporarily = 30,
    RobustManagementPolicyViolation = 31,
    UnspecifiedQosFailure = 32,
    DeniedInsufficientBandwidth = 33,
    DeniedPoorChannelConditions = 34,
    DeniedQosNotSupported = 35,
    // 36 Reserved
    RequestDeclined = 37,
    InvalidParameters = 38,
    RejectedWithSuggestedChanges = 39,
    StatusInvalidElement = 40,
    StatusInvalidGroupCipher = 41,
    StatusInvalidPairwiseCipher = 42,
    StatusInvalidAkmp = 43,
    UnsupportedRsneVersion = 44,
    InvalidRsneCapabilities = 45,
    StatusCipherOutOfPolicy = 46,
    RejectedForDelayPeriod = 47,
    DlsNotAllowed = 48,
    NotPresent = 49,
    NotQosSta = 50,
    DeniedListenIntervalTooLarge = 51,
    StatusInvalidFtActionFrameCount = 52,
    StatusInvalidPmkid = 53,
    StatusInvalidMde = 54,
    StatusInvalidFte = 55,
    RequestedTclasNotSupported56 = 56, // see RequestedTclasNotSupported80 below
    InsufficientTclasProcessingResources = 57,
    TryAnotherBss = 58,
    GasAdvertisementProtocolNotSupported = 59,
    NoOutstandingGasRequest = 60,
    GasResponseNotReceivedFromServer = 61,
    GasQueryTimeout = 62,
    GasQueryResponseTooLarge = 63,
    RejectedHomeWithSuggestedChanges = 64,
    ServerUnreachable = 65,
    // 66 Reserved
    RejectedForSspPermissions = 67,
    RefusedUnauthenticatedAccessNotSupported = 68,
    // 69-71 Reserved
    InvalidRsne = 72,
    UApsdCoexistanceNotSupported = 73,
    UApsdCoexModeNotSupported = 74,
    BadIntervalWithUApsdCoex = 75,
    AntiCloggingTokenRequired = 76,
    UnsupportedFiniteCyclicGroup = 77,
    CannotFindAlternativeTbtt = 78,
    TransmissionFailure = 79,
    RequestedTclasNotSupported80 = 80, // see RequestedTclasNotSupported56 above
    TclasResourcesExhausted = 81,
    RejectedWithSuggestedBssTransition = 82,
    RejectWithSchedule = 83,
    RejectNoWakeupSpecified = 84,
    SuccessPowerSaveMode = 85,
    PendingAdmittingFstSession = 86,
    PerformingFstNow = 87,
    PendingGapInBaWindow = 88,
    RejectUPidSetting = 89,
    // 90-91 Reserved
    RefusedExternalReason = 92,
    RefusedApOutOfMemory = 93,
    RejectedEmergencyServicesNotSupported = 94,
    QueryResponseOutstanding = 95,
    RejectDseBand = 96,
    TclasProcessingTerminated = 97,
    TsScheduleConflict = 98,
    DeniedWithSuggestedBandAndChannel = 99,
    MccaopReservationConflict = 100,
    MafLimitExceeded = 101,
    MccaTrackLimitExceeded = 102,
    DeniedDueToSpectrumManagement = 103,
    DeniedVhtNotSupported = 104,
    EnablementDenied = 105,
    RestrictionFromAuthorizedGdb = 106,
    AuthorizationDeenabled = 107,
    // 108-65535 Reserved
}

fn round_up<T: Unsigned + Copy>(value: T, multiple: T) -> T {
    let overshoot = value + multiple - T::one();
    overshoot - overshoot % multiple
}

#[cfg(test)]
mod tests {
    use {super::*, crate::test_utils::fake_frames::*};

    #[test]
    fn mgmt_hdr_len() {
        assert_eq!(MgmtHdr::len(HtControl::ABSENT), 24);
        assert_eq!(MgmtHdr::len(HtControl::PRESENT), 28);
    }

    #[test]
    fn data_hdr_len() {
        assert_eq!(DataHdr::len(Addr4::ABSENT, QosControl::ABSENT, HtControl::ABSENT), 24);
        assert_eq!(DataHdr::len(Addr4::PRESENT, QosControl::ABSENT, HtControl::ABSENT), 30);
        assert_eq!(DataHdr::len(Addr4::ABSENT, QosControl::PRESENT, HtControl::ABSENT), 26);
        assert_eq!(DataHdr::len(Addr4::ABSENT, QosControl::ABSENT, HtControl::PRESENT), 28);
        assert_eq!(DataHdr::len(Addr4::PRESENT, QosControl::PRESENT, HtControl::ABSENT), 32);
        assert_eq!(DataHdr::len(Addr4::ABSENT, QosControl::PRESENT, HtControl::PRESENT), 30);
        assert_eq!(DataHdr::len(Addr4::PRESENT, QosControl::ABSENT, HtControl::PRESENT), 34);
        assert_eq!(DataHdr::len(Addr4::PRESENT, QosControl::PRESENT, HtControl::PRESENT), 36);
    }

    #[test]
    fn parse_mgmt_frame() {
        let bytes = make_mgmt_frame(false);
        match MacFrame::parse(&bytes[..], false) {
            Some(MacFrame::Mgmt { mgmt_hdr, ht_ctrl, body }) => {
                assert_eq!(0x0101, { mgmt_hdr.frame_ctrl.0 });
                assert_eq!(0x0202, { mgmt_hdr.duration });
                assert_eq!([3, 3, 3, 3, 3, 3], mgmt_hdr.addr1);
                assert_eq!([4, 4, 4, 4, 4, 4], mgmt_hdr.addr2);
                assert_eq!([5, 5, 5, 5, 5, 5], mgmt_hdr.addr3);
                assert_eq!(0x0606, { mgmt_hdr.seq_ctrl.0 });
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
        #[rustfmt::skip]
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
        let bytes = make_data_frame_single_llc(None, None);
        match MacFrame::parse(&bytes[..], false) {
            Some(MacFrame::Data { data_hdr, addr4, qos_ctrl, ht_ctrl, body }) => {
                assert_eq!(0b00000000_10001000, { data_hdr.frame_ctrl.0 });
                assert_eq!(0x0202, { data_hdr.duration });
                assert_eq!([3, 3, 3, 3, 3, 3], data_hdr.addr1);
                assert_eq!([4, 4, 4, 4, 4, 4], data_hdr.addr2);
                assert_eq!([5, 5, 5, 5, 5, 5], data_hdr.addr3);
                assert_eq!(0x0606, { data_hdr.seq_ctrl.0 });
                assert!(addr4.is_none());
                match qos_ctrl {
                    None => panic!("qos_ctrl expected to be present"),
                    Some(qos_ctrl) => {
                        assert_eq!(0x0101, qos_ctrl.get().0);
                    }
                };
                assert!(ht_ctrl.is_none());
                assert_eq!(&body[..], &[7, 7, 7, 8, 8, 8, 9, 10, 11, 11, 11]);
            }
            _ => panic!("failed parsing data frame"),
        };
    }

    #[test]
    fn msdu_iterator_single_llc() {
        let bytes = make_data_frame_single_llc(None, None);
        let msdus = MsduIterator::from_raw_data_frame(&bytes[..], false);
        assert!(msdus.is_some());
        let mut found_msdu = false;
        for Msdu { dst_addr, src_addr, llc_frame } in msdus.unwrap() {
            if found_msdu {
                panic!("unexpected MSDU: {:x?}", llc_frame.body);
            }
            assert_eq!(dst_addr, [3; 6]);
            assert_eq!(src_addr, [4; 6]);
            assert_eq!(llc_frame.hdr.protocol_id(), 9 << 8 | 10);
            assert_eq!(llc_frame.body, [11; 3]);
            found_msdu = true;
        }
        assert!(found_msdu);
    }

    #[test]
    fn parse_data_frame_with_padding() {
        let bytes = make_data_frame_with_padding();
        match MacFrame::parse(&bytes[..], true) {
            Some(MacFrame::Data { qos_ctrl, body, .. }) => {
                assert_eq!(0x0101, qos_ctrl.expect("qos_ctrl not present").get().0);
                assert_eq!(&[7, 7, 7, 8, 8, 8, 9, 10, 11, 11, 11, 11, 11], &body[..]);
            }
            _ => panic!("failed parsing data frame"),
        };
    }

    #[test]
    fn msdu_iterator_single_llc_padding() {
        let bytes = make_data_frame_with_padding();
        let msdus = MsduIterator::from_raw_data_frame(&bytes[..], true);
        assert!(msdus.is_some());
        let mut found_msdu = false;
        for Msdu { dst_addr, src_addr, llc_frame } in msdus.unwrap() {
            if found_msdu {
                panic!("unexpected MSDU: {:x?}", llc_frame.body);
            }
            assert_eq!(dst_addr, [3; 6]);
            assert_eq!(src_addr, [4; 6]);
            assert_eq!(llc_frame.hdr.protocol_id(), 9 << 8 | 10);
            assert_eq!(llc_frame.body, [11; 5]);
            found_msdu = true;
        }
        assert!(found_msdu);
    }

    #[test]
    fn parse_llc_with_addr4_ht_ctrl() {
        let bytes = make_data_frame_single_llc(Some([1, 2, 3, 4, 5, 6]), Some([4, 3, 2, 1]));
        match MacFrame::parse(&bytes[..], false) {
            Some(MacFrame::Data { data_hdr, qos_ctrl, body, .. }) => {
                let fc = data_hdr.frame_ctrl;
                match DataSubtype::parse(fc.frame_subtype(), qos_ctrl.map(|x| x.get()), &body[..]) {
                    Some(DataSubtype::Data(DataFrameBody::Llc { llc_frame })) => {
                        let llc = LlcFrame::parse(llc_frame).expect("LLC frame too short");
                        assert_eq!(7, llc.hdr.dsap);
                        assert_eq!(7, llc.hdr.ssap);
                        assert_eq!(7, llc.hdr.control);
                        assert_eq!([8, 8, 8], llc.hdr.oui);
                        assert_eq!([9, 10], llc.hdr.protocol_id_be);
                        assert_eq!(0x090A, llc.hdr.protocol_id());
                        assert_eq!(&[11, 11, 11], llc.body);
                    }
                    _ => panic!("failed parsing LLC"),
                }
            }
            _ => panic!("failed parsing data frame"),
        };
    }

    #[test]
    fn test_llc_set_protocol_id() {
        let mut hdr: LlcHdr = Default::default();
        hdr.set_protocol_id(0xAABB);
        assert_eq!([0xAA, 0xBB], hdr.protocol_id_be);
        assert_eq!(0xAABB, hdr.protocol_id());
    }

    #[test]
    fn parse_data_amsdu() {
        let amsdu_data_frame = make_data_frame_amsdu();

        let msdus = MsduIterator::from_raw_data_frame(&amsdu_data_frame[..], false);
        assert!(msdus.is_some());
        let mut found_msdus = (false, false);
        for Msdu { dst_addr, src_addr, llc_frame } in msdus.unwrap() {
            match found_msdus {
                (false, false) => {
                    assert_eq!(dst_addr, [0x78, 0x8a, 0x20, 0x0d, 0x67, 0x03]);
                    assert_eq!(src_addr, [0xb4, 0xf7, 0xa1, 0xbe, 0xb9, 0xab]);
                    assert_eq!(llc_frame.hdr.protocol_id(), 0x0800);
                    assert_eq!(llc_frame.body, MSDU_1_PAYLOAD);
                    found_msdus = (true, false);
                }
                (true, false) => {
                    assert_eq!(dst_addr, [0x78, 0x8a, 0x20, 0x0d, 0x67, 0x04]);
                    assert_eq!(src_addr, [0xb4, 0xf7, 0xa1, 0xbe, 0xb9, 0xac]);
                    assert_eq!(llc_frame.hdr.protocol_id(), 0x0801);
                    assert_eq!(llc_frame.body, MSDU_2_PAYLOAD);
                    found_msdus = (true, true);
                }
                _ => panic!("unexepcted MSDU: {:x?}", llc_frame.body),
            }
        }
        assert_eq!(found_msdus, (true, true));
    }

    #[test]
    fn parse_data_amsdu_padding_too_short() {
        let amsdu_data_frame = make_data_frame_amsdu_padding_too_short();

        let msdus = MsduIterator::from_raw_data_frame(&amsdu_data_frame[..], false);
        assert!(msdus.is_some());
        let mut found_one_msdu = false;
        for Msdu { dst_addr, src_addr, llc_frame } in msdus.unwrap() {
            assert!(!found_one_msdu);
            assert_eq!(dst_addr, [0x78, 0x8a, 0x20, 0x0d, 0x67, 0x03]);
            assert_eq!(src_addr, [0xb4, 0xf7, 0xa1, 0xbe, 0xb9, 0xab]);
            assert_eq!(llc_frame.hdr.protocol_id(), 0x0800);
            assert_eq!(llc_frame.body, MSDU_1_PAYLOAD);
            found_one_msdu = true;
        }
        assert!(found_one_msdu);
    }

    #[test]
    fn eth_hdr_big_endian() {
        let mut bytes: Vec<u8> = vec![
            1, 2, 3, 4, 5, 6, // dst_addr
            7, 8, 9, 10, 11, 12, // src_addr
            13, 14, // ether_type
            99, 99, // trailing bytes
        ];
        let (mut hdr, body) =
            LayoutVerified::<_, EthernetIIHdr>::new_unaligned_from_prefix(&mut bytes[..])
                .expect("cannot create ethernet header.");
        assert_eq!(hdr.da, [1u8, 2, 3, 4, 5, 6]);
        assert_eq!(hdr.sa, [7u8, 8, 9, 10, 11, 12]);
        assert_eq!(hdr.ether_type(), 13 << 8 | 14);
        assert_eq!(hdr.ether_type_be, [13u8, 14]);
        assert_eq!(body, [99, 99]);

        hdr.set_ether_type(0x888e);
        assert_eq!(hdr.ether_type_be, [0x88, 0x8e]);
        #[rustfmt::skip]
        assert_eq!(
            &[1u8, 2, 3, 4, 5, 6,
            7, 8, 9, 10, 11, 12,
            0x88, 0x8e,
            99, 99],
            &bytes[..]);
    }

    #[test]
    fn data_hdr_dst_addr() {
        let mut data_hdr = make_data_hdr(None, [0, 0], None);
        let (mut data_hdr, _) =
            LayoutVerified::<_, DataHdr>::new_unaligned_from_prefix(&mut data_hdr[..])
                .expect("invalid data header");
        let mut fc = FrameControl(0);
        fc.set_to_ds(true);
        data_hdr.frame_ctrl = fc;
        assert_eq!(data_dst_addr(&data_hdr), [5; 6]); // Addr3
        fc.set_to_ds(false);
        data_hdr.frame_ctrl = fc;
        assert_eq!(data_dst_addr(&data_hdr), [3; 6]); // Addr1
    }

    #[test]
    fn data_hdr_src_addr() {
        let mut data_hdr = make_data_hdr(None, [0, 0], None);
        let (mut data_hdr, _) =
            LayoutVerified::<_, DataHdr>::new_unaligned_from_prefix(&mut data_hdr[..])
                .expect("invalid data header");
        let mut fc = FrameControl(0);
        // to_ds == false && from_ds == false
        data_hdr.frame_ctrl = fc;
        assert_eq!(data_src_addr(&data_hdr, None), Some([4; 6])); // Addr2

        fc.set_to_ds(true);
        // to_ds == true && from_ds == false
        data_hdr.frame_ctrl = fc;
        assert_eq!(data_src_addr(&data_hdr, None), Some([4; 6])); // Addr2

        fc.set_from_ds(true);
        // to_ds == true && from_ds == true;
        data_hdr.frame_ctrl = fc;
        assert_eq!(data_src_addr(&data_hdr, Some([11; 6])), Some([11; 6])); // Addr4

        fc.set_to_ds(false);
        // to_ds == false && from_ds == true;
        data_hdr.frame_ctrl = fc;
        assert_eq!(data_src_addr(&data_hdr, None), Some([5; 6])); // Addr3
    }

    #[test]
    fn data_hdr_ta() {
        let mut data_hdr = make_data_hdr(None, [0, 0], None);
        let (data_hdr, _) =
            LayoutVerified::<_, DataHdr>::new_unaligned_from_prefix(&mut data_hdr[..])
                .expect("invalid data header");
        assert_eq!(data_transmitter_addr(&data_hdr), [4; 6]); // Addr2
    }

    #[test]
    fn data_hdr_ra() {
        let mut data_hdr = make_data_hdr(None, [0, 0], None);
        let (data_hdr, _) =
            LayoutVerified::<_, DataHdr>::new_unaligned_from_prefix(&mut data_hdr[..])
                .expect("invalid data header");
        assert_eq!(data_receiver_addr(&data_hdr), [3; 6]); // Addr2
    }

    #[test]
    fn data_hdr_bssid() {
        let mut data_hdr = make_data_hdr(None, [0, 0], None);
        let (mut data_hdr, _) =
            LayoutVerified::<_, DataHdr>::new_unaligned_from_prefix(&mut data_hdr[..])
                .expect("invalid data header");
        let mut fc = FrameControl(0);
        // to_ds == false && from_ds == false
        data_hdr.frame_ctrl = fc;
        assert_eq!(data_bssid(&data_hdr), Some([5; 6])); // Addr3

        fc.set_to_ds(true);
        // to_ds == true && from_ds == false
        data_hdr.frame_ctrl = fc;
        assert_eq!(data_bssid(&data_hdr), Some([3; 6])); // Addr1

        fc.set_from_ds(true);
        // to_ds == true && from_ds == true;
        data_hdr.frame_ctrl = fc;
        assert_eq!(data_bssid(&data_hdr), None);

        fc.set_to_ds(false);
        // to_ds == false && from_ds == true;
        data_hdr.frame_ctrl = fc;
        assert_eq!(data_bssid(&data_hdr), Some([4; 6])); // Addr2
    }

    #[test]
    fn round_up_to_4() {
        assert_eq!(0, round_up(0u32, 4));
        assert_eq!(4, round_up(1u32, 4));
        assert_eq!(4, round_up(2u32, 4));
        assert_eq!(4, round_up(3u32, 4));
        assert_eq!(4, round_up(4u32, 4));
        assert_eq!(8, round_up(5u32, 4));
    }
}
