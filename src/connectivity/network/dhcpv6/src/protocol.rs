// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Parsing and serialization for DHCPv6 messages.

use byteorder::{ByteOrder, NetworkEndian};
use fuchsia_syslog::fx_log_warn;
use num_derive::FromPrimitive;
use packet::{
    records::{Records, RecordsImpl, RecordsImplLayout, RecordsSerializer, RecordsSerializerImpl},
    BufferView, BufferViewMut, InnerPacketBuilder, ParsablePacket, ParseMetadata,
};
use std::convert::TryFrom;
use std::mem;
use std::slice::Iter;
use thiserror::Error;
use uuid::Uuid;
use zerocopy::{AsBytes, ByteSlice, FromBytes, LayoutVerified, Unaligned};

type U16 = zerocopy::U16<NetworkEndian>;

#[derive(Debug, Error, PartialEq)]
pub enum ProtocolError {
    #[error("invalid message type: {}", _0)]
    InvalidDhcpv6MessageType(u8),
    #[error("invalid option code: {}", _0)]
    InvalidOpCode(u16),
    #[error("invalid option length {} for option code {:?}", _1, _0)]
    InvalidOpLen(Dhcpv6OptionCode, usize),
    #[error("buffer exhausted while more byts are expected")]
    BufferExhausted,
}

/// A DHCPv6 message type as defined in [RFC 8415, Section 7.3].
///
/// [RFC 8415, Section 7.3]: https://tools.ietf.org/html/rfc8415#section-7.3
#[derive(Debug, PartialEq, FromPrimitive)]
enum Dhcpv6MessageType {
    Solicit = 1,
    Advertise = 2,
    Request = 3,
    Confirm = 4,
    Renew = 5,
    Rebind = 6,
    Reply = 7,
    Release = 8,
    Decline = 9,
    Reconfigure = 10,
    InformationRequest = 11,
    RelayForw = 12,
    RelayRepl = 13,
}

impl From<Dhcpv6MessageType> for u8 {
    fn from(t: Dhcpv6MessageType) -> u8 {
        t as u8
    }
}

impl TryFrom<u8> for Dhcpv6MessageType {
    type Error = ProtocolError;

    fn try_from(b: u8) -> Result<Dhcpv6MessageType, ProtocolError> {
        <Self as num_traits::FromPrimitive>::from_u8(b)
            .ok_or(ProtocolError::InvalidDhcpv6MessageType(b))
    }
}

/// A DHCPv6 option code that identifies a corresponding option.
///
/// Options that are not found in this type are currently not supported. An exhaustive list of
/// option codes can be found [here][option-codes].
///
/// [option-codes]: https://www.iana.org/assignments/dhcpv6-parameters/dhcpv6-parameters.xhtml#dhcpv6-parameters-2
#[derive(Debug, PartialEq, FromPrimitive, Clone, Copy)]
#[repr(u8)]
pub enum Dhcpv6OptionCode {
    // TODO(jayzhuang): add more option codes.
    ClientId = 1,
    ServerId = 2,
    Oro = 6,
    Preference = 7,
    ElapsedTime = 8,
}

impl From<Dhcpv6OptionCode> for u16 {
    fn from(code: Dhcpv6OptionCode) -> u16 {
        code as u16
    }
}

impl TryFrom<u16> for Dhcpv6OptionCode {
    type Error = ProtocolError;

    fn try_from(n: u16) -> Result<Dhcpv6OptionCode, ProtocolError> {
        <Self as num_traits::FromPrimitive>::from_u16(n).ok_or(ProtocolError::InvalidOpCode(n))
    }
}

