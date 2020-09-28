// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    bt_avctp::{pub_decodable_enum, AvcCommandType},
    fidl_fuchsia_bluetooth_avrcp as fidl_avrcp,
    std::{convert::TryFrom, result},
    thiserror::Error,
};

mod browsing;
mod continuation;
mod get_capabilities;
mod get_element_attributes;
mod get_play_status;
mod notification;
pub mod player_application_settings;
mod rejected;
mod set_absolute_volume;

pub use {
    self::browsing::*, self::continuation::*, self::get_capabilities::*,
    self::get_element_attributes::*, self::get_play_status::*, self::notification::*,
    self::player_application_settings::*, self::rejected::*, self::set_absolute_volume::*,
};

/// The error types for packet parsing.
#[derive(Error, Debug, PartialEq)]
pub enum Error {
    /// The value that was sent on the wire was out of range.
    #[error("Value was out of range")]
    InvalidMessageLength,

    /// A specific parameter ID was not understood.
    /// This specifically includes:
    /// PDU ID, Capability ID, Event ID, Player Application Setting Attribute ID, Player Application
    /// Setting Value ID, and Element Attribute ID
    #[error("Unrecognized parameter id for a message")]
    InvalidParameter,

    /// The body format was invalid.
    #[error("Failed to parse message contents")]
    InvalidMessage,

    /// A message couldn't be encoded. Passed in buffer was too short.
    #[error("Encoding buffer too small")]
    BufferLengthOutOfRange,

    /// A message couldn't be encoded. Logical error with parameters.
    #[error("Encountered an error encoding a message")]
    ParameterEncodingError,

    /// A enum value is out of expected range
    #[error("Value is out of expected range for enum")]
    OutOfRange,

    #[doc(hidden)]
    #[error("__Nonexhaustive error should never be created.")]
    __Nonexhaustive,
}

pub type PacketResult<T> = result::Result<T, Error>;

pub_decodable_enum! {
    /// Common charset IDs from the MIB Enum we may see in AVRCP. See:
    /// https://www.iana.org/assignments/character-sets/character-sets.xhtml
    CharsetId<u16, Error, OutOfRange> {
        Ascii => 3,
        Iso8859_1 => 4,
        Utf8 => 106,
        Ucs2 => 1000,
        Utf16be => 1013,
        Utf16le => 1014,
        Utf16 => 1015,
    }
}

/// The size, in bytes, of an attributes id.
pub const ATTRIBUTE_ID_LEN: usize = 4;

pub_decodable_enum! {
    MediaAttributeId<u8, Error, OutOfRange> {
        Title => 0x1,
        ArtistName => 0x2,
        AlbumName => 0x3,
        TrackNumber => 0x4,
        TotalNumberOfTracks => 0x5,
        Genre => 0x6,
        PlayingTime => 0x7,
        DefaultCoverArt => 0x8,
    }
}

pub_decodable_enum! {
    PduId<u8, Error, InvalidParameter> {
        GetCapabilities => 0x10,
        ListPlayerApplicationSettingAttributes => 0x11,
        ListPlayerApplicationSettingValues => 0x12,
        GetCurrentPlayerApplicationSettingValue => 0x13,
        SetPlayerApplicationSettingValue => 0x14,
        GetPlayerApplicationSettingAttributeText => 0x15,
        GetPlayerApplicationSettingValueText => 0x16,
        InformDisplayableCharacterSet => 0x17,
        InformBatteryStatusOfCT => 0x18,
        GetElementAttributes => 0x20,
        GetPlayStatus => 0x30,
        RegisterNotification => 0x31,
        RequestContinuingResponse => 0x40,
        AbortContinuingResponse => 0x41,
        SetAbsoluteVolume => 0x50,
        SetAddressedPlayer => 0x60,
        GetFolderItems => 0x71,
        PlayItem => 0x74,
        GetTotalNumberOfItems => 0x75,
        AddToNowPlaying => 0x90,
        GeneralReject => 0xa0,
    }
}

pub_decodable_enum! {
    PacketType<u8, Error, OutOfRange> {
        Single => 0b00,
        Start => 0b01,
        Continue => 0b10,
        Stop => 0b11,
    }
}

