// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! An implementation of the [FIDL wire format] for laying out messages whose types are defined
//! at runtime.
//!
//! [FIDL wire format]: https://fuchsia.dev/fuchsia-src/reference/fidl/language/wire-format

use fidl::encoding::{create_persistent_header, encode_persistent_header};
use std::default::Default;

/// A FIDL struct for encoding. Fields are defined in order.
pub struct Structure {
    fields: Vec<Field>,
}

impl Default for Structure {
    fn default() -> Self {
        Structure { fields: vec![] }
    }
}

impl Structure {
    /// Add a field and its value to this dynamic struct definition.
    pub fn field(mut self, field: Field) -> Self {
        self.fields.push(field);
        self
    }

    /// Encode this struct into it's [persistent message encoding].
    ///
    /// [persistent message encoding]: https://fuchsia.dev/fuchsia-src/contribute/governance/rfcs/0120_standalone_use_of_fidl_wire_format
    pub fn encode_persistent(&self) -> Vec<u8> {
        // TODO(https://fxbug.dev/86328) we should have our own implementation of this header
        let mut header = create_persistent_header();
        let mut buf = encode_persistent_header(&mut header).unwrap();

        if self.fields.is_empty() {
            // A structure can be:
            //
            // * empty — it has no fields. Such a structure is 1 byte in size, with an alignment of
            // 1 byte, and is exactly equivalent to a structure containing a uint8 with the value
            // zero.
            Field::UInt8(0).encode_inline(&mut buf);
        } else {
            // encode primary objects first
            for field in &self.fields {
                field.encode_inline(&mut buf);
            }
        }

        // Externally, the structure is aligned on an 8-byte boundary, and may therefore contain
        // final padding to meet that requirement.
        buf.pad_to(8);

        buf
    }
}

/// A field of a FIDL struct.
pub enum Field {
    Bool(bool),
    UInt8(u8),
    UInt16(u16),
    UInt32(u32),
    UInt64(u64),
    Int8(i8),
    Int16(i16),
    Int32(i32),
    Int64(i64),
}

impl Field {
    fn encode_inline(&self, buf: &mut Vec<u8>) {
        buf.pad_to(self.alignment());

        match self {
            Self::Bool(b) => buf.push(if *b { 1u8 } else { 0u8 }),
            Self::UInt8(n) => buf.push(*n),
            Self::UInt16(n) => buf.extend(n.to_le_bytes()),
            Self::UInt32(n) => buf.extend(n.to_le_bytes()),
            Self::UInt64(n) => buf.extend(n.to_le_bytes()),
            Self::Int8(n) => buf.extend(n.to_le_bytes()),
            Self::Int16(n) => buf.extend(n.to_le_bytes()),
            Self::Int32(n) => buf.extend(n.to_le_bytes()),
            Self::Int64(n) => buf.extend(n.to_le_bytes()),
        }
    }

    fn alignment(&self) -> usize {
        match self {
            Self::Bool(_) | Self::UInt8(_) | Self::Int8(_) => 1,
            Self::UInt16(_) | Self::Int16(_) => 2,
            Self::UInt32(_) | Self::Int32(_) => 4,
            Self::UInt64(_) | Self::Int64(_) => 8,
        }
    }
}

trait Padding {
    fn pad_to(&mut self, align: usize);
}

impl Padding for Vec<u8> {
    fn pad_to(&mut self, align: usize) {
        let start_len = self.len();
        let num_bytes = (align - (start_len % align)) % align;
        self.resize(start_len + num_bytes, 0);
    }
}
