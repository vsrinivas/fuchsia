// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Parsing and serialization for DHCPv6 messages.

use {
    byteorder::{ByteOrder, NetworkEndian},
    fuchsia_syslog::fx_log_warn,
    mdns::protocol::{Domain, ParseError},
    num_derive::FromPrimitive,
    packet::{
        records::{
            ParsedRecord, RecordParseResult, Records, RecordsImpl, RecordsImplLayout,
            RecordsSerializer, RecordsSerializerImpl,
        },
        BufferView, BufferViewMut, InnerPacketBuilder, ParsablePacket, ParseMetadata,
    },
    std::{
        convert::{TryFrom, TryInto},
        mem,
        net::Ipv6Addr,
        slice::Iter,
    },
    thiserror::Error,
    uuid::Uuid,
    zerocopy::{AsBytes, ByteSlice},
};

type U16 = zerocopy::U16<NetworkEndian>;
type U32 = zerocopy::U32<NetworkEndian>;

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
    #[error("failed to parse domain {:?}", _0)]
    DomainParseError(ParseError),
}

/// A DHCPv6 message type as defined in [RFC 8415, Section 7.3].
///
/// [RFC 8415, Section 7.3]: https://tools.ietf.org/html/rfc8415#section-7.3
#[derive(Debug, PartialEq, FromPrimitive, AsBytes)]
#[repr(u8)]
pub enum Dhcpv6MessageType {
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
    DnsServers = 23,
    DomainList = 24,
    InformationRefreshTime = 32,
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
pub enum Dhcpv6Option<'a> {
    // TODO(jayzhuang): add more options.
    // https://tools.ietf.org/html/rfc8415#section-21.2
    ClientId(&'a Duid),
    // https://tools.ietf.org/html/rfc8415#section-21.3
    ServerId(&'a Duid),
    // https://tools.ietf.org/html/rfc8415#section-21.7
    // TODO(jayzhuang): add validation, not all option codes can present in ORO.
    // See https://www.iana.org/assignments/dhcpv6-parameters/dhcpv6-parameters.xhtml#dhcpv6-parameters-2
    Oro(Vec<Dhcpv6OptionCode>),
    // https://tools.ietf.org/html/rfc8415#section-21.8
    Preference(u8),
    // https://tools.ietf.org/html/rfc8415#section-21.9
    ElapsedTime(u16),
    // https://tools.ietf.org/html/rfc8415#section-21.23
    InformationRefreshTime(u32),
    // https://tools.ietf.org/html/rfc3646#section-3
    DnsServers(Vec<Ipv6Addr>),
    // https://tools.ietf.org/html/rfc3646#section-4
    DomainList(Vec<checked::Domain>),
}

mod checked {
    use crate::protocol::ProtocolError;
    use mdns::protocol::{DomainBuilder, EmbeddedPacketBuilder};
    use packet::BufferViewMut;
    use zerocopy::ByteSliceMut;

    /// A checked domain that can only be created through the provided constructor.
    #[derive(Debug, PartialEq)]
    pub struct Domain {
        domain: String,
        builder: DomainBuilder,
    }

    impl Domain {
        /// Constructs a `Domain` and takes ownership of the input string.
        ///
        /// # Errors
        ///
        /// If the input is not a valid domain following the definition in [RFC 1035].
        ///
        /// [RFC 1035]: https://tools.ietf.org/html/rfc1035
        pub(crate) fn from_string(s: String) -> Result<Self, ProtocolError> {
            let builder = DomainBuilder::from_str(&s).map_err(ProtocolError::DomainParseError)?;
            Ok(Domain { domain: s, builder })
        }

        pub(crate) fn bytes_len(&self) -> usize {
            self.builder.bytes_len()
        }

        pub(crate) fn serialize<B: ByteSliceMut, BV: BufferViewMut<B>>(&self, bv: &mut BV) {
            let () = self.builder.serialize(bv);
        }
    }
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
            Dhcpv6Option::ElapsedTime(_),
            Dhcpv6Option::InformationRefreshTime(_),
            Dhcpv6Option::DnsServers(_),
            Dhcpv6Option::DomainList(_)
        )
    }
}

