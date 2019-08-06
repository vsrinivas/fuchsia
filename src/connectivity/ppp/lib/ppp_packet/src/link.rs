// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Utilities for parsing and serializing LCP options.

use {
    crate::records::options::{OptionsImpl, OptionsImplLayout, OptionsSerializerImpl},
    byteorder::{ByteOrder, NetworkEndian},
};

/// An LCP control option.
#[derive(Clone, Eq, Hash, PartialEq, Debug)]
pub enum ControlOption {
    /// Any sequence of bytes not recognized as a valid option.
    Unrecognized(u8, Vec<u8>),
    /// The maximum number of bytes in the Information and Padding fields of a PPP packet.
    MaximumReceiveUnit(u16),
    /// The authentication protocol desired, and any additional data as determined by the
    /// particular protocol.
    AuthenticationProtocol(u16, Vec<u8>),
    /// The link quality monitoring protocol desired, and any additional data as determined by the
    /// particular protocol.
    QualityProtocol(u16, Vec<u8>),
    /// A 32-bit number which is very likely to be unique on the link.
    MagicNumber(u32),
    /// Requests that the protocol field of PPP packets be compressed (if the sent protocol
    /// supports compression).
    ProtocolFieldCompression,
    /// Requests that the address and control fields of the HDLC framing be compressed.
    AddressControlFieldCompression,
}

/// Implementation of the LCP control options parsing and serialization.
#[derive(Copy, Clone)]
pub struct ControlOptionsImpl;

impl ControlOptionsImpl {
    const TYPE_MAXIMUM_RECEIVE_UNIT: u8 = 1;
    const TYPE_AUTHENTICATION_PROTOCOL: u8 = 3;
    const TYPE_QUALITY_PROTOCOL: u8 = 4;
    const TYPE_MAGIC_NUMBER: u8 = 5;
    const TYPE_PROTOCOL_FIELD_COMPRESSION: u8 = 7;
    const TYPE_ADDRESS_CONTROL_FIELD_COMPRESSION: u8 = 8;
}

impl OptionsImplLayout for ControlOptionsImpl {
    type Error = ();
    const END_OF_OPTIONS: Option<u8> = None;
    const NOP: Option<u8> = None;
}

impl<'a> OptionsImpl<'a> for ControlOptionsImpl {
    type Option = ControlOption;

    fn parse(kind: u8, data: &[u8]) -> Result<Option<ControlOption>, ()> {
        match kind {
            Self::TYPE_MAXIMUM_RECEIVE_UNIT => {
                if data.len() == 2 {
                    Ok(Some(ControlOption::MaximumReceiveUnit(NetworkEndian::read_u16(&data))))
                } else {
                    Err(())
                }
            }
            Self::TYPE_AUTHENTICATION_PROTOCOL => {
                if data.len() >= 2 {
                    Ok(Some(ControlOption::AuthenticationProtocol(
                        NetworkEndian::read_u16(&data),
                        data[2..].to_vec(),
                    )))
                } else {
                    Err(())
                }
            }
            Self::TYPE_QUALITY_PROTOCOL => {
                if data.len() >= 2 {
                    Ok(Some(ControlOption::QualityProtocol(
                        NetworkEndian::read_u16(&data),
                        data[2..].to_vec(),
                    )))
                } else {
                    Err(())
                }
            }
            Self::TYPE_MAGIC_NUMBER => {
                if data.len() == 4 {
                    Ok(Some(ControlOption::MagicNumber(NetworkEndian::read_u32(&data))))
                } else {
                    Err(())
                }
            }
            Self::TYPE_PROTOCOL_FIELD_COMPRESSION => {
                if data.is_empty() {
                    Ok(Some(ControlOption::ProtocolFieldCompression))
                } else {
                    Err(())
                }
            }
            Self::TYPE_ADDRESS_CONTROL_FIELD_COMPRESSION => {
                if data.is_empty() {
                    Ok(Some(ControlOption::AddressControlFieldCompression))
                } else {
                    Err(())
                }
            }
            unrecognized => Ok(Some(ControlOption::Unrecognized(unrecognized, data.to_vec()))),
        }
    }
}

impl<'a> OptionsSerializerImpl<'a> for ControlOptionsImpl {
    type Option = ControlOption;

    fn get_option_length(option: &Self::Option) -> usize {
        match option {
            ControlOption::Unrecognized(_, data) => data.len(),
            ControlOption::MaximumReceiveUnit(_) => 2,
            ControlOption::AuthenticationProtocol(_, data)
            | ControlOption::QualityProtocol(_, data) => 2 + data.len(),
            ControlOption::MagicNumber(_) => 4,
            ControlOption::ProtocolFieldCompression
            | ControlOption::AddressControlFieldCompression => 0,
        }
    }

    fn get_option_kind(option: &Self::Option) -> u8 {
        match option {
            ControlOption::Unrecognized(kind, _) => *kind,
            ControlOption::MaximumReceiveUnit(_) => Self::TYPE_MAXIMUM_RECEIVE_UNIT,
            ControlOption::AuthenticationProtocol(_, _) => Self::TYPE_AUTHENTICATION_PROTOCOL,
            ControlOption::QualityProtocol(_, _) => Self::TYPE_QUALITY_PROTOCOL,
            ControlOption::MagicNumber(_) => Self::TYPE_MAGIC_NUMBER,
            ControlOption::ProtocolFieldCompression => Self::TYPE_PROTOCOL_FIELD_COMPRESSION,
            ControlOption::AddressControlFieldCompression => {
                Self::TYPE_ADDRESS_CONTROL_FIELD_COMPRESSION
            }
        }
    }

    fn serialize(data: &mut [u8], option: &Self::Option) {
        match option {
            ControlOption::Unrecognized(_, unrecognized_data) => {
                data.copy_from_slice(unrecognized_data);
            }
            ControlOption::MaximumReceiveUnit(mru) => NetworkEndian::write_u16(data, *mru),
            ControlOption::AuthenticationProtocol(protocol, protocol_data)
            | ControlOption::QualityProtocol(protocol, protocol_data) => {
                NetworkEndian::write_u16(data, *protocol);
                data[2..].copy_from_slice(protocol_data);
            }
            ControlOption::MagicNumber(magic) => NetworkEndian::write_u32(data, *magic),
            ControlOption::ProtocolFieldCompression
            | ControlOption::AddressControlFieldCompression => {}
        }
    }
}
