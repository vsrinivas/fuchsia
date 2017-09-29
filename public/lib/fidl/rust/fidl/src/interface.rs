// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Traits and utility functions for interface "pointers" and interface requests

use zircon::{Channel, HandleBased};

use {Encodable, Decodable, EncodeBuf, DecodeBuf, EncodableType, Result};
use {EncodableNullable, DecodableNullable};

/// A FIDL endpoint.
pub trait Endpoint: HandleBased {
    /// Convert the endpoint into a channel.
    fn into_channel(self) -> Channel {
        Channel::from(self.into())
    }
}

/// FIDL pointer to a type that implements a given interface.
pub struct InterfacePtr<E> {
    /// The inner type that implements the interface.
    pub inner: E,
    /// Which version of the interface is present.
    pub version: u32,
}

impl<E: HandleBased + Encodable> Encodable for InterfacePtr<E> {
    fn encode(self, buf: &mut EncodeBuf, base: usize, offset: usize) {
        let start = base + offset;
        self.inner.encode(buf, start, 0);
        self.version.encode(buf, start, 4)
    }

    fn encodable_type() -> EncodableType {
        EncodableType::InterfacePtr
    }

    fn size() -> usize {
        8
    }
}

impl<E: HandleBased + Decodable> Decodable for InterfacePtr<E> {
    fn decode(buf: &mut DecodeBuf, base: usize, offset: usize) -> Result<Self> {
        let start = base + offset;
        let inner = try!(E::decode(buf, start, 0));
        let version = try!(u32::decode(buf, start, 4));
        Ok(InterfacePtr {
            inner: inner,
            version: version
        })
    }
}

// Support for Option<InterfacePtr<E>>

impl<E: HandleBased + Encodable> EncodableNullable for InterfacePtr<E> {
    type NullType = u32;
    fn null_value() -> u32 { !0u32 }
}

impl<E: HandleBased + Decodable> DecodableNullable for InterfacePtr<E> {
}
