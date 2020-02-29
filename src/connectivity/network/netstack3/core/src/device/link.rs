// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Link device (L2) definitions.
//!
//! This module contains definitions of link-layer devices, otherwise known as
//! L2 devices.

use core::fmt::Debug;

use net_types::ethernet::Mac;
use zerocopy::{AsBytes, FromBytes, Unaligned};

/// The type of address used by a link device.
pub(crate) trait LinkAddress:
    'static + FromBytes + AsBytes + Unaligned + Copy + Clone + Debug + Eq
{
    /// The length of the address in bytes.
    const BYTES_LENGTH: usize;

    /// Returns the underlying bytes of a `LinkAddress`.
    fn bytes(&self) -> &[u8];

    /// Constructs a `LinkLayerAddress` from the provided bytes.
    ///
    /// # Panics
    ///
    /// `from_bytes` may panic if `bytes` is not **exactly** [`BYTES_LENGTH`]
    /// long.
    fn from_bytes(bytes: &[u8]) -> Self;
}

/// A [`LinkAddress`] with a broadcast value.
///
/// A `BroadcastLinkAddress` is a `LinkAddress` for which at least one address
/// is a "broadcast" address, indicating that a frame should be received by all
/// hosts on a link.
pub(crate) trait BroadcastLinkAddress: LinkAddress {
    /// The broadcast address.
    ///
    /// If the addressing scheme supports multiple broadcast addresses, then
    /// there is no requirement as to which one is chosen for this constant.
    const BROADCAST: Self;
}

impl LinkAddress for Mac {
    const BYTES_LENGTH: usize = 6;

    fn bytes(&self) -> &[u8] {
        self.as_ref()
    }

    fn from_bytes(bytes: &[u8]) -> Self {
        // assert that contract is being held:
        debug_assert_eq!(bytes.len(), Self::BYTES_LENGTH);
        let mut b = [0; Self::BYTES_LENGTH];
        b.copy_from_slice(bytes);
        Self::new(b)
    }
}

impl BroadcastLinkAddress for Mac {
    const BROADCAST: Mac = Mac::BROADCAST;
}

/// A link device.
///
/// `LinkDevice` is used to identify a particular link device implementation. It
/// is only intended to exist at the type level, never instantiated at runtime.
pub(crate) trait LinkDevice: 'static + Copy + Clone {
    /// The type of address used to address link devices of this type.
    type Address: LinkAddress;
}
