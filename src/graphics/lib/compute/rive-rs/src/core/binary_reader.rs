// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{convert::TryInto, str};

use crate::core::property::Writable;

#[derive(Debug)]
pub struct BinaryReader<'b> {
    buffer: &'b [u8],
}

impl Iterator for BinaryReader<'_> {
    type Item = u8;

    fn next(&mut self) -> Option<Self::Item> {
        let result = self.buffer.first().copied()?;
        self.buffer = &self.buffer[1..];

        Some(result)
    }
}

impl<'b> BinaryReader<'b> {
    pub fn new(buffer: &'b [u8]) -> Self {
        Self { buffer }
    }

    pub fn reached_end(&self) -> bool {
        self.buffer.is_empty()
    }

    pub fn read_bytes(&mut self, len: usize) -> Option<&[u8]> {
        let slice = self.buffer.get(..len)?;
        self.buffer = &self.buffer[len..];

        Some(slice)
    }

    pub fn read_var_u64(&mut self) -> Option<u64> {
        let mut result = 0;
        let mut shift = 0;

        loop {
            let byte = self.next()?;

            result |= ((byte & 0b0111_1111) as u64).checked_shl(shift)?;
            shift += 7;

            if byte & 0b1000_0000 == 0 {
                break;
            }
        }

        Some(result)
    }

    pub fn read_string(&mut self) -> Option<String> {
        let len = self.read_var_u64()? as usize;
        let slice = self.read_bytes(len)?;

        str::from_utf8(slice).ok().map(str::to_string)
    }

    pub fn read_f32(&mut self) -> Option<f32> {
        let array = self.read_bytes(4)?.try_into().ok()?;
        Some(f32::from_le_bytes(array))
    }

    pub fn read_f64(&mut self) -> Option<f64> {
        let array = self.read_bytes(8)?.try_into().ok()?;
        Some(f64::from_le_bytes(array))
    }

    pub fn read_u8(&mut self) -> Option<u8> {
        self.next()
    }

    pub fn read_u32(&mut self) -> Option<u32> {
        let array = self.read_bytes(4)?.try_into().ok()?;
        Some(u32::from_le_bytes(array))
    }

    pub fn write<W: Writable>(&mut self, property: &W) -> bool {
        property.write(self).is_some()
    }
}
