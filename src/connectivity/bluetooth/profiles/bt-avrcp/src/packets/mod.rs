// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    bt_avctp::AvcCommandType,
    fidl_fuchsia_bluetooth_avrcp as fidl_avrcp,
    packet_encoding::{decodable_enum, Decodable, Encodable},
    std::{convert::TryFrom, result},
    thiserror::Error,
};

mod browsing;
mod continuation;
mod get_capabilities;
mod get_element_attributes;
mod get_play_status;
mod inform_battery_status;
mod notification;
mod play_item;
pub mod player_application_settings;
mod rejected;
mod set_absolute_volume;

pub use {
    self::browsing::*, self::continuation::*, self::get_capabilities::*,
    self::get_element_attributes::*, self::get_play_status::*, self::inform_battery_status::*,
    self::notification::*, self::play_item::*, self::player_application_settings::*,
    self::rejected::*, self::set_absolute_volume::*,
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

    /// A enum value is out of expected range.
    #[error("Value is out of expected range for enum")]
    OutOfRange,

    /// AVRCP 1.6.2 section 6.15.3.
    /// Direction parameter is invalid.
    #[error("The Direction parameter is invalid")]
    InvalidDirection,

    #[doc(hidden)]
    #[error("__Nonexhaustive error should never be created.")]
    __Nonexhaustive,
}

impl From<Error> for fidl_avrcp::BrowseControllerError {
    fn from(e: Error) -> Self {
        match e {
            Error::InvalidDirection => fidl_avrcp::BrowseControllerError::InvalidDirection,
            _ => fidl_avrcp::BrowseControllerError::PacketEncoding,
        }
    }
}

pub type PacketResult<T> = result::Result<T, Error>;

decodable_enum! {
    /// Common charset IDs from the MIB Enum we may see in AVRCP. See:
    /// https://www.iana.org/assignments/character-sets/character-sets.xhtml
    /// TODO(fxb/102060): we ideally would not want to return OutOfRange error
    /// but instead return a default value (i.e. 0 => Unknown).
    pub enum CharsetId<u16, Error, OutOfRange> {
        Ascii = 3,
        Iso8859_1 = 4,
        Utf8 = 106,
        Ucs2 = 1000,
        Utf16be = 1013,
        Utf16le = 1014,
        Utf16 = 1015,
    }
}

impl CharsetId {
    pub fn is_utf8(&self) -> bool {
        match &self {
            Self::Utf8 | Self::Ascii => true,
            _ => false,
        }
    }
}

decodable_enum! {
    pub enum Direction<u8, Error, InvalidDirection> {
        FolderUp = 0,
        FolderDown = 1,
    }
}

/// The size, in bytes, of an attributes id.
pub const ATTRIBUTE_ID_LEN: usize = 4;

decodable_enum! {
    pub enum MediaAttributeId<u8, Error, OutOfRange> {
        Title = 0x1,
        ArtistName = 0x2,
        AlbumName = 0x3,
        TrackNumber = 0x4,
        TotalNumberOfTracks = 0x5,
        Genre = 0x6,
        PlayingTime = 0x7,
        DefaultCoverArt = 0x8,
    }
}

impl From<&fidl_avrcp::MediaAttributeId> for MediaAttributeId {
    fn from(attr_id: &fidl_avrcp::MediaAttributeId) -> Self {
        match attr_id {
            fidl_avrcp::MediaAttributeId::Title => Self::Title,
            fidl_avrcp::MediaAttributeId::ArtistName => Self::ArtistName,
            fidl_avrcp::MediaAttributeId::AlbumName => Self::AlbumName,
            fidl_avrcp::MediaAttributeId::TrackNumber => Self::TrackNumber,
            fidl_avrcp::MediaAttributeId::TotalNumberOfTracks => Self::TotalNumberOfTracks,
            fidl_avrcp::MediaAttributeId::Genre => Self::Genre,
            fidl_avrcp::MediaAttributeId::PlayingTime => Self::PlayingTime,
            fidl_avrcp::MediaAttributeId::DefaultCoverArt => Self::DefaultCoverArt,
        }
    }
}

decodable_enum! {
    pub enum PduId<u8, Error, InvalidParameter> {
        GetCapabilities = 0x10,
        ListPlayerApplicationSettingAttributes = 0x11,
        ListPlayerApplicationSettingValues = 0x12,
        GetCurrentPlayerApplicationSettingValue = 0x13,
        SetPlayerApplicationSettingValue = 0x14,
        GetPlayerApplicationSettingAttributeText = 0x15,
        GetPlayerApplicationSettingValueText = 0x16,
        InformDisplayableCharacterSet = 0x17,
        InformBatteryStatusOfCT = 0x18,
        GetElementAttributes = 0x20,
        GetPlayStatus = 0x30,
        RegisterNotification = 0x31,
        RequestContinuingResponse = 0x40,
        AbortContinuingResponse = 0x41,
        SetAbsoluteVolume = 0x50,
        SetAddressedPlayer = 0x60,
        SetBrowsedPlayer = 0x70,
        GetFolderItems = 0x71,
        ChangePath = 0x72,
        PlayItem = 0x74,
        GetTotalNumberOfItems = 0x75,
        AddToNowPlaying = 0x90,
        GeneralReject = 0xa0,
    }
}

