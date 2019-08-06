// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Utilities for parsing and serializing IPV6CP options.

use {
    crate::records::options::{OptionsImpl, OptionsImplLayout, OptionsSerializerImpl},
    byteorder::{ByteOrder, NetworkEndian},
};

/// An IPV6CP control option.
#[derive(Clone, Eq, Hash, PartialEq, Debug)]
pub enum ControlOption {
    /// Any sequence of bytes not recognized as a valid option.
    Unrecognized(u8, Vec<u8>),
    /// 64-bit identifier which is very likely to be unique on the link.
    InterfaceIdentifier(u64),
}

/// Implementation of the IPV6CP control options parsing and serialization.
pub struct ControlOptionsImpl;

impl ControlOptionsImpl {
    const TYPE_INTERFACE_IDENTIFIER: u8 = 1;
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
            Self::TYPE_INTERFACE_IDENTIFIER => {
                if data.len() == 8 {
                    Ok(Some(ControlOption::InterfaceIdentifier(NetworkEndian::read_u64(&data))))
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
            ControlOption::InterfaceIdentifier(_) => 8,
        }
    }

    fn get_option_kind(option: &Self::Option) -> u8 {
        match option {
            ControlOption::Unrecognized(kind, _) => *kind,
            ControlOption::InterfaceIdentifier(_) => Self::TYPE_INTERFACE_IDENTIFIER,
        }
    }

    fn serialize(data: &mut [u8], option: &Self::Option) {
        match option {
            ControlOption::Unrecognized(_, unrecognized_data) => {
                data.copy_from_slice(&unrecognized_data);
            }
            ControlOption::InterfaceIdentifier(identifier) => {
                NetworkEndian::write_u64(data, *identifier)
            }
        }
    }
}
