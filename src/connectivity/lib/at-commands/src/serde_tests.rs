// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Tests for AT command serialization and deserialization with defragmentation.
#![cfg(test)]

use crate::{
    highlevel::Command,
    serde::{DeserializeBytes, DeserializeError, DeserializeErrorCause, DeserializeResult, SerDe},
};
use {assert_matches::assert_matches, std::io::Cursor};

#[test]
fn serialize() {
    let commands = vec![Command::Testex {}, Command::Testex {}];
    let mut bytes = Vec::new();
    let result = Command::serialize(&mut bytes, &commands);
    result.expect("Failed to serialize.");
    assert_eq!(bytes, b"ATTESTEX\rATTESTEX\r");
}

#[test]
fn one_command_deserialize() {
    let bytes = b"ATTESTEX\r";
    let result = Command::deserialize(&mut Cursor::new(bytes), DeserializeBytes::new());
    assert_eq!(
        result,
        DeserializeResult {
            values: vec!(Command::Testex {}),
            error: None,
            remaining_bytes: DeserializeBytes { bytes: b"\r".to_vec() }
        }
    );
}

#[test]
fn newline_terminates() {
    let bytes = b"ATTESTEX\n";
    let result = Command::deserialize(&mut Cursor::new(bytes), DeserializeBytes::new());
    assert_eq!(
        result,
        DeserializeResult {
            values: vec!(Command::Testex {}),
            error: None,
            remaining_bytes: DeserializeBytes { bytes: b"\n".to_vec() }
        }
    );
}

#[test]
fn one_command_no_final_cr() {
    let bytes = b"ATTESTEX";
    let result = Command::deserialize(&mut Cursor::new(bytes), DeserializeBytes::new());
    assert_eq!(
        result,
        DeserializeResult {
            values: Vec::new(),
            error: None,
            remaining_bytes: DeserializeBytes { bytes: b"ATTESTEX".to_vec() }
        }
    );
}

#[test]
fn multiple_command_deserialize() {
    let bytes = b"ATTESTEX\rATTESTEX\r";
    let result = Command::deserialize(&mut Cursor::new(bytes), DeserializeBytes::new());
    assert_eq!(
        result,
        DeserializeResult {
            values: vec!(Command::Testex {}, Command::Testex {}),
            error: None,
            remaining_bytes: DeserializeBytes { bytes: b"\r".to_vec() }
        }
    );
}

#[test]
fn multipart_command_deserialize() {
    let bytes1 = b"ATTES";
    let bytes2 = b"TEX\r";
    let result1 = Command::deserialize(&mut Cursor::new(bytes1), DeserializeBytes::new());
    assert_eq!(
        result1,
        DeserializeResult {
            values: Vec::new(),
            error: None,
            remaining_bytes: DeserializeBytes { bytes: b"ATTES".to_vec() }
        }
    );
    let result2 = Command::deserialize(&mut Cursor::new(bytes2), result1.remaining_bytes);
    assert_eq!(
        result2,
        DeserializeResult {
            values: vec!(Command::Testex {}),
            error: None,
            remaining_bytes: DeserializeBytes { bytes: b"\r".to_vec() }
        }
    )
}

#[test]
fn multiple_and_multipart_command_deserialize() {
    let bytes1 = b"ATTESTEX\rATTES";
    let bytes2 = b"TEX\r";
    let result1 = Command::deserialize(&mut Cursor::new(bytes1), DeserializeBytes::new());
    assert_eq!(
        result1,
        DeserializeResult {
            values: vec!(Command::Testex {}),
            error: None,
            remaining_bytes: DeserializeBytes { bytes: b"\rATTES".to_vec() }
        }
    );
    let result2 = Command::deserialize(&mut Cursor::new(bytes2), result1.remaining_bytes);
    assert_eq!(
        result2,
        DeserializeResult {
            values: vec!(Command::Testex {}),
            error: None,
            remaining_bytes: DeserializeBytes { bytes: b"\r".to_vec() }
        }
    )
}

#[test]
fn deserialize_error() {
    let bytes = b"ATNOTACOMMAND\r";
    let result = Command::deserialize(&mut Cursor::new(bytes), DeserializeBytes::new());
    assert_matches!(
        result,
        DeserializeResult {
            values,
            error: Some(DeserializeError {
                cause: DeserializeErrorCause::UnknownCommand(_),
                bytes: error_bytes
            }),
            remaining_bytes: DeserializeBytes { bytes }
        } if
              error_bytes == b"ATNOTACOMMAND".to_vec() &&
              values == Vec::new() &&
              bytes == b"\r".to_vec()
    )
}

#[test]
fn deserialize_error_and_continue() {
    let bytes = b"ATNOTACOMMAND\rATTESTEX\r";
    let result1 = Command::deserialize(&mut Cursor::new(bytes), DeserializeBytes::new());
    assert_matches!(
        result1,
        DeserializeResult {
            values,
            error: Some(DeserializeError {
                cause: DeserializeErrorCause::UnknownCommand(_),
                bytes: error_bytes
            }),
            remaining_bytes: DeserializeBytes { ref bytes }
        } if
              error_bytes == b"ATNOTACOMMAND".to_vec() &&
              values == Vec::new() &&
              bytes == &b"\rATTESTEX\r".to_vec()
    );
    let result2 = Command::deserialize(&mut Cursor::new([]), result1.remaining_bytes);
    assert_eq!(
        result2,
        DeserializeResult {
            values: vec!(Command::Testex {}),
            error: None,
            remaining_bytes: DeserializeBytes { bytes: b"\r".to_vec() }
        }
    )
}

#[test]
fn deserialize_error_and_continue_with_more_bytes() {
    let bytes1 = b"ATNOTACOMMAND\rATTES";
    let bytes2 = b"TEX\rATTESTEX\rATTEST";
    let result1 = Command::deserialize(&mut Cursor::new(bytes1), DeserializeBytes::new());
    assert_matches!(
        result1,
        DeserializeResult {
            values,
            error: Some(DeserializeError {
                cause: DeserializeErrorCause::UnknownCommand(_),
                bytes: error_bytes
            }),
            remaining_bytes: DeserializeBytes { ref bytes }
        } if
              error_bytes == b"ATNOTACOMMAND".to_vec() &&
              values == Vec::new() &&
              bytes == &b"\rATTES".to_vec()
    );
    let result2 = Command::deserialize(&mut Cursor::new(bytes2), result1.remaining_bytes);
    assert_eq!(
        result2,
        DeserializeResult {
            values: vec!(Command::Testex {}, Command::Testex {}),
            error: None,
            remaining_bytes: DeserializeBytes { bytes: b"\rATTEST".to_vec() }
        }
    )
}