decodable_enum! {
    pub enum PacketType<u8, Error, OutOfRange> {
        Single = 0b00,
        Start = 0b01,
        Continue = 0b10,
        Stop = 0b11,
    }
}

decodable_enum! {
    pub enum StatusCode<u8, Error, OutOfRange> {
        InvalidCommand = 0x00,
        InvalidParameter = 0x01,
        ParameterContentError = 0x02,
        InternalError = 0x03,
        Success = 0x04,
        UidChanged = 0x05,
        InvalidDirection = 0x07,
        NonDirectory = 0x08,
        DoesNotExist = 0x09,
        InvalidScope = 0x0a,
        RangeOutOfBounds = 0x0b,
        ItemNotPlayable = 0x0c,
        MediaInUse = 0x0d,
        InvalidPlayerId = 0x11,
        PlayerNotBrowsable = 0x12,
        PlayerNotAddressed = 0x13,
        NoValidSearchResults = 0x14,
        NoAvailablePlayers = 0x15,
        AddressedPlayerChanged = 0x16,
    }
}

decodable_enum! {
    pub enum ItemType<u8, Error, OutOfRange> {
        MediaPlayer = 0x01,
        Folder = 0x02,
        MediaElement = 0x03,
    }
}

decodable_enum! {
    pub enum FolderType<u8, Error, OutOfRange> {
        Mixed = 0x00,
        Titles = 0x01,
        Albums = 0x02,
        Artists = 0x03,
        Genres = 0x04,
        Playlists = 0x05,
        Years = 0x06,
    }
}

impl From<FolderType> for fidl_avrcp::FolderType {
    fn from(t: FolderType) -> Self {
        match t {
            FolderType::Mixed => Self::Mixed,
            FolderType::Titles => Self::Titles,
            FolderType::Albums => Self::Albums,
            FolderType::Artists => Self::Artists,
            FolderType::Genres => Self::Genres,
            FolderType::Playlists => Self::Playlists,
            FolderType::Years => Self::Years,
        }
    }
}

decodable_enum! {
    pub enum MediaType<u8, Error, OutOfRange> {
        Audio = 0x00,
        Video = 0x01,
    }
}

impl From<MediaType> for fidl_avrcp::MediaType {
    fn from(t: MediaType) -> Self {
        match t {
            MediaType::Audio => Self::Audio,
            MediaType::Video => Self::Video,
        }
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

impl From<StatusCode> for fidl_avrcp::BrowseControllerError {
    fn from(status: StatusCode) -> Self {
        match status {
            StatusCode::Success => {
                panic!("cannot convert StatusCode::Success to BrowseControllerError")
            }
            StatusCode::UidChanged => fidl_avrcp::BrowseControllerError::UidChanged,
            StatusCode::InvalidDirection => fidl_avrcp::BrowseControllerError::InvalidDirection,
            StatusCode::NonDirectory | StatusCode::DoesNotExist | StatusCode::InvalidPlayerId => {
                fidl_avrcp::BrowseControllerError::InvalidId
            }
            StatusCode::InvalidScope => fidl_avrcp::BrowseControllerError::InvalidScope,
            StatusCode::RangeOutOfBounds => fidl_avrcp::BrowseControllerError::RangeOutOfBounds,
            StatusCode::ItemNotPlayable => fidl_avrcp::BrowseControllerError::ItemNotPlayable,
            StatusCode::MediaInUse => fidl_avrcp::BrowseControllerError::MediaInUse,
            StatusCode::PlayerNotBrowsable => fidl_avrcp::BrowseControllerError::PlayerNotBrowsable,
            StatusCode::PlayerNotAddressed => fidl_avrcp::BrowseControllerError::PlayerNotAddressed,
            StatusCode::NoValidSearchResults => fidl_avrcp::BrowseControllerError::NoValidResults,
            StatusCode::NoAvailablePlayers => fidl_avrcp::BrowseControllerError::NoAvailablePlayers,
            _ => fidl_avrcp::BrowseControllerError::PacketEncoding,
        }
    }
}

// Shared by get_play_status and notification
decodable_enum! {
    pub enum PlaybackStatus<u8, Error, OutOfRange> {
        Stopped = 0x00,
        Playing = 0x01,
        Paused = 0x02,
        FwdSeek = 0x03,
        RevSeek = 0x04,
        Error = 0xff,
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

decodable_enum! {
    pub enum PlayerApplicationSettingAttributeId<u8, Error, InvalidParameter> {
        Equalizer = 0x01,
        RepeatStatusMode = 0x02,
        ShuffleMode = 0x03,
        ScanMode = 0x04,
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
    type Error = Error;

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
    type Error = Error;

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
impl<T: VendorDependentRawPdu + Encodable<Error = Error>> PacketEncodable for T {
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
    type Error = Error;

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

#[cfg(test)]
#[track_caller]
pub fn decode_avc_vendor_command(command: &bt_avctp::AvcCommand) -> Result<(PduId, &[u8]), Error> {
    let packet_body = command.body();
    let preamble = VendorDependentPreamble::decode(packet_body)?;
    let body = &packet_body[preamble.encoded_len()..];
    let pdu_id = PduId::try_from(preamble.pdu_id)?;
    Ok((pdu_id, body))
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
