// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! An implementation of the [FIDL wire format] for laying out messages whose types are defined
//! at runtime.
//!
//! [FIDL wire format]: https://fuchsia.dev/fuchsia-src/reference/fidl/language/wire-format

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
        let mut buf = Vec::new();

        // encode the persistent header:
        buf.push(0); // disambiguator
        buf.push(1); // current wire format magic number
        buf.extend(2u16.to_le_bytes()); // v2 wire format
        buf.extend([0; 4]); // reserved with zeroes

        // encode the body of the message
        self.encode(&mut buf);

        buf
    }

    /// Encode this struct without any header.
    pub fn encode(&self, buf: &mut Vec<u8>) {
        // encode the struct's fields:
        if self.fields.is_empty() {
            // A structure can be:
            //
            // * empty — it has no fields. Such a structure is 1 byte in size, with an alignment of
            // 1 byte, and is exactly equivalent to a structure containing a uint8 with the value
            // zero.
            BasicField::UInt8(0).encode_inline(buf);
        } else {
            // encode primary objects first
            for field in &self.fields {
                field.encode_inline(buf);
            }

            for field in &self.fields {
                field.encode_out_of_line(buf);
            }
        }

        // Externally, the structure is aligned on an 8-byte boundary, and may therefore contain
        // final padding to meet that requirement.
        buf.pad_to(8);
    }
}

/// A field of a FIDL struct.
pub enum Field {
    Basic(BasicField),
    Vector(VectorField),
}

impl Field {
    fn alignment(&self) -> usize {
        match self {
            Self::Basic(b) => b.alignment(),
            Self::Vector(l) => l.alignment(),
        }
    }

    fn encode_inline(&self, buf: &mut Vec<u8>) {
        buf.pad_to(self.alignment());
        match self {
            Self::Basic(b) => b.encode_inline(buf),
            Self::Vector(l) => l.encode_inline(buf),
        }
    }

    fn encode_out_of_line(&self, buf: &mut Vec<u8>) {
        match self {
            Self::Basic(_) => (),
            Self::Vector(l) => {
                // each secondary object must be padded to 8 bytes, as well as the primary
                buf.pad_to(8);
                l.encode_out_of_line(buf);
            }
        }
    }
}

pub enum BasicField {
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

impl BasicField {
    fn encode_inline(&self, buf: &mut Vec<u8>) {
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
            _ => 8,
        }
    }
}

pub enum VectorField {
    BoolVector(Vec<bool>),
    UInt8Vector(Vec<u8>),
    UInt16Vector(Vec<u16>),
    UInt32Vector(Vec<u32>),
    UInt64Vector(Vec<u64>),
    Int8Vector(Vec<i8>),
    Int16Vector(Vec<i16>),
    Int32Vector(Vec<i32>),
    Int64Vector(Vec<i64>),
    // TODO(https://fxbug.dev/88174) figure out a better api for nested vectors
    UInt8VectorVector(Vec<Vec<u8>>),
}

impl VectorField {
    fn alignment(&self) -> usize {
        8
    }

    fn encode_inline(&self, buf: &mut Vec<u8>) {
        // Stored as a 16 byte record consisting of:
        //   * `size`: 64-bit unsigned number of elements
        //   * `data`: 64-bit presence indication or pointer to out-of-line element data
        let size = match self {
            Self::BoolVector(v) => v.len(),
            Self::UInt8Vector(v) => v.len(),
            Self::UInt16Vector(v) => v.len(),
            Self::UInt32Vector(v) => v.len(),
            Self::UInt64Vector(v) => v.len(),
            Self::Int8Vector(v) => v.len(),
            Self::Int16Vector(v) => v.len(),
            Self::Int32Vector(v) => v.len(),
            Self::Int64Vector(v) => v.len(),
            Self::UInt8VectorVector(v) => v.len(),
        } as u64;
        buf.extend(size.to_le_bytes());

        // When encoded for transfer, `data` indicates presence of content:
        //   * `0`: vector is absent
        //   * `UINTPTR_MAX`: vector is present, data is the next out-of-line object */
        // (we always encode UINTPTR_MAX because we don't support nullable vectors)
        buf.extend(u64::MAX.to_le_bytes());
    }

    fn encode_out_of_line(&self, buf: &mut Vec<u8>) {
        match self {
            Self::BoolVector(v) => {
                for b in v {
                    BasicField::Bool(*b).encode_inline(buf);
                }
            }
            Self::UInt8Vector(v) => buf.extend(v),
            Self::UInt16Vector(v) => {
                for n in v {
                    BasicField::UInt16(*n).encode_inline(buf);
                }
            }
            Self::UInt32Vector(v) => {
                for n in v {
                    BasicField::UInt32(*n).encode_inline(buf);
                }
            }
            Self::UInt64Vector(v) => {
                for n in v {
                    BasicField::UInt64(*n).encode_inline(buf);
                }
            }
            Self::Int8Vector(v) => {
                for n in v {
                    BasicField::Int8(*n).encode_inline(buf);
                }
            }
            Self::Int16Vector(v) => {
                for n in v {
                    BasicField::Int16(*n).encode_inline(buf);
                }
            }
            Self::Int32Vector(v) => {
                for n in v {
                    BasicField::Int32(*n).encode_inline(buf);
                }
            }
            Self::Int64Vector(v) => {
                for n in v {
                    BasicField::Int64(*n).encode_inline(buf);
                }
            }
            Self::UInt8VectorVector(outer) => {
                let as_fields = outer
                    .iter()
                    .map(|v| Field::Vector(VectorField::UInt8Vector(v.clone())))
                    .collect::<Vec<_>>();

                for field in &as_fields {
                    field.encode_inline(buf);
                }
                for field in &as_fields {
                    field.encode_out_of_line(buf);
                }
            }
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
