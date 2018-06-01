// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Serialization and deserialization of wire formats.
//!
//! This module provides efficient serialization and deserialization of the
//! various wire formats used by this program. Where possible, it uses lifetimes
//! and immutability to allow for safe zero-copy parsing.
//!
//! # Endianness
//!
//! All values exposed or consumed by this crate are in host byte order, so the
//! caller does not need to worry about it. Any necessary conversions are
//! performed under the hood.

mod ethernet;
mod udp;
mod util;

pub use self::ethernet::*;
pub use self::udp::*;

use std::fmt::Debug;

// We use a trait rather than a concrete type (such as the enum Err below) so
// that we are free to change what error type we use in the future. We may
// eventually switch from returning 'impl ParseErr' to using a concrete type
// once we get enough experience with this.

/// Parsing errors.
///
/// All errors returned from parsing functions in this module implement
/// `ParseErr`.
pub trait ParseErr: Debug {
    /// Is this a checksum-related error?
    fn is_checksum(&self) -> bool;
}

#[derive(Eq, PartialEq, Debug)]
enum Err {
    Format,
    Checksum,
}

impl ParseErr for Err {
    fn is_checksum(&self) -> bool {
        *self == Err::Checksum
    }
}
