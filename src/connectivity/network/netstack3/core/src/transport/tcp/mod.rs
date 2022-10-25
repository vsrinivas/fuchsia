// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The Transmission Control Protocol (TCP).

pub mod buffer;
mod rtt;
pub mod segment;
mod seqnum;
pub mod socket;
pub mod state;

use rand::RngCore;

use crate::{
    ip::{IpDeviceId, IpExt},
    sync::Mutex,
    transport::tcp::socket::{isn::IsnGenerator, TcpNonSyncContext, TcpSockets},
};

/// Control flags that can alter the state of a TCP control block.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum Control {
    /// Corresponds to the SYN bit in a TCP segment.
    SYN,
    /// Corresponds to the FIN bit in a TCP segment.
    FIN,
    /// Corresponds to the RST bit in a TCP segment.
    RST,
}

impl Control {
    /// Returns whether the control flag consumes one byte from the sequence
    /// number space.
    fn has_sequence_no(self) -> bool {
        match self {
            Control::SYN | Control::FIN => true,
            Control::RST => false,
        }
    }
}

/// Errors surfaced to the user.
#[derive(Debug)]
#[cfg_attr(test, derive(PartialEq, Eq))]
pub(crate) enum UserError {
    /// The connection was reset because of a RST segment.
    ConnectionReset,
    /// The connection was closed because of a user request.
    ConnectionClosed,
}

pub(crate) struct TcpState<I: IpExt, D: IpDeviceId, C: TcpNonSyncContext> {
    pub(crate) isn_generator: IsnGenerator<C::Instant>,
    pub(crate) sockets: Mutex<TcpSockets<I, D, C>>,
}

impl<I: IpExt, D: IpDeviceId, C: TcpNonSyncContext> TcpState<I, D, C> {
    pub(crate) fn new(now: C::Instant, rng: &mut impl RngCore) -> Self {
        Self {
            isn_generator: IsnGenerator::new(now, rng),
            sockets: Mutex::new(TcpSockets::new(rng)),
        }
    }
}

/// Named tuple for holding sizes of buffers for a socket.
///
/// TODO(https://fxbug.dev/110625): Use this to implement setting SO_SNDBUF.
#[derive(Copy, Clone, Debug, Default)]
#[cfg_attr(test, derive(Eq, PartialEq))]
pub struct BufferSizes {}