/// A parsed DHCPv6 options.
///
/// Options that are not found in this type are currently not supported. An exhaustive list of
/// options can be found [here][options].
///
/// [options]: https://www.iana.org/assignments/dhcpv6-parameters/dhcpv6-parameters.xhtml#dhcpv6-parameters-2
#[derive(Debug, PartialEq)]
enum Dhcpv6Option<'a> {
    // TODO(jayzhuang): add more options.
    // https://tools.ietf.org/html/rfc8415#section-21.2
    ClientId(&'a Duid),
    // https://tools.ietf.org/html/rfc8415#section-21.3
    ServerId(&'a Duid),
    // https://tools.ietf.org/html/rfc8415#section-21.7
    Oro(Vec<Dhcpv6OptionCode>),
    // https://tools.ietf.org/html/rfc8415#section-21.8
    Preference(u8),
    // https://tools.ietf.org/html/rfc8415#section-21.9
    ElapsedTime(u16),
}

/// An ID that uniquely identifies a DHCPv6 client or server, defined in [RFC8415, Section 11].
///
/// [RFC8415, Section 11]: https://tools.ietf.org/html/rfc8415#section-11
type Duid = [u8];

macro_rules! option_to_code {
    ($option:ident, $(Dhcpv6Option::$variant:tt($($v:tt)*)),*) => {
        match $option {
            $(Dhcpv6Option::$variant($($v)*)=>Dhcpv6OptionCode::$variant,)*
        }
    }
}

impl Dhcpv6Option<'_> {
    /// A helper function that returns the corresponding option code for the calling option.
    fn code(&self) -> Dhcpv6OptionCode {
        option_to_code!(
            self,
            Dhcpv6Option::ClientId(_),
            Dhcpv6Option::ServerId(_),
            Dhcpv6Option::Oro(_),
            Dhcpv6Option::Preference(_),
            Dhcpv6Option::ElapsedTime(_)
        )
    }
}

struct Dhcpv6OptionsImpl;

impl RecordsImplLayout for Dhcpv6OptionsImpl {
    type Context = ();
    type Error = ProtocolError;
}

impl<'a> RecordsImpl<'a> for Dhcpv6OptionsImpl {
    type Record = Dhcpv6Option<'a>;

    /// Tries to parse an option from the beginning of the input buffer. Returns the parsed
    /// `Dhcpv6Option` and the remaining buffer. If the buffer is malformed, returns a
    /// `ProtocolError`. Option format as defined in [RFC 8415, Section 21.1]:
    ///
    /// [RFC 8415, Section 21.1]: https://tools.ietf.org/html/rfc8415#section-21.1
    fn parse_with_context<BV: BufferView<&'a [u8]>>(
        data: &mut BV,
        _context: &mut Self::Context,
    ) -> Result<Option<Option<Self::Record>>, Self::Error> {
        if data.len() == 0 {
            return Ok(None);
        }

        let opt_code = data.take_obj_front::<U16>().ok_or(ProtocolError::BufferExhausted)?.get();
        let opt_len =
            usize::from(data.take_obj_front::<U16>().ok_or(ProtocolError::BufferExhausted)?.get());
        let opt_val = data.take_front(opt_len).ok_or(ProtocolError::BufferExhausted)?;

        let opt = match Dhcpv6OptionCode::try_from(opt_code)? {
            Dhcpv6OptionCode::ClientId => Ok(Dhcpv6Option::ClientId(opt_val)),
            Dhcpv6OptionCode::ServerId => Ok(Dhcpv6Option::ServerId(opt_val)),
            Dhcpv6OptionCode::Oro => match opt_len % 2 {
                0 => Ok(Dhcpv6Option::Oro(
                    opt_val
                        .chunks(2)
                        .map(|opt| Dhcpv6OptionCode::try_from(NetworkEndian::read_u16(opt)))
                        .collect::<Result<Vec<Dhcpv6OptionCode>, ProtocolError>>()?,
                )),
                _ => Err(ProtocolError::InvalidOpLen(Dhcpv6OptionCode::Oro, opt_len)),
            },
            Dhcpv6OptionCode::Preference => match opt_val {
                &[b] => Ok(Dhcpv6Option::Preference(b)),
                _ => Err(ProtocolError::InvalidOpLen(Dhcpv6OptionCode::Preference, opt_val.len())),
            },
            Dhcpv6OptionCode::ElapsedTime => match opt_val {
                &[b0, b1] => Ok(Dhcpv6Option::ElapsedTime(u16::from_be_bytes([b0, b1]))),
                _ => Err(ProtocolError::InvalidOpLen(Dhcpv6OptionCode::ElapsedTime, opt_val.len())),
            },
        }?;

        Ok(Some(Some(opt)))
    }
}