pub_decodable_enum! {
    StatusCode<u8, Error, OutOfRange> {
        InvalidCommand => 0x00,
        InvalidParameter => 0x01,
        ParameterContentError => 0x02,
        InternalError => 0x03,
        Success => 0x04,
        UidChanged => 0x05,
        InvalidScope => 0x0a,
        RangeOutOfBounds => 0x0b,
        InvalidPlayerId => 0x11,
        NoValidSearchResults => 0x14,
        NoAvailablePlayers => 0x15,
        AddressedPlayerChanged => 0x16,
    }
}

impl From<fidl_avrcp::TargetAvcError> for StatusCode {
    fn from(src: fidl_avrcp::TargetAvcError) -> StatusCode {
        match src {
            fidl_avrcp::TargetAvcError::RejectedInvalidCommand => StatusCode::InvalidCommand,
            fidl_avrcp::TargetAvcError::RejectedInvalidParameter => StatusCode::InvalidParameter,
            fidl_avrcp::TargetAvcError::RejectedParameterContentError => {
                StatusCode::ParameterContentError
            }
            fidl_avrcp::TargetAvcError::RejectedInternalError => StatusCode::InternalError,
            fidl_avrcp::TargetAvcError::RejectedUidChanged => StatusCode::UidChanged,
            fidl_avrcp::TargetAvcError::RejectedInvalidPlayerId => StatusCode::InvalidPlayerId,
            fidl_avrcp::TargetAvcError::RejectedNoAvailablePlayers => {
                StatusCode::NoAvailablePlayers
            }
            fidl_avrcp::TargetAvcError::RejectedAddressedPlayerChanged => {
                StatusCode::AddressedPlayerChanged
            }
        }
    }
}

// Shared by get_play_status and notification
pub_decodable_enum! {
    PlaybackStatus<u8, Error, OutOfRange> {
        Stopped => 0x00,
        Playing => 0x01,
        Paused => 0x02,
        FwdSeek => 0x03,
        RevSeek => 0x04,
        Error => 0xff,
    }
}

impl From<fidl_avrcp::PlaybackStatus> for PlaybackStatus {
    fn from(src: fidl_avrcp::PlaybackStatus) -> PlaybackStatus {
        match src {
            fidl_avrcp::PlaybackStatus::Stopped => PlaybackStatus::Stopped,
            fidl_avrcp::PlaybackStatus::Playing => PlaybackStatus::Playing,
            fidl_avrcp::PlaybackStatus::Paused => PlaybackStatus::Paused,
            fidl_avrcp::PlaybackStatus::FwdSeek => PlaybackStatus::FwdSeek,
            fidl_avrcp::PlaybackStatus::RevSeek => PlaybackStatus::RevSeek,
            fidl_avrcp::PlaybackStatus::Error => PlaybackStatus::Error,
        }
    }
}

impl From<PlaybackStatus> for fidl_avrcp::PlaybackStatus {
    fn from(src: PlaybackStatus) -> fidl_avrcp::PlaybackStatus {
        match src {
            PlaybackStatus::Stopped => fidl_avrcp::PlaybackStatus::Stopped,
            PlaybackStatus::Playing => fidl_avrcp::PlaybackStatus::Playing,
            PlaybackStatus::Paused => fidl_avrcp::PlaybackStatus::Paused,
            PlaybackStatus::FwdSeek => fidl_avrcp::PlaybackStatus::FwdSeek,
            PlaybackStatus::RevSeek => fidl_avrcp::PlaybackStatus::RevSeek,
            PlaybackStatus::Error => fidl_avrcp::PlaybackStatus::Error,
        }
    }
}

pub_decodable_enum! {
    PlayerApplicationSettingAttributeId<u8, Error, InvalidParameter> {
        Equalizer => 0x01,
        RepeatStatusMode => 0x02,
        ShuffleMode => 0x03,
        ScanMode => 0x04,
    }
}

impl From<fidl_avrcp::PlayerApplicationSettingAttributeId> for PlayerApplicationSettingAttributeId {
    fn from(
        src: fidl_avrcp::PlayerApplicationSettingAttributeId,
    ) -> PlayerApplicationSettingAttributeId {
        match src {
            fidl_avrcp::PlayerApplicationSettingAttributeId::Equalizer => {
                PlayerApplicationSettingAttributeId::Equalizer
            }
            fidl_avrcp::PlayerApplicationSettingAttributeId::RepeatStatusMode => {
                PlayerApplicationSettingAttributeId::RepeatStatusMode
            }
            fidl_avrcp::PlayerApplicationSettingAttributeId::ShuffleMode => {
                PlayerApplicationSettingAttributeId::ShuffleMode
            }
            fidl_avrcp::PlayerApplicationSettingAttributeId::ScanMode => {
                PlayerApplicationSettingAttributeId::ScanMode
            }
        }
    }
}

