// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The Internet Protocol, versions 4 and 6.

mod address;

pub use self::address::*;

/// An IP protocol or next header number.
///
/// For IPv4, this is the protocol number. For IPv6, this is the next header
/// number.
#[allow(missing_docs)]
#[repr(u8)]
pub enum IpProto {
    Tcp = 6,
    Udp = 17,
}