/// Generates a random DUID UUID based on the format defined in [RFC 8415, Section 11.5].
///
/// [RFC 8415, Section 11.5]: https://tools.ietf.org/html/rfc8415#section-11.5
fn duid_uuid() -> [u8; 18] {
    let mut duid = [0u8; 18];
    duid[1] = 4;
    let uuid = Uuid::new_v4();
    let uuid = uuid.as_bytes();
    for i in 0..16 {
        duid[2 + i] = uuid[i];
    }
    duid
}

impl<'a> RecordsSerializerImpl<'a> for Dhcpv6OptionsImpl {
    type Record = Dhcpv6Option<'a>;

    /// Calculates the serialized length of the option based on option format defined in
    /// [RFC 8415, Section 21.1].
    ///
    /// For variable length options that exceeds the size limit (u16::MAX), fallback to a default
    /// value.
    ///
    /// [RFC 8415, Section 21.1]: https://tools.ietf.org/html/rfc8415#section-21.1
    fn record_length(opt: &Self::Record) -> usize {
        4 + match opt {
            Dhcpv6Option::ClientId(duid) | Dhcpv6Option::ServerId(duid) => {
                u16::try_from(duid.len()).unwrap_or(18) as usize
            }
            Dhcpv6Option::Oro(opts) => u16::try_from(2 * opts.len()).unwrap_or(0) as usize,
            Dhcpv6Option::Preference(_) => 1,
            Dhcpv6Option::ElapsedTime(_) => 2,
        }
    }

    /// Serializes an option and appends to input buffer based on option format defined in
    /// [RFC 8415, Section 21.1].
    ///
    /// For variable length options that exceeds the size limit (u16::MAX), log a warning and
    /// fallback to use a default value instead, so it is impossible for future changes to
    /// introduce DoS vulnerabilities even if they accidentally allow such options to be injected.
    ///
    /// [RFC 8415, Section 21.1]: https://tools.ietf.org/html/rfc8415#section-21.1
    fn serialize(mut buf: &mut [u8], opt: &Self::Record) {
        // Implements BufferViewMut, giving us write_obj_front.
        let mut buf = &mut buf;
        buf.write_obj_front(&U16::new(opt.code().into()));

        match opt {
            Dhcpv6Option::ClientId(duid) | Dhcpv6Option::ServerId(duid) => {
                let uuid = duid_uuid();
                let (duid, len) = u16::try_from(duid.len()).map_or_else(
                    |err| {
                        // Do not panic, so DUIDs with length exceeding u16
                        // won't introduce DoS vulnerability.
                        fx_log_warn!(
                            "failed to convert duid length to u16: {}, using a random UUID",
                            err
                        );
                        (&uuid.as_bytes()[..], 18)
                    },
                    |len| (duid, len),
                );
                buf.write_obj_front(&U16::new(len));
                buf.write_obj_front(duid);
            }
            Dhcpv6Option::Oro(requested_opts) => {
                let empty = vec![];
                let (requested_opts, len) = u16::try_from(2 * requested_opts.len()).map_or_else(
                    |err| {
                        // Do not panic, so OROs with size exceeding u16 won't introduce DoS
                        // vulnerability.
                        fx_log_warn!(
                            "failed to convert requested option length to u16: {}, using empty options",
                            err
                        );
                        (&empty, 0)
                    },
                    |len| (requested_opts, len),
                );
                buf.write_obj_front(&U16::new(len));
                buf.write_obj_front(
                    requested_opts
                        .into_iter()
                        .flat_map(|opt_code| {
                            let opt_code: [u8; 2] = u16::from(*opt_code).to_be_bytes();
                            opt_code.to_vec()
                        })
                        .collect::<Vec<u8>>()
                        .as_slice(),
                );
            }
            Dhcpv6Option::Preference(pref_val) => {
                buf.write_obj_front(&U16::new(1));
                buf.write_obj_front(pref_val);
            }
            Dhcpv6Option::ElapsedTime(elapsed_time) => {
                buf.write_obj_front(&U16::new(2));
                buf.write_obj_front(&U16::new(*elapsed_time));
            }
        }
    }
}