#[derive(Debug)]
pub struct Dhcpv6OptionsImpl;

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
    ) -> RecordParseResult<Self::Record, Self::Error> {
        if data.len() == 0 {
            return Ok(ParsedRecord::Done);
        }

        let opt_code = data.take_obj_front::<U16>().ok_or(ProtocolError::BufferExhausted)?.get();
        let opt_len =
            usize::from(data.take_obj_front::<U16>().ok_or(ProtocolError::BufferExhausted)?.get());
        let mut opt_val = data.take_front(opt_len).ok_or(ProtocolError::BufferExhausted)?;

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
            Dhcpv6OptionCode::InformationRefreshTime => match opt_val {
                &[b0, b1, b2, b3] => {
                    Ok(Dhcpv6Option::InformationRefreshTime(u32::from_be_bytes([b0, b1, b2, b3])))
                }
                _ => Err(ProtocolError::InvalidOpLen(
                    Dhcpv6OptionCode::InformationRefreshTime,
                    opt_val.len(),
                )),
            },
            Dhcpv6OptionCode::DnsServers => match opt_len % 16 {
                0 => Ok(Dhcpv6Option::DnsServers(
                    opt_val
                        .chunks(16)
                        .map(|opt| {
                            let opt: [u8; 16] =
                                opt.try_into().expect("unexpected byte slice length after chunk");
                            Ipv6Addr::from(opt)
                        })
                        .collect::<Vec<Ipv6Addr>>(),
                )),
                _ => Err(ProtocolError::InvalidOpLen(Dhcpv6OptionCode::DnsServers, opt_len)),
            },
            Dhcpv6OptionCode::DomainList => {
                let mut opt_val = &mut opt_val;
                let mut domains = Vec::new();
                while opt_val.len() > 0 {
                    domains.push(checked::Domain::from_string(
                        Domain::parse(&mut opt_val, None)
                            .map_err(ProtocolError::DomainParseError)?
                            .to_string(),
                    )?);
                }
                Ok(Dhcpv6Option::DomainList(domains))
            }
        }?;

        Ok(ParsedRecord::Parsed(opt))
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
            Dhcpv6Option::InformationRefreshTime(_) => 4,
            Dhcpv6Option::DnsServers(recursive_name_servers) => {
                u16::try_from(16 * recursive_name_servers.len()).unwrap_or(0) as usize
            }
            Dhcpv6Option::DomainList(domains) => {
                u16::try_from(domains.iter().fold(0, |tot, domain| tot + domain.bytes_len()))
                    .unwrap_or(0) as usize
            }
        }
    }

    /// Serializes an option and appends to input buffer based on option format defined in
    /// [RFC 8415, Section 21.1].
    ///
    /// For variable length options that exceeds the size limit (u16::MAX), log a warning and
    /// fallback to use a default value instead, so it is impossible for future changes to
    /// introduce DoS vulnerabilities even if they accidentally allow such options to be injected.
    ///
    /// # Panics
    ///
    /// If buffer is too small. This means `record_length` is not correctly implemented.
    ///
    /// [RFC 8415, Section 21.1]: https://tools.ietf.org/html/rfc8415#section-21.1
    fn serialize(mut buf: &mut [u8], opt: &Self::Record) {
        // Implements BufferViewMut, giving us write_obj_front.
        let mut buf = &mut buf;
        let () = buf.write_obj_front(&U16::new(opt.code().into())).expect("buffer is too small");

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
                let () = buf.write_obj_front(&U16::new(len)).expect("buffer is too small");
                let () = buf.write_obj_front(duid).expect("buffer is too small");
            }
            Dhcpv6Option::Oro(requested_opts) => {
                let empty = Vec::new();
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
                let () = buf.write_obj_front(&U16::new(len)).expect("buffer is too small");
                let () = buf
                    .write_obj_front(
                        requested_opts
                            .into_iter()
                            .flat_map(|opt_code| {
                                let opt_code: [u8; 2] = u16::from(*opt_code).to_be_bytes();
                                opt_code.to_vec()
                            })
                            .collect::<Vec<u8>>()
                            .as_slice(),
                    )
                    .expect("buffer is too small");
            }
            Dhcpv6Option::Preference(pref_val) => {
                let () = buf.write_obj_front(&U16::new(1)).expect("buffer is too small");
                let () = buf.write_obj_front(pref_val).expect("buffer is too small");
            }
            Dhcpv6Option::ElapsedTime(elapsed_time) => {
                let () = buf.write_obj_front(&U16::new(2)).expect("buffer is too small");
                let () =
                    buf.write_obj_front(&U16::new(*elapsed_time)).expect("buffer is too small");
            }
            Dhcpv6Option::InformationRefreshTime(information_refresh_time) => {
                let () = buf.write_obj_front(&U16::new(4)).expect("buffer is too small");
                let () = buf
                    .write_obj_front(&U32::new(*information_refresh_time))
                    .expect("buffer is too smal");
            }
            Dhcpv6Option::DnsServers(recursive_name_servers) => {
                let empty = Vec::new();
                let (recursive_name_servers, len) =
                    u16::try_from(16 * recursive_name_servers.len()).map_or_else(
                        |err| {
                            // Do not panic, so DnsServers with size exceeding u16 won't introduce
                            // DoS vulnerability.
                            fx_log_warn!(
                                "failed to convert recursive name servers option length to u16: {}, using empty list",
                                err
                            );
                            (&empty, 0)
                        },
                        |len| (recursive_name_servers, len),
                    );
                let () = buf.write_obj_front(&U16::new(len)).expect("buffer is too small");
                recursive_name_servers.iter().for_each(|server_addr| {
                    let () =
                        buf.write_obj_front(&server_addr.octets()).expect("buffer is too small");
                })
            }
            Dhcpv6Option::DomainList(domains) => {
                let empty = Vec::new();
                let (domains, len) =
                    u16::try_from(domains.iter().fold(0, |tot, domain| tot + domain.bytes_len()))
                        .map_or_else(
                            |err| {
                                // Do not panic, so DomainList with size exceeding u16 won't
                                // introduce DoS vulnerability.
                                fx_log_warn!(
                                    "failed to convert domain list option length to u16: {}, using empty list",
                                    err
                                );
                                (&empty, 0)
                            },
                            |len| (domains, len),
                        );
                let () = buf.write_obj_front(&U16::new(len)).expect("buffer is too small");
                domains.iter().for_each(|domain| {
                    domain.serialize(&mut buf);
                })
            }
        }
    }
}