impl From<PlayerApplicationSettingAttributeId> for fidl_avrcp::PlayerApplicationSettingAttributeId {
    fn from(
        src: PlayerApplicationSettingAttributeId,
    ) -> fidl_avrcp::PlayerApplicationSettingAttributeId {
        match src {
            PlayerApplicationSettingAttributeId::Equalizer => {
                fidl_avrcp::PlayerApplicationSettingAttributeId::Equalizer
            }
            PlayerApplicationSettingAttributeId::RepeatStatusMode => {
                fidl_avrcp::PlayerApplicationSettingAttributeId::RepeatStatusMode
            }
            PlayerApplicationSettingAttributeId::ShuffleMode => {
                fidl_avrcp::PlayerApplicationSettingAttributeId::ShuffleMode
            }
            PlayerApplicationSettingAttributeId::ScanMode => {
                fidl_avrcp::PlayerApplicationSettingAttributeId::ScanMode
            }
        }
    }
}

// Copied from the AVCTP crate. They need to be local types so that the impls work.
/// A decodable type can be created from a byte buffer.
/// The type returned is separate (copied) from the buffer once decoded.
pub trait Decodable<E = Error>: Sized {
    /// Decodes into a new object, or returns an error.
    fn decode(buf: &[u8]) -> result::Result<Self, E>;
}

/// A encodable type can write itself into a byte buffer.
pub trait Encodable<E = Error> {
    /// Returns the number of bytes necessary to encode |self|
    fn encoded_len(&self) -> usize;

    /// Writes the encoded version of |self| at the start of |buf|
    /// |buf| must be at least size() length.
    fn encode(&self, buf: &mut [u8]) -> result::Result<(), E>;
}

/// The preamble at the start of all vendor dependent commands, responses, and rejections.
pub struct VendorDependentPreamble {
    pub pdu_id: u8,
    pub packet_type: PacketType,
    pub parameter_length: u16,
}

impl VendorDependentPreamble {
    pub fn new(
        pdu_id: u8,
        packet_type: PacketType,
        parameter_length: u16,
    ) -> VendorDependentPreamble {
        VendorDependentPreamble { pdu_id, packet_type, parameter_length }
    }

    pub fn new_single(pdu_id: u8, parameter_length: u16) -> VendorDependentPreamble {
        Self::new(pdu_id, PacketType::Single, parameter_length)
    }

    pub fn packet_type(&self) -> PacketType {
        self.packet_type
    }
}

impl Decodable for VendorDependentPreamble {
    fn decode(buf: &[u8]) -> PacketResult<Self> {
        if buf.len() < 4 {
            return Err(Error::InvalidMessage);
        }
        Ok(Self {
            pdu_id: buf[0],
            packet_type: PacketType::try_from(buf[1])?,
            parameter_length: ((buf[2] as u16) << 8) | buf[3] as u16,
        })
    }
}

impl Encodable for VendorDependentPreamble {
    fn encoded_len(&self) -> usize {
        4
    }

    fn encode(&self, buf: &mut [u8]) -> PacketResult<()> {
        buf[0] = self.pdu_id;
        buf[1] = u8::from(&self.packet_type);
        buf[2] = (self.parameter_length >> 8) as u8;
        buf[3] = (self.parameter_length & 0xff) as u8;
        Ok(())
    }
}

const AVC_PAYLOAD_SIZE: usize = 508; // 512 - 4 byte preamble

pub trait VendorDependentRawPdu {
    /// Protocol Data Unit type.
    fn raw_pdu_id(&self) -> u8;
}

pub trait VendorDependentPdu {
    fn pdu_id(&self) -> PduId;
}

impl<T: VendorDependentPdu> VendorDependentRawPdu for T {
    fn raw_pdu_id(&self) -> u8 {
        u8::from(&self.pdu_id())
    }
}

pub trait PacketEncodable {
    /// Encode packet for single command/response.
    fn encode_packet(&self) -> Result<Vec<u8>, Error>;

    /// Encode packet(s) and split at AVC 512 byte limit.
    /// For use with AVC.
    fn encode_packets(&self) -> Result<Vec<Vec<u8>>, Error>;
}

