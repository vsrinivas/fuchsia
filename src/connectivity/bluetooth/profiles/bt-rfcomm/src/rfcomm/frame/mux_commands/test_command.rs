// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_bluetooth::types::Channel;

use crate::rfcomm::frame::{
    mux_commands::{Decodable, Encodable},
    FrameParseError,
};

/// The maximum size (in bytes) of a Test Command pattern.
/// This is an arbitrarily chosen constant, and is not defined in the RFCOMM/GSM specs.
/// This length is chosen as the maximum amount of data that can fit in a single
/// packet of the underlying Channel.
const TEST_COMMAND_MAX_PATTERN_LENGTH: usize = Channel::DEFAULT_MAX_TX;

/// The Test Command is used to test the connection between two entities in a Session.
/// The command can be arbitrarily sized, and contains a pattern that must be echoed
/// back.
/// See GSM 5.4.6.3.4.
#[derive(Debug, PartialEq)]
pub struct TestCommandParams {
    pub test_pattern: Vec<u8>,
}

impl Decodable for TestCommandParams {
    fn decode(buf: &[u8]) -> Result<Self, FrameParseError> {
        if buf.len() > TEST_COMMAND_MAX_PATTERN_LENGTH {
            return Err(FrameParseError::InvalidFrame);
        }
        Ok(Self { test_pattern: buf.to_vec() })
    }
}

impl Encodable for TestCommandParams {
    fn encoded_len(&self) -> usize {
        self.test_pattern.len()
    }

    fn encode(&self, buf: &mut [u8]) -> Result<(), FrameParseError> {
        if buf.len() < self.encoded_len() {
            return Err(FrameParseError::BufferTooSmall);
        }
        buf[..self.encoded_len()].copy_from_slice(&self.test_pattern);
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use matches::assert_matches;

    #[test]
    fn test_decode_test_command_with_empty_buf() {
        let buf = [];
        let expected = TestCommandParams { test_pattern: vec![] };
        assert_eq!(TestCommandParams::decode(&buf[..]).unwrap(), expected);
    }

    #[test]
    fn test_decode_test_command_with_nonempty_buf() {
        let buf = [0x00, 0x01, 0x02, 0x03];
        let expected = TestCommandParams { test_pattern: buf.to_vec() };
        assert_eq!(TestCommandParams::decode(&buf[..]).unwrap(), expected);
    }

    #[test]
    fn test_encode_buf_too_small() {
        let mut small_buf = [];
        let command = TestCommandParams { test_pattern: vec![0x01, 0x02] };
        assert_matches!(command.encode(&mut small_buf[..]), Err(FrameParseError::BufferTooSmall));
    }

    #[test]
    fn test_encode_larger_buf_is_ok() {
        let mut buf = [0; 3];
        let command = TestCommandParams { test_pattern: vec![0x01, 0x02] };
        assert!(command.encode(&mut buf[..]).is_ok());
        let expected = [0x01, 0x02, 0x00];
        assert_eq!(buf, expected);
    }

    #[test]
    fn test_encode_buf_success() {
        let test_pattern = vec![0x01, 0x02, 0x03];

        let mut buf = [0; 3];
        let command = TestCommandParams { test_pattern: test_pattern.clone() };
        assert!(command.encode(&mut buf[..]).is_ok());
        assert_eq!(test_pattern, buf);
    }
}