#[derive(FromBytes, AsBytes, Unaligned, Copy, Clone, Debug, PartialEq)]
#[repr(C)]
struct Dhcpv6MessagePrefix {
    msg_type: u8,
    transaction_id: [u8; 3],
}

/// A DHCPv6 message as defined in [RFC 8415, Section 8].
///
/// [RFC 8415, Section 8]: https://tools.ietf.org/html/rfc8415#section-8
struct Dhcpv6Message<B> {
    prefix: LayoutVerified<B, Dhcpv6MessagePrefix>,
    options: Records<B, Dhcpv6OptionsImpl>,
}

impl<B: ByteSlice> ParsablePacket<B, ()> for Dhcpv6Message<B> {
    type Error = ProtocolError;

    fn parse_metadata(&self) -> ParseMetadata {
        ParseMetadata::from_packet(
            mem::size_of::<Dhcpv6MessagePrefix>(),
            self.options.bytes().len(),
            0,
        )
    }

    /// Tries to parse a DHCPv6 message by consuming the input buffer. Returns the parsed
    /// `Dhcpv6Message`. If the buffer is malformed, returns a `ProtocolError`.
    fn parse<BV: BufferView<B>>(mut buf: BV, _args: ()) -> Result<Self, Self::Error> {
        let prefix =
            buf.take_obj_front::<Dhcpv6MessagePrefix>().ok_or(ProtocolError::BufferExhausted)?;
        let _ = Dhcpv6MessageType::try_from(prefix.msg_type)?;
        let options = Records::<_, Dhcpv6OptionsImpl>::parse(buf.take_rest_front())?;
        Ok(Dhcpv6Message { prefix, options })
    }
}

struct Dhcpv6MessageBuilder<'a> {
    prefix: Dhcpv6MessagePrefix,
    options: RecordsSerializer<'a, Dhcpv6OptionsImpl, Dhcpv6Option<'a>, Iter<'a, Dhcpv6Option<'a>>>,
}

impl<'a> Dhcpv6MessageBuilder<'a> {
    fn new(
        msg_type: Dhcpv6MessageType,
        transaction_id: [u8; 3],
        options: &'a [Dhcpv6Option<'a>],
    ) -> Dhcpv6MessageBuilder<'a> {
        let msg_type = u8::from(msg_type);
        Dhcpv6MessageBuilder {
            prefix: Dhcpv6MessagePrefix { msg_type, transaction_id },
            options: RecordsSerializer::new(options.iter()),
        }
    }
}

