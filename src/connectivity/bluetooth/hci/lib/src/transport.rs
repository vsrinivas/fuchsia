// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_zircon as zx,
    futures::{stream::FusedStream, Sink, Stream},
};

pub mod uart;
pub use uart::Uart;

/// Represents an HCI packet received from the controller.
#[derive(Debug)]
#[cfg_attr(test, derive(PartialEq, Clone))]
pub enum IncomingPacket {
    Event(Vec<u8>),
    Acl(Vec<u8>),
}

impl IncomingPacket {
    pub fn inner(&self) -> &[u8] {
        match self {
            IncomingPacket::Event(b) => &b,
            IncomingPacket::Acl(b) => &b,
        }
    }
}

/// A zero-sized type that cannot be constructed outside of the `transport` module but can be
/// passed around and consumed. It can be exchanged for an `IncomingPacket`. Anyone holding
/// one can be guaranteed that there is a valid packet received from the controller that is
/// buffered in the `HwTransport` because an `IncomingPacketToken` is only created by the
/// `HwTransport` implementation when a packet is ready and the `IncomingPacketToken` must be
/// consumed to take the `IncomingPacket`.
pub struct IncomingPacketToken {
    // Non-public field so that this struct cannot be created outside of the transport module.
    _hidden: (),
}

impl IncomingPacketToken {
    #[allow(dead_code)]
    fn mint() -> IncomingPacketToken {
        IncomingPacketToken { _hidden: () }
    }
}

#[cfg(test)]
impl IncomingPacketToken {
    /// `IncomingPacketToken::mint` is a private method to guarantee that an `IncomingPacketToken`
    /// can only be minted from within the `transport` module. However, test code is allowed to
    /// mint tokens as needed, so this public method is provided to test code for that purpose.
    pub fn mint_in_test() -> IncomingPacketToken {
        IncomingPacketToken::mint()
    }
}

/// Packets to be sent to the controller on behalf of the host.
#[cfg_attr(test, derive(PartialEq, Clone))]
pub enum OutgoingPacket<'a> {
    Cmd(&'a [u8]),
    Acl(&'a [u8]),
}

/// Represents an interface to interact with a Bluetooth controller, abstracting over transport
/// layer specifics.
pub trait HwTransport
where
    for<'a> Self: Unpin
        + FusedStream
        + Sink<OutgoingPacket<'a>, Error = zx::Status>
        + Stream<Item = IncomingPacketToken>,
{
    /// Provide a `IncomingPacketToken` to prove that a packet is ready and available for
    /// consumption and a `Vec<u8>` buffer to write packet data into.
    /// `IncomingPacket` contains a complete packet that is ready to be sent up to the host system.
    ///
    /// `buffer` allows for the caller to pass in an allocated buffer, so that allocations can be
    /// reused. `buffer` will be resized to fit the incoming packet if `buffer` is not large
    /// enough. If buffers are reused, they will quickly grow to fit the maximum size packet seen
    /// and then will not need to be resized unless incoming packet sizes grow. This reduces
    /// number of allocations necessary to a constant factor for a given incoming packet size.
    fn take_incoming(&mut self, _proof: IncomingPacketToken, buffer: Vec<u8>) -> IncomingPacket;

    /// Perform any operations that this object needs to do in the device unbind proceedure.
    /// This can be unsafe in some implementations due to interfacing with C FFI.
    unsafe fn unbind(&mut self);
}

/// Not all implementations of a `HwTransport` can be constructed in a non-async context.
/// All implementations of a `HwTransportBuilder` must be capable of being instantiated in
/// a non-async context. The `HwTransportBuilder` can then be passed to an async context
/// where the HwTransport can finally be built.
pub trait HwTransportBuilder: Send {
    fn build(self: Box<Self>) -> Result<Box<dyn HwTransport>, zx::Status>;
}