/// A transaction ID defined in [RFC 8415, Section 8].
///
/// [RFC 8415, Section 8]: https://tools.ietf.org/html/rfc8415#section-8
type TransactionId = [u8; 3];

/// A DHCPv6 message as defined in [RFC 8415, Section 8].
///
/// [RFC 8415, Section 8]: https://tools.ietf.org/html/rfc8415#section-8
#[derive(Debug)]
pub struct Dhcpv6Message<'a, B> {
    // TODO(http://fxbug.dev/53401): create getters instead of directly exposing these fields.
    pub msg_type: Dhcpv6MessageType,
    pub transaction_id: &'a TransactionId,
    pub options: Records<B, Dhcpv6OptionsImpl>,
}

impl<'a, B: 'a + ByteSlice> ParsablePacket<B, ()> for Dhcpv6Message<'a, B> {
    type Error = ProtocolError;

    fn parse_metadata(&self) -> ParseMetadata {
        let Self { msg_type, transaction_id, options } = self;
        ParseMetadata::from_packet(
            0,
            mem::size_of_val(msg_type) + mem::size_of_val(transaction_id) + options.bytes().len(),
            0,
        )
    }

    /// Tries to parse a DHCPv6 message by consuming the input buffer. Returns the parsed
    /// `Dhcpv6Message`. If the buffer is malformed, returns a `ProtocolError`.
    fn parse<BV: BufferView<B>>(mut buf: BV, _args: ()) -> Result<Self, Self::Error> {
        let msg_type = Dhcpv6MessageType::try_from(
            buf.take_byte_front().ok_or(ProtocolError::BufferExhausted)?,
        )?;
        let transaction_id =
            buf.take_obj_front::<TransactionId>().ok_or(ProtocolError::BufferExhausted)?.into_ref();
        let options = Records::<_, Dhcpv6OptionsImpl>::parse(buf.take_rest_front())?;
        Ok(Dhcpv6Message { msg_type, transaction_id, options })
    }
}

pub struct Dhcpv6MessageBuilder<'a> {
    msg_type: Dhcpv6MessageType,
    transaction_id: TransactionId,
    options: RecordsSerializer<'a, Dhcpv6OptionsImpl, Dhcpv6Option<'a>, Iter<'a, Dhcpv6Option<'a>>>,
}

impl<'a> Dhcpv6MessageBuilder<'a> {
    pub fn new(
        msg_type: Dhcpv6MessageType,
        transaction_id: TransactionId,
        options: &'a [Dhcpv6Option<'a>],
    ) -> Dhcpv6MessageBuilder<'a> {
        Dhcpv6MessageBuilder {
            msg_type,
            transaction_id,
            options: RecordsSerializer::new(options.iter()),
        }
    }
}

