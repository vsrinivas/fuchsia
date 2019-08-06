// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Utilities for parsing and serializing IPCP options.

use {
    crate::records::options::{OptionsImpl, OptionsImplLayout, OptionsSerializerImpl},
    byteorder::{ByteOrder, NetworkEndian},
};

/// An IPCP control option.
#[derive(Clone, Eq, Hash, PartialEq, Debug)]
pub enum ControlOption {
    /// Any sequence of bytes not recognized as a valid option.
    Unrecognized(u8, Vec<u8>),
    /// The compression protocol desired, and any additional data as determined by the
    /// particular protocol.
    IpCompressionProtocol(u16, Vec<u8>),
    /// The desired local address of the sender.
    IpAddress(u32),
}

/// Implementation of the IPCP control options parsing and serialization.
pub struct ControlOptionsImpl;

impl ControlOptionsImpl {
    const TYPE_IP_COMPRESSION_PROTOCOL: u8 = 2;
    const TYPE_IP_ADDRESS: u8 = 3;
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
            Self::TYPE_IP_COMPRESSION_PROTOCOL => {
                if data.len() >= 2 {
                    Ok(Some(ControlOption::IpCompressionProtocol(
                        NetworkEndian::read_u16(&data),
                        data[2..].to_vec(),
                    )))
                } else {
                    Err(())
                }
            }
            Self::TYPE_IP_ADDRESS => {
                if data.len() == 4 {
                    Ok(Some(ControlOption::IpAddress(NetworkEndian::read_u32(&data))))
                } else {
                    Err(())
                }
            }
            unrecognized => Ok(Some(ControlOption::Unrecognized(unrecognized, data[..].to_vec()))),
        }
    }
}

impl<'a> OptionsSerializerImpl<'a> for ControlOptionsImpl {
    type Option = ControlOption;

    fn get_option_length(option: &Self::Option) -> usize {
        match option {
            ControlOption::Unrecognized(_, data) => data.len(),
            ControlOption::IpCompressionProtocol(_, data) => 2 + data.len(),
            ControlOption::IpAddress(_) => 4,
        }
    }

    fn get_option_kind(option: &Self::Option) -> u8 {
        match option {
            ControlOption::Unrecognized(kind, _) => *kind,
            ControlOption::IpCompressionProtocol(_, _) => Self::TYPE_IP_COMPRESSION_PROTOCOL,
            ControlOption::IpAddress(_) => Self::TYPE_IP_ADDRESS,
        }
    }

    fn serialize(data: &mut [u8], option: &Self::Option) {
        match option {
            ControlOption::Unrecognized(_, unrecognized_data) => {
                data.copy_from_slice(&unrecognized_data);
            }
            ControlOption::IpCompressionProtocol(protocol, protocol_data) => {
                NetworkEndian::write_u16(data, *protocol);
                data[2..].copy_from_slice(&protocol_data);
            }
            ControlOption::IpAddress(address) => NetworkEndian::write_u32(data, *address),
        }
    }
}