impl InnerPacketBuilder for Dhcpv6MessageBuilder<'_> {
    fn bytes_len(&self) -> usize {
        mem::size_of::<Dhcpv6MessagePrefix>() + self.options.records_bytes_len()
    }

    fn serialize(&self, mut buffer: &mut [u8]) {
        // Implements BufferViewMut, giving us write_obj_front.
        let mut buffer = &mut buffer;
        buffer.write_obj_front(&self.prefix);
        self.options.serialize_records(buffer);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn test_buf_with_no_options() -> Vec<u8> {
        let builder = Dhcpv6MessageBuilder::new(Dhcpv6MessageType::Solicit, [1, 2, 3], &[]);
        let mut buf = vec![0; builder.bytes_len()];
        builder.serialize(&mut buf);
        buf
    }

    #[test]
    fn test_message_serialization() {
        let options = [
            Dhcpv6Option::ClientId(&[4, 5, 6]),
            Dhcpv6Option::ServerId(&[8]),
            Dhcpv6Option::Oro(vec![Dhcpv6OptionCode::ClientId, Dhcpv6OptionCode::ServerId]),
            Dhcpv6Option::Preference(42),
            Dhcpv6Option::ElapsedTime(3600),
        ];
        let builder = Dhcpv6MessageBuilder::new(Dhcpv6MessageType::Solicit, [1, 2, 3], &options);
        let mut buf = vec![0; builder.bytes_len()];
        builder.serialize(&mut buf);
        assert_eq!(buf.len(), 35);
        assert_eq!(
            buf,
            vec![
                1, // message type
                1, 2, 3, // transaction id
                0, 1, 0, 3, 4, 5, 6, // option - client ID
                0, 2, 0, 1, 8, // option - server ID
                0, 6, 0, 4, 0, 1, 0, 2, // option - ORO
                0, 7, 0, 1, 42, // option - preference
                0, 8, 0, 2, 14, 16, // option - elapsed time
            ],
        );
    }

    #[test]
    fn test_message_serialization_parsing_roundtrip() {
        let options = [
            Dhcpv6Option::ClientId(&[4, 5, 6]),
            Dhcpv6Option::ServerId(&[8]),
            Dhcpv6Option::Oro(vec![Dhcpv6OptionCode::ClientId, Dhcpv6OptionCode::ServerId]),
            Dhcpv6Option::Preference(42),
            Dhcpv6Option::ElapsedTime(3600),
        ];
        let builder = Dhcpv6MessageBuilder::new(Dhcpv6MessageType::Solicit, [1, 2, 3], &options);
        let mut buf = vec![0; builder.bytes_len()];
        builder.serialize(&mut buf);

        let mut buf = &buf[..];
        let msg = Dhcpv6Message::parse(&mut buf, ()).expect("parse should succeed");
        assert_eq!(*msg.prefix.into_ref(), builder.prefix);
        let got_options: Vec<_> = msg.options.iter().collect();
        assert_eq!(got_options, options);
    }

    #[test]
    fn test_message_serialization_duid_too_long() {
        let options = [Dhcpv6Option::ClientId(&[0u8; (u16::MAX as usize) + 1])];
        let builder = Dhcpv6MessageBuilder::new(Dhcpv6MessageType::Solicit, [1, 2, 3], &options);
        let mut buf = vec![0; builder.bytes_len()];
        builder.serialize(&mut buf);

        assert_eq!(buf.len(), 26);
        assert_eq!(
            buf[..8],
            [
                1, // message type
                1, 2, 3, // transaction id
                0, 1, 0, 18, // option - client ID (code and len only)
            ],
        );

        // Make sure the buffer is still parsable.
        let mut buf = &buf[..];
        let _ = Dhcpv6Message::parse(&mut buf, ()).expect("parse should succeed");
    }

    #[test]
    fn test_message_serialization_oro_too_long() {
        let options =
            [Dhcpv6Option::Oro(vec![Dhcpv6OptionCode::Preference; (u16::MAX as usize) + 1])];
        let builder = Dhcpv6MessageBuilder::new(Dhcpv6MessageType::Solicit, [1, 2, 3], &options);
        let mut buf = vec![0; builder.bytes_len()];
        builder.serialize(&mut buf);

        assert_eq!(
            buf,
            vec![
                1, // message type
                1, 2, 3, // transaction id
                0, 6, 0, 0, // option - ORO
            ],
        );

        // Make sure the buffer is still parsable.
        let mut buf = &buf[..];
        let _ = Dhcpv6Message::parse(&mut buf, ()).expect("parse should succeed");
    }

    #[test]
    fn test_option_serialization_parsing_roundtrip() {
        let mut buf = [0u8; 6];
        let option = Dhcpv6Option::ElapsedTime(42);

        <Dhcpv6OptionsImpl as RecordsSerializerImpl>::serialize(&mut buf, &option);
        assert_eq!(buf, [0, 8, 0, 2, 0, 42]);

        let options = Records::<_, Dhcpv6OptionsImpl>::parse_with_context(&buf[..], ()).unwrap();
        let options: Vec<Dhcpv6Option<'_>> = options.iter().collect();
        assert_eq!(options.len(), 1);
        assert_eq!(options[0], Dhcpv6Option::ElapsedTime(42));
    }

    #[test]
    fn test_buffer_too_short() {
        let buf = [];
        assert!(Dhcpv6Message::parse(&mut &buf[..], ()).is_err());

        let buf = [
            1, // valid message type
            0, // transaction id is too short
        ];
        assert!(Dhcpv6Message::parse(&mut &buf[..], ()).is_err());

        let buf = [
            1, // valid message type
            1, 2, 3, // valid transaction id
            0, // option code is too short
        ];
        assert!(Dhcpv6Message::parse(&mut &buf[..], ()).is_err());

        let buf = [
            1, // valid message type
            1, 2, 3, // valida transaction id
            0, 1, // valid option code
            0, // option length is too short
        ];
        assert!(Dhcpv6Message::parse(&mut &buf[..], ()).is_err());

        // option value too short
        let buf = [
            1, // valid message type
            1, 2, 3, // valid transaction id
            0, 1, // valid option code
            0, 100, // valid option length
            1, 2, // option value is too short
        ];
        assert!(Dhcpv6Message::parse(&mut &buf[..], ()).is_err());
    }

    #[test]
    fn test_invalid_message_type() {
        let mut buf = test_buf_with_no_options();
        // 0 is an invalid message type.
        buf[0] = 0;
        assert!(Dhcpv6Message::parse(&mut &buf[..], ()).is_err());
    }

    #[test]
    fn test_invalid_op_code() {
        let mut buf = test_buf_with_no_options();
        buf.append(&mut vec![
            0, 0, // opt code = 0, invalid op code
            0, 1, // valid opt length
            0, // valid opt value
        ]);
        assert!(Dhcpv6Message::parse(&mut &buf[..], ()).is_err());
    }

    // Oro must have a even option length, according to [RFC 8415, Section 21.7].
    //
    // [RFC 8415, Section 21.7]: https://tools.ietf.org/html/rfc8415#section-21.7
    #[test]
    fn test_invalid_oro_opt_len() {
        let mut buf = test_buf_with_no_options();
        buf.append(&mut vec![
            0, 6, // opt code = 6, ORO
            0, 1, // invalid opt length, must be even
            0,
        ]);
        assert!(Dhcpv6Message::parse(&mut &buf[..], ()).is_err());
    }

    // Preference must have option length 1, according to [RFC8415, Section 21.8].
    //
    // [RFC8415, Section 21.8]: https://tools.ietf.org/html/rfc8415#section-21.8
    #[test]
    fn test_invalid_preference_opt_len() {
        let mut buf = test_buf_with_no_options();
        buf.append(&mut vec![
            0, 7, // opt code = 7, preference
            0, 2, // invalid opt length, must be even
            0, 0,
        ]);
        assert!(Dhcpv6Message::parse(&mut &buf[..], ()).is_err());
    }

    // Elapsed time must have option length 2, according to [RFC 8145, Section 21.9].
    //
    // [RFC 8145, Section 21.9]: https://tools.ietf.org/html/rfc8415#section-21.9
    #[test]
    fn test_elapsed_time_invalid_opt_len() {
        let mut buf = test_buf_with_no_options();
        buf.append(&mut vec![
            0, 8, // opt code = 8, elapsed time
            0, 3, // invalid opt length, must be even
            0, 0, 0,
        ]);
        assert!(Dhcpv6Message::parse(&mut &buf[..], ()).is_err());
    }
}
