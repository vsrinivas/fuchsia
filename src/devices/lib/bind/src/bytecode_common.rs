// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::errors::UserError;
use byteorder::ByteOrder;
use std::fmt;
use thiserror::Error;

// Common functions and enums for interpreting the bytecode.

#[allow(dead_code)]
#[derive(Debug, Error, Clone, PartialEq)]
pub enum BytecodeError {
    UnexpectedEnd,
    InvalidHeader(u32, u32),
    InvalidVersion(u32),
    InvalidStringLength,
    EmptyString,
    Utf8ConversionFailure,
    InvalidSymbolTableKey(u32),
    IncorrectSectionSize,
}

impl fmt::Display for BytecodeError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", UserError::from(self.clone()))
    }
}

pub type BytecodeIter = std::vec::IntoIter<u8>;

pub fn next_u8(iter: &mut BytecodeIter) -> Result<u8, BytecodeError> {
    iter.next().ok_or(BytecodeError::UnexpectedEnd)
}

#[allow(dead_code)]
pub fn next_u32(iter: &mut BytecodeIter) -> Result<u32, BytecodeError> {
    let mut bytes: [u8; 4] = [0; 4];
    for i in 0..4 {
        bytes[i] = next_u8(iter)?;
    }

    Ok(byteorder::LittleEndian::read_u32(&bytes))
}

// Return the next four bytes in the iterator as a u32. If the iterator is empty,
// return None.
pub fn try_next_u32(iter: &mut BytecodeIter) -> Result<Option<u32>, BytecodeError> {
    let mut bytes: [u8; 4] = [0; 4];
    if let Some(byte) = iter.next() {
        bytes[0] = byte;
    } else {
        return Ok(None);
    }

    for i in 1..4 {
        bytes[i] = next_u8(iter)?;
    }

    Ok(Some(byteorder::LittleEndian::read_u32(&bytes)))
}
