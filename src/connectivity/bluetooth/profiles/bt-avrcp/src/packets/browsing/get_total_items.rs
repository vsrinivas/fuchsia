// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::convert::TryFrom;

use crate::packets::{Decodable, Encodable, Error, PacketResult, Scope, StatusCode};

/// GetTotalNumberOfItemsCommand used for retrieving the number of items in a folder.
/// Defined in AVRCP 1.6.2, Section 6.10.4.4.
#[derive(Debug)]
pub struct GetTotalNumberOfItemsCommand {
    scope: Scope,
}

impl GetTotalNumberOfItemsCommand {
    #[cfg(test)]
    pub fn new(scope: Scope) -> Self {
        Self { scope }
    }

    pub fn scope(&self) -> Scope {
        self.scope
    }
}

impl Decodable for GetTotalNumberOfItemsCommand {
    fn decode(buf: &[u8]) -> PacketResult<Self> {
        if buf.len() < 1 {
            return Err(Error::InvalidMessage);
        }

        let scope = Scope::try_from(buf[0])?;

        Ok(Self { scope })
    }
}

impl Encodable for GetTotalNumberOfItemsCommand {
    fn encoded_len(&self) -> usize {
        1
    }

    fn encode(&self, buf: &mut [u8]) -> PacketResult<()> {
        if buf.len() < self.encoded_len() {
            return Err(Error::OutOfRange);
        }

        buf[0] = u8::from(&self.scope);
        Ok(())
    }
}

/// The fixed size of a GetTotalNumberOfItems response.
/// 1 byte for `status`, 2 bytes for `uid_counter`, 4 bytes for `number_of_items`.
/// Defined in AVRCP 1.6.1, Section 6.10.4.4.2.
pub const TOTAL_NUMBER_OF_ITEMS_RESPONSE_SIZE: usize = 7;

/// Response for GetTotalNumberOfItems.
/// Defined in AVRCP 1.6.1, Section 6.10.4.4.
#[derive(Debug)]
pub struct GetTotalNumberOfItemsResponse {
    status: StatusCode,
    uid_counter: u16,
    number_of_items: u32,
}

impl GetTotalNumberOfItemsResponse {
    pub fn new(status: StatusCode, uid_counter: u16, number_of_items: u32) -> Self {
        Self { status, uid_counter, number_of_items }
    }
}

impl Encodable for GetTotalNumberOfItemsResponse {
    fn encoded_len(&self) -> usize {
        TOTAL_NUMBER_OF_ITEMS_RESPONSE_SIZE
    }

    fn encode(&self, buf: &mut [u8]) -> PacketResult<()> {
        if buf.len() < self.encoded_len() {
            return Err(Error::OutOfRange);
        }

        buf[0] = u8::from(&self.status);
        buf[1..3].copy_from_slice(&self.uid_counter.to_be_bytes());
        buf[3..7].copy_from_slice(&self.number_of_items.to_be_bytes());

        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_get_total_number_of_items_command_encode() {
        let cmd = GetTotalNumberOfItemsCommand::new(Scope::Search);

        assert_eq!(cmd.encoded_len(), 1);
        let mut buf = vec![0; cmd.encoded_len()];
        assert_eq!(cmd.encode(&mut buf[..]).map_err(|e| format!("{:?}", e)), Ok(()));
        assert_eq!(buf, &[0x02]);
    }

    #[test]
    fn test_get_total_number_of_items_command_decode() {
        let buf = [0x01];

        let cmd = GetTotalNumberOfItemsCommand::decode(&buf[..]);
        assert!(cmd.is_ok());
        let cmd = cmd.expect("Should work, just checked");
        assert_eq!(cmd.scope, Scope::MediaPlayerVirtualFilesystem);
    }

    #[test]
    fn test_get_total_number_of_items_response_encode() {
        let response = GetTotalNumberOfItemsResponse::new(StatusCode::Success, 10, 1);

        assert_eq!(response.encoded_len(), 7);
        let mut buf = vec![0; response.encoded_len()];
        assert_eq!(response.encode(&mut buf[..]).map_err(|e| format!("{:?}", e)), Ok(()));
        assert_eq!(buf, &[0x04, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x01]);
    }
}
