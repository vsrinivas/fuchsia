// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A collection of types that represent the various parts of socket addresses.

use core::{convert::Infallible as Never, marker::PhantomData, num::NonZeroU16};

use net_types::{
    ip::{Ip, IpAddress},
    SpecifiedAddr,
};

use crate::{ip::IpDeviceId, socket::SocketMapAddrSpec};

/// The IP address and identifier (port) of a listening socket.
#[derive(Copy, Clone, Debug, Eq, Hash, PartialEq)]
pub(crate) struct ListenerIpAddr<A: IpAddress, LI> {
    /// The specific address being listened on, or `None` for all addresses.
    pub(crate) addr: Option<SpecifiedAddr<A>>,
    /// The local identifier (i.e. port for TCP/UDP).
    pub(crate) identifier: LI,
}

/// The address of a listening socket.
#[derive(Copy, Clone, Debug, Eq, Hash, PartialEq)]
pub(crate) struct ListenerAddr<A: IpAddress, D, P> {
    pub(crate) ip: ListenerIpAddr<A, P>,
    pub(crate) device: Option<D>,
}

// The IP address and identifier (port) of a connected socket.
#[derive(Copy, Clone, Debug, Eq, Hash, PartialEq)]
pub(crate) struct ConnIpAddr<A: IpAddress, LI, RI> {
    pub(crate) local: (SpecifiedAddr<A>, LI),
    pub(crate) remote: (SpecifiedAddr<A>, RI),
}

/// The address of a connected socket.
#[derive(Copy, Clone, Debug, Eq, Hash, PartialEq)]
pub(crate) struct ConnAddr<A: IpAddress, D, LI, RI> {
    pub(crate) ip: ConnIpAddr<A, LI, RI>,
    pub(crate) device: Option<D>,
}

/// Uninstantiable type used to implement [`SocketMapAddrSpec`] for addresses
/// with IP addresses and 16-bit local and remote port identifiers.
pub(crate) struct IpPortSpec<I, D>(PhantomData<(I, D)>, Never);

impl<I: Ip, D: IpDeviceId> SocketMapAddrSpec for IpPortSpec<I, D> {
    type IpAddr = I::Addr;
    type DeviceId = D;
    type RemoteIdentifier = NonZeroU16;
    type LocalIdentifier = NonZeroU16;
}