impl InnerPacketBuilder for Dhcpv6MessageBuilder<'_> {
    /// Calculates the serialized length of the DHCPv6 message based on format defined in
    /// [RFC 8415, Section 8].
    ///
    /// [RFC 8415, Section 8]: https://tools.ietf.org/html/rfc8415#section-8
    fn bytes_len(&self) -> usize {
        let Self { msg_type, transaction_id, options } = self;
        mem::size_of_val(msg_type) + mem::size_of_val(transaction_id) + options.records_bytes_len()
    }

    /// Serializes DHCPv6 message based on format defined in [RFC 8415, Section 8].
    ///
    /// # Panics
    ///
    /// If buffer is too small. This means `record_length` is not correctly implemented.
    ///
    /// [RFC 8415, Section 8]: https://tools.ietf.org/html/rfc8415#section-8
    fn serialize(&self, mut buffer: &mut [u8]) {
        let Self { msg_type, transaction_id, options } = self;
        // Implements BufferViewMut, giving us write_obj_front.
        let mut buffer = &mut buffer;
        let () = buffer.write_obj_front(msg_type).expect("buffer is too small");
        let () = buffer.write_obj_front(transaction_id).expect("buffer is too small");
        let () = options.serialize_records(buffer);
    }
}

#[cfg(test)]
mod tests {
    use {super::*, matches::assert_matches};

    fn test_buf_with_no_options() -> Vec<u8> {
        let builder = Dhcpv6MessageBuilder::new(Dhcpv6MessageType::Solicit, [1, 2, 3], &[]);
        let mut buf = vec![0; builder.bytes_len()];
        let () = builder.serialize(&mut buf);
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
            Dhcpv6Option::InformationRefreshTime(86400),
            Dhcpv6Option::DnsServers(vec![
                Ipv6Addr::from([0, 1, 2, 3, 4, 5, 6, 107, 108, 109, 110, 111, 212, 213, 214, 215]),
                Ipv6Addr::from([10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25]),
            ]),
            Dhcpv6Option::DomainList(vec![
                checked::Domain::from_string("fuchsia.dev".to_string())
                    .expect("failed to construct test domain"),
                checked::Domain::from_string("www.google.com".to_string())
                    .expect("failed to construct test domain"),
            ]),
        ];
        let builder = Dhcpv6MessageBuilder::new(Dhcpv6MessageType::Solicit, [1, 2, 3], &options);
        let mut buf = vec![0; builder.bytes_len()];

        let () = builder.serialize(&mut buf);