/// Provides methods to encode one or more vendor dependent packets with their preambles.
impl<T: VendorDependentRawPdu + Encodable> PacketEncodable for T {
    // This default trait impl is tested in rejected.rs.
    /// Encode packet for single command/response.
    fn encode_packet(&self) -> Result<Vec<u8>, Error> {
        let len = self.encoded_len();
        let preamble = VendorDependentPreamble::new_single(self.raw_pdu_id(), len as u16);
        let prelen = preamble.encoded_len();
        let mut buf = vec![0; len + prelen];
        preamble.encode(&mut buf[..])?;
        self.encode(&mut buf[prelen..])?;
        Ok(buf)
    }

    // This default trait impl is tested in get_element_attributes.rs.
    fn encode_packets(&self) -> Result<Vec<Vec<u8>>, Error> {
        let mut buf = vec![0; self.encoded_len()];
        self.encode(&mut buf[..])?;

        let mut payloads = vec![];
        let mut len_remaining = self.encoded_len();
        let mut packet_type =
            if len_remaining > AVC_PAYLOAD_SIZE { PacketType::Start } else { PacketType::Single };
        let mut offset = 0;

        loop {
            // length - preamble size
            let current_len =
                if len_remaining > AVC_PAYLOAD_SIZE { AVC_PAYLOAD_SIZE } else { len_remaining };
            let preamble =
                VendorDependentPreamble::new(self.raw_pdu_id(), packet_type, current_len as u16);

            let mut payload_buf = vec![0; preamble.encoded_len()];
            preamble.encode(&mut payload_buf[..])?;
            payload_buf.extend_from_slice(&buf[offset..current_len + offset]);
            payloads.push(payload_buf);

            len_remaining -= current_len;
            offset += current_len;
            if len_remaining == 0 {
                break;
            } else if len_remaining <= AVC_PAYLOAD_SIZE {
                packet_type = PacketType::Stop;
            } else {
                packet_type = PacketType::Continue;
            }
        }
        Ok(payloads)
    }
}

/// Specifies the AVC command type for this packet. Used only on Command packet and not
/// response packet encoders.
pub trait VendorCommand: VendorDependentPdu {
    /// Command type.
    fn command_type(&self) -> AvcCommandType;
}

/// For sending raw pre-assembled packets, typically as part of test packets. No decoder for this
/// packet type.
pub struct RawVendorDependentPacket {
    pdu_id: PduId,
    payload: Vec<u8>,
}

impl RawVendorDependentPacket {
    pub fn new(pdu_id: PduId, payload: &[u8]) -> Self {
        Self { pdu_id, payload: payload.to_vec() }
    }
}

impl Encodable for RawVendorDependentPacket {
    fn encoded_len(&self) -> usize {
        self.payload.len()
    }

    fn encode(&self, buf: &mut [u8]) -> PacketResult<()> {
        if buf.len() != self.payload.len() {
            return Err(Error::InvalidMessageLength);
        }

        buf.copy_from_slice(&self.payload[..]);
        Ok(())
    }
}

/// Packet PDU ID for vendor dependent packet encoding.
impl VendorDependentPdu for RawVendorDependentPacket {
    fn pdu_id(&self) -> PduId {
        self.pdu_id
    }
}

// TODO(fxbug.dev/41343): Specify the command type with the REPL when sending raw packets.
// For now, default to Control.
/// Specifies the AVC command type for this AVC command packet
impl VendorCommand for RawVendorDependentPacket {
    fn command_type(&self) -> AvcCommandType {
        AvcCommandType::Control
    }
}

// TODO: Remove once slice_fill feature stabilizes.
// https://github.com/rust-lang/rust/issues/70758
trait FillExt<T> {
    fn fill(&mut self, v: T);
}

impl FillExt<u8> for [u8] {
    fn fill(&mut self, v: u8) {
        for i in self {
            *i = v
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_bluetooth_avrcp as fidl;

    #[test]
    fn test_playback_status_to_fidl() {
        let status = PlaybackStatus::Playing;
        let status: fidl::PlaybackStatus = status.into();
        let expected = fidl::PlaybackStatus::Playing;
        assert_eq!(expected, status);
    }

    #[test]
    fn test_playback_status_from_fidl() {
        let status = fidl::PlaybackStatus::Stopped;
        let status: PlaybackStatus = status.into();
        let expected = PlaybackStatus::Stopped;
        assert_eq!(expected, status);
    }
}
