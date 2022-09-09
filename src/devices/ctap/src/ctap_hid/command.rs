// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Error};
use std::convert::TryFrom;

/// CTAPHID commands as defined in https://fidoalliance.org/specs/fido-v2.0-ps-20190130/
/// Note that the logical rather than numeric ordering here matches that in the specification.
#[repr(u8)]
#[derive(Debug, PartialEq, Copy, Clone)]
pub enum Command {
    Msg = 0x03,
    Cbor = 0x10,
    Init = 0x06,
    Ping = 0x01,
    Cancel = 0x11,
    Error = 0x3f,
    Keepalive = 0x3b,
    Wink = 0x08,
    Lock = 0x04,
}

impl TryFrom<u8> for Command {
    type Error = anyhow::Error;

    fn try_from(value: u8) -> Result<Self, Error> {
        match value {
            0x03 => Ok(Self::Msg),
            0x10 => Ok(Self::Cbor),
            0x06 => Ok(Self::Init),
            0x01 => Ok(Self::Ping),
            0x11 => Ok(Self::Cancel),
            0x3f => Ok(Self::Error),
            0x3b => Ok(Self::Keepalive),
            0x08 => Ok(Self::Wink),
            0x04 => Ok(Self::Lock),
            value => Err(format_err!("Invalid command: {:?}", value)),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn command_conversion() {
        // Verify that every integer that maps to a valid command is also the value of that
        // command. Note we don't have runtime enum information to test all commands are accessible
        // from an integer.
        for i in 0..=std::u8::MAX {
            if let Ok(command) = Command::try_from(i) {
                assert_eq!(command as u8, i);
            }
        }
    }
}