        assert_eq!(buf.len(), 112);
        #[rustfmt::skip]
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
                0, 32, 0, 4, 0, 1, 81, 128, // option - informtion refresh time
                // option - Dns servers
                0, 23, 0, 32,
                0, 1, 2, 3, 4, 5, 6, 107, 108, 109, 110, 111, 212, 213, 214, 215,
                10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,
                // option - Dns domains
                0, 24, 0, 29,
                7, 102, 117, 99, 104, 115, 105, 97, 3, 100, 101, 118, 0,
                3, 119, 119, 119, 6, 103, 111, 111, 103, 108, 101, 3, 99, 111, 109, 0
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
            Dhcpv6Option::InformationRefreshTime(86400),
            Dhcpv6Option::DnsServers(vec![Ipv6Addr::from(0 as u128)]),
            Dhcpv6Option::DomainList(vec![
                checked::Domain::from_string("fuchsia.dev".to_string())
                    .expect("failed to construct test domain"),
                checked::Domain::from_string("www.google.com".to_string())
                    .expect("failed to construct test domain"),
            ]),
        ];
        let builder = Dhcpv6MessageBuilder::new(Dhcpv6MessageType::Solicit, [1, 2, 3], &options);
        let mut buf = vec![0; builder.bytes_len()];
        let () = builder.serialize(&mut buf);

        let mut buf = &buf[..];
        let msg = Dhcpv6Message::parse(&mut buf, ()).expect("parse should succeed");
        assert_eq!(msg.msg_type, Dhcpv6MessageType::Solicit);
        assert_eq!(msg.transaction_id, &[1, 2, 3]);
        let got_options: Vec<_> = msg.options.iter().collect();
        assert_eq!(got_options, options);
    }

    #[test]
    fn test_message_serialization_duid_too_long() {
        let options = [Dhcpv6Option::ClientId(&[0u8; (u16::MAX as usize) + 1])];
        let builder = Dhcpv6MessageBuilder::new(Dhcpv6MessageType::Solicit, [1, 2, 3], &options);
        let mut buf = vec![0; builder.bytes_len()];
        let () = builder.serialize(&mut buf);

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
        let () = builder.serialize(&mut buf);

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

        let () = <Dhcpv6OptionsImpl as RecordsSerializerImpl>::serialize(&mut buf, &option);
        assert_eq!(buf, [0, 8, 0, 2, 0, 42]);

        let options = Records::<_, Dhcpv6OptionsImpl>::parse_with_context(&buf[..], ()).unwrap();
        let options: Vec<Dhcpv6Option<'_>> = options.iter().collect();
        assert_eq!(options.len(), 1);
        assert_eq!(options[0], Dhcpv6Option::ElapsedTime(42));
    }

    #[test]
    fn test_buffer_too_short() {
        let buf = [];
        assert_matches!(
            Dhcpv6Message::parse(&mut &buf[..], ()),
            Err(ProtocolError::BufferExhausted)
        );

        let buf = [
            1, // valid message type
            0, // transaction id is too short
        ];
        assert_matches!(
            Dhcpv6Message::parse(&mut &buf[..], ()),
            Err(ProtocolError::BufferExhausted)
        );

        let buf = [
            1, // valid message type
            1, 2, 3, // valid transaction id
            0, // option code is too short
        ];
        assert_matches!(
            Dhcpv6Message::parse(&mut &buf[..], ()),
            Err(ProtocolError::BufferExhausted)
        );

        let buf = [
            1, // valid message type
            1, 2, 3, // valida transaction id
            0, 1, // valid option code
            0, // option length is too short
        ];
        assert_matches!(
            Dhcpv6Message::parse(&mut &buf[..], ()),
            Err(ProtocolError::BufferExhausted)
        );

        // option value too short
        let buf = [
            1, // valid message type
            1, 2, 3, // valid transaction id
            0, 1, // valid option code
            0, 100, // valid option length
            1, 2, // option value is too short
        ];
        assert_matches!(
            Dhcpv6Message::parse(&mut &buf[..], ()),
            Err(ProtocolError::BufferExhausted)
        );
    }

    #[test]
    fn test_invalid_message_type() {
        let mut buf = test_buf_with_no_options();
        // 0 is an invalid message type.
        buf[0] = 0;
        assert_matches!(
            Dhcpv6Message::parse(&mut &buf[..], ()),
            Err(ProtocolError::InvalidDhcpv6MessageType(0))
        );
    }

    #[test]
    fn test_invalid_op_code() {
        let mut buf = test_buf_with_no_options();
        buf.append(&mut vec![
            0, 0, // opt code = 0, invalid op code
            0, 1, // valid opt length
            0, // valid opt value
        ]);
        assert_matches!(
            Dhcpv6Message::parse(&mut &buf[..], ()),
            Err(ProtocolError::InvalidOpCode(0))
        );
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
        assert_matches!(
            Dhcpv6Message::parse(&mut &buf[..], ()),
            Err(ProtocolError::InvalidOpLen(Dhcpv6OptionCode::Oro, 1))
        );
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
        assert_matches!(
            Dhcpv6Message::parse(&mut &buf[..], ()),
            Err(ProtocolError::InvalidOpLen(Dhcpv6OptionCode::Preference, 2))
        );
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
        assert_matches!(
            Dhcpv6Message::parse(&mut &buf[..], ()),
            Err(ProtocolError::InvalidOpLen(Dhcpv6OptionCode::ElapsedTime, 3))
        );
    }

    // Information refresh time must have option length 4, according to [RFC 8145, Section 21.23].
    //
    // [RFC 8145, Section 21.23]: https://tools.ietf.org/html/rfc8415#section-21.23
    #[test]
    fn test_information_refresh_time_invalid_opt_len() {
        let mut buf = test_buf_with_no_options();
        buf.append(&mut vec![
            0, 32, // opt code = 32, information refresh time
            0, 3, // invalid opt length, must be 4
            0, 0, 0,
        ]);
        assert_matches!(
            Dhcpv6Message::parse(&mut &buf[..], ()),
            Err(ProtocolError::InvalidOpLen(Dhcpv6OptionCode::InformationRefreshTime, 3))
        );
    }

    // Option length of Dns servers must be multiples of 16, according to [RFC 3646, Section 3].
    //
    // [RFC 3646, Section 3]: https://tools.ietf.org/html/rfc3646#section-3
    #[test]
    fn test_dns_servers_invalid_opt_len() {
        let mut buf = test_buf_with_no_options();
        buf.append(&mut vec![
            0, 23, // opt code = 23, dns servers
            0, 17, // invalid opt length, must be multiple of 16
            0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
        ]);
        assert_matches!(
            Dhcpv6Message::parse(&mut &buf[..], ()),
            Err(ProtocolError::InvalidOpLen(Dhcpv6OptionCode::DnsServers, 17))
        );
    }
}
