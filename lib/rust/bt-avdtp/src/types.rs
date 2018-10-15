// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]

use fuchsia_syslog::fx_log_warn;
use fuchsia_zircon as zx;

pub(crate) trait TryFrom<T>: Sized {
    type Error;
    fn try_from(value: T) -> Result<Self, Self::Error>;
}

/// A decodable type can be created from a byte buffer.
/// The type returned is separate (copied) from the buffer once decoded.
pub(crate) trait Decodable: Sized {
    /// Decodes into a new object, or returns an error.
    fn decode(buf: &[u8]) -> Result<Self, Error>;
}

/// A encodable type can write itself into a byte buffer.
pub(crate) trait Encodable: Sized {
    /// Returns the number of bytes necessary to encode |self|
    fn encoded_len(&self) -> usize;

    /// Writes the encoded version of |self| at the start of |buf|
    /// |buf| must be at least size() length.
    fn encode(&self, buf: &mut [u8]) -> Result<(), Error>;
}

/// Generates an enum value where each variant can be converted into a constant in the given
/// raw_type.  For example:
/// decodable_enum! {
///     Color<u8> {
///        Red => 1,
///        Blue => 2,
///        Green => 3,
///     }
/// }
/// Then Color::try_from(2) returns Color::Red, and u8::from(Color::Red) returns 1.
macro_rules! decodable_enum {
    ($(#[$meta:meta])* $name:ident<$raw_type:ty> {
        $($variant:ident => $val:expr),*,
    }) => {
        $(#[$meta])*
        #[derive(Debug, PartialEq)]
        pub(crate) enum $name {
            $($variant),*
        }

        tofrom_decodable_enum! {
            $name<$raw_type> {
                $($variant => $val),*,
            }
        }
    }
}

/// The same as decodable_enum, but the struct is public.
macro_rules! pub_decodable_enum {
    ($(#[$meta:meta])* $name:ident<$raw_type:ty> {
        $($variant:ident => $val:expr),*,
    }) => {
        $(#[$meta])*
        #[derive(Debug, PartialEq)]
        pub enum $name {
            $($variant),*
        }

        tofrom_decodable_enum! {
            $name<$raw_type> {
                $($variant => $val),*,
            }
        }
    }
}

/// A From<&$name> for $raw_type implementation and
/// TryFrom<$raw_type> for $name implementation, used by (pub_)decodable_enum
macro_rules! tofrom_decodable_enum {
    ($name:ident<$raw_type:ty> {
        $($variant:ident => $val:expr),*,
    }) => {
        impl From<&$name> for $raw_type {
            fn from(v: &$name) -> $raw_type {
                match v {
                    $($name::$variant => $val),*,
                }
            }
        }

        impl TryFrom<$raw_type> for $name {
            type Error = Error;
            fn try_from(value: $raw_type) -> Result<Self, Self::Error> {
                match value {
                    $($val => Ok($name::$variant)),*,
                    _ => Err(Error::OutOfRange),
                }
            }
        }
    }
}

/// An AVDTP Transaction Label
/// Not used outside the library.  Public as part of some internal Error variants.
/// See Section 8.4.1
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct TxLabel(u8);

// Transaction labels are only 4 bits.
const MAX_TX_LABEL: u8 = 0xF;

impl TryFrom<u8> for TxLabel {
    type Error = Error;

    fn try_from(value: u8) -> Result<Self, Self::Error> {
        if value > MAX_TX_LABEL {
            fx_log_warn!("TxLabel out of range: {}", value);
            Err(Error::OutOfRange)
        } else {
            Ok(TxLabel(value))
        }
    }
}

impl From<&TxLabel> for u8 {
    fn from(v: &TxLabel) -> u8 {
        v.0
    }
}

impl From<&TxLabel> for usize {
    fn from(v: &TxLabel) -> usize {
        v.0 as usize
    }
}

pub_decodable_enum! {
    /// Type of media
    /// USed to specify the type of media on a stream endpoint.
    /// Part of the StreamInformation in Discovery Response.
    /// Defined in the Bluetooth Assigned Numbers
    /// https://www.bluetooth.com/specifications/assigned-numbers/audio-video
    MediaType<u8> {
        Audio => 0x00,
        Video => 0x01,
        Multimedia => 0x02,
    }
}

pub_decodable_enum! {
    /// Type of endpoint (source or sync)
    /// Part of the StreamInformation in Discovery Response.
    /// See Section 8.20.3
    EndpointType<u8> {
        Source => 0x00,
        Sink => 0x01,
    }
}

decodable_enum! {
    /// Indicated whether this paket is part of a fragmented packet set.
    /// See Section 8.4.2
    SignalingPacketType<u8> {
        Single => 0x00,
        Start => 0x01,
        Continue => 0x02,
        End => 0x03,
    }
}

decodable_enum! {
    /// Specifies the command type of each signaling command or the response
    /// type of each response packet.
    /// See Section 8.4.3
    SignalingMessageType<u8> {
        Command => 0x00,
        GeneralReject => 0x01,
        ResponseAccept => 0x02,
        ResponseReject => 0x03,
    }
}

decodable_enum! {
    /// Indicates the signaling command on a command packet.  The same identifier is used on the
    /// response to that command packet.
    /// See Section 8.4.4
    #[derive(Copy, Clone)]
    SignalIdentifier<u8> {
        Discover => 0x01,
        GetCapabilities => 0x02,
        SetConfiguration => 0x03,
        GetConfiguration => 0x04,
        Reconfigure => 0x05,
        Open => 0x06,
        Start => 0x07,
        Close => 0x08,
        Suspend => 0x09,
        Abort => 0x0A,
        SecurityControl => 0x0B,
        GetAllCapabilities => 0x0C,
        DelayReport => 0x0D,
    }
}

#[derive(Debug)]
pub(crate) struct SignalingHeader {
    pub label: TxLabel,
    packet_type: SignalingPacketType,
    message_type: SignalingMessageType,
    num_packets: u8,
    pub signal: SignalIdentifier,
}

impl SignalingHeader {
    pub fn new(
        label: TxLabel, signal: SignalIdentifier, message_type: SignalingMessageType,
    ) -> SignalingHeader {
        SignalingHeader {
            label: label,
            signal: signal,
            message_type: message_type,
            packet_type: SignalingPacketType::Single,
            num_packets: 1,
        }
    }

    pub fn label(&self) -> TxLabel {
        self.label
    }

    pub fn signal(&self) -> SignalIdentifier {
        self.signal
    }

    pub fn is_type(&self, other: SignalingMessageType) -> bool {
        self.message_type == other
    }

    pub fn is_command(&self) -> bool {
        self.is_type(SignalingMessageType::Command)
    }
}

impl Decodable for SignalingHeader {
    fn decode(bytes: &[u8]) -> Result<SignalingHeader, Error> {
        if bytes.len() < 2 {
            return Err(Error::OutOfRange);
        }
        let label = TxLabel::try_from(bytes[0] >> 4)?;
        let packet_type = SignalingPacketType::try_from((bytes[0] >> 2) & 0x3)?;
        let (id_offset, num_packets) = match packet_type {
            SignalingPacketType::Start => {
                if bytes.len() < 3 {
                    return Err(Error::OutOfRange);
                }
                (2, bytes[1])
            }
            _ => (1, 1),
        };
        let signal_id_val = bytes[id_offset] & 0x3F;
        let id = SignalIdentifier::try_from(signal_id_val)
            .map_err(|_| Error::InvalidSignalId(label, signal_id_val))?;
        let header = SignalingHeader {
            label: label,
            packet_type: packet_type,
            message_type: SignalingMessageType::try_from(bytes[0] & 0x3)?,
            signal: id,
            num_packets: num_packets,
        };
        Ok(header)
    }
}

impl Encodable for SignalingHeader {
    fn encoded_len(&self) -> usize {
        if self.num_packets > 1 {
            3
        } else {
            2
        }
    }

    fn encode(&self, buf: &mut [u8]) -> Result<(), Error> {
        if buf.len() < self.encoded_len() {
            return Err(Error::Encoding);
        }
        buf[0] = u8::from(&self.label) << 4
            | u8::from(&self.packet_type) << 2
            | u8::from(&self.message_type);
        buf[1] = u8::from(&self.signal);
        Ok(())
    }
}

/// The error type of the AVDTP library.
#[derive(Fail, Debug, PartialEq)]
pub enum Error {
    /// The value that was sent on the wire was out of range.
    #[fail(display = "Value was out of range")]
    OutOfRange,

    /// The signal identifier was invalid when parsing a message.
    #[fail(display = "Invalid signal id for {:?}: {:X?}", _0, _1)]
    InvalidSignalId(TxLabel, u8),

    /// The header was invalid when parsing a message from the peer.
    #[fail(display = "Invalid Header for a AVDTP message")]
    InvalidHeader,

    /// The body format was invalid when parsing a message from the peer.
    #[fail(display = "Failed to parse AVDTP message contents")]
    InvalidMessage,

    /// The remote end failed to respond to this command in time.
    #[fail(display = "Command timed out")]
    Timeout,

    /// The Remote end rejected a command we sent (with this error code)
    #[fail(display = "Remote end rejected the command (code = {:}", _0)]
    RemoteRejected(u8),

    /// The Remote end rejected a start or suspend command we sent, indicating this SEID and error
    /// code.
    #[fail(
        display = "Remote end rejected the command (SEID = {:}, code = {:}",
        _0, _1
    )]
    RemoteStreamRejected(u8, u8),

    /// Message has been requested that isn't Implemented
    #[fail(display = "Message has not been implemented yet")]
    UnimplementedMessage,

    /// The distant peer has disconnected.
    #[fail(display = "Peer has disconnected")]
    PeerDisconnected,

    /// Sent if a Command Future is polled after it's already completed
    #[fail(display = "Command Response has already been received")]
    AlreadyReceived,

    /// Encountered an IO error setting up the channel
    #[fail(display = "Encountered an IO error reading from the peer: {}", _0)]
    ChannelSetup(#[cause] zx::Status),

    /// Encountered an IO error reading from the peer.
    #[fail(display = "Encountered an IO error reading from the peer: {}", _0)]
    PeerRead(#[cause] zx::Status),

    /// Encountered an IO error reading from the peer.
    #[fail(display = "Encountered an IO error writing to the peer: {}", _0)]
    PeerWrite(#[cause] zx::Status),

    /// A message couldn't be encoded.
    #[fail(display = "Encontered an error encoding a message")]
    Encoding,

    /// An error has been detected, and the request that is being handled
    /// should be rejected with the error code given.
    #[fail(display = "Invalid request detected: {:?}", _0)]
    RequestInvalid(ErrorCode),

    /// Same as RequestInvalid, but an extra byte is included, which is used
    /// in Stream and Configure responses
    #[fail(display = "Invalid request detected: {:?} (extra: {:?})", _0, _1)]
    RequestInvalidExtra(ErrorCode, u8),

    #[doc(hidden)]
    #[fail(display = "__Nonexhaustive error should never be created.")]
    __Nonexhaustive,
}

pub_decodable_enum!{
    /// Error Codes that can be returned as part of a reject message.
    /// See Section 8.20.6
    ErrorCode<u8> {
        // Header Error Codes
        BadHeaderFormat => 0x01,

        // Payload Format Error Codes
        BadLength => 0x11,
        BadAcpSeid => 0x12,
        SepInUse => 0x13,
        SepNotInUse => 0x14,
        BadServiceCategory => 0x17,
        BadPayloadFormat => 0x18,
        NotSupportedCommand => 0x19,
        InvalidCapabilities => 0x1A,

        // Transport Service Capabilities Error Codes
        BadRecoveryType => 0x22,
        BadMediaTransportFormat => 0x23,
        BadRecoveryFormat => 0x25,
        BadRohcFormat => 0x26,
        BadCpFormat => 0x27,
        BadMultiplexingFormat => 0x28,
        UnsupportedConfiguration => 0x29,

        // Procedure Error Codes
        BadState => 0x31,
    }
}

#[cfg(test)]
mod test {
    use super::*;

    decodable_enum! {
        TestEnum<u16> {
            One => 1,
            Two => 2,
            Max => 65535,
        }
    }

    #[test]
    fn try_from_success() {
        let one = TestEnum::try_from(1);
        assert!(one.is_ok());
        assert_eq!(TestEnum::One, one.unwrap());
        let two = TestEnum::try_from(2);
        assert!(two.is_ok());
        assert_eq!(TestEnum::Two, two.unwrap());
        let max = TestEnum::try_from(65535);
        assert!(max.is_ok());
        assert_eq!(TestEnum::Max, max.unwrap());
    }

    #[test]
    fn try_from_error() {
        let err = TestEnum::try_from(5);
        assert_eq!(Some(Error::OutOfRange), err.err());
    }

    #[test]
    fn into_rawtype() {
        let raw = u16::from(&TestEnum::One);
        assert_eq!(1, raw);
        let raw = u16::from(&TestEnum::Two);
        assert_eq!(2, raw);
        let raw = u16::from(&TestEnum::Max);
        assert_eq!(65535, raw);
    }

    #[test]
    fn txlabel_tofrom_u8() {
        let mut label: Result<TxLabel, Error> = TxLabel::try_from(15);
        assert!(label.is_ok());
        assert_eq!(15, u8::from(&label.unwrap()));
        label = TxLabel::try_from(16);
        assert_eq!(Err(Error::OutOfRange), label);
    }

    #[test]
    fn txlabel_to_usize() {
        let label = TxLabel::try_from(1).unwrap();
        assert_eq!(1, usize::from(&label));
    }
}
