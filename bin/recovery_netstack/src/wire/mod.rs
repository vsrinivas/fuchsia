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

pub mod arp;
pub mod ethernet;
pub mod ipv4;
pub mod tcp;
#[cfg(test)]
mod testdata;
pub mod udp;
mod util;

pub use self::ethernet::*;
pub use self::udp::*;
pub use self::util::{ensure_prefix_padding, BufferAndRange};
