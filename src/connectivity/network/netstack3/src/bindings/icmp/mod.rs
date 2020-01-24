// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implements fuchsia.net.icmp FIDL for creating simple sockets for sending and receiving ICMP
//! messages. This is useful for debugging the network stack within other applications.
//!
//! The entry point for all ICMP-related tasks is fuchsia.net.icmp.Provider.

mod echo;
mod provider;

pub(crate) use provider::IcmpProviderWorker;

use super::{InnerValue, LockedStackContext, StackContext};
use fidl_fuchsia_net_icmp as fidl_icmp;
use net_types::{
    ip::{Ip, Ipv4, Ipv4Addr, Ipv6, Ipv6Addr},
    SpecifiedAddr,
};
use netstack3_core::{
    icmp::{self as core_icmp, IcmpConnId, IcmpIpExt},
    SocketError,
};
use std::collections::HashMap;

// ICMP messages are buffered upon receival to allow circumvention of DoS attack vectors.
// Messages are taken from these buffers by calling EchoSocket's `watch` method, giving the client
// the ability to throttle consumption.
const RX_BUFFER_SIZE: usize = 256;

pub(crate) struct EchoSocket {
    reply_tx: futures::channel::mpsc::Sender<fidl_icmp::EchoPacket>,
}

impl EchoSocket {
    pub(crate) fn try_send(
        &mut self,
        packet: fidl_icmp::EchoPacket,
    ) -> Result<(), futures::channel::mpsc::TrySendError<fidl_icmp::EchoPacket>> {
        self.reply_tx.try_send(packet)
    }
}

#[derive(Default)]
pub(crate) struct IcmpEchoSockets {
    v4: HashMap<IcmpConnId<Ipv4>, EchoSocket>,
    v6: HashMap<IcmpConnId<Ipv6>, EchoSocket>,
}

#[derive(Debug, Eq, PartialEq)]
pub(crate) enum InnerIcmpConnId<V4 = IcmpConnId<Ipv4>, V6 = IcmpConnId<Ipv6>> {
    V4(V4),
    V6(V6),
}

impl<I: IpExt> From<IcmpConnId<I>> for InnerIcmpConnId {
    fn from(id: IcmpConnId<I>) -> InnerIcmpConnId {
        I::icmp_conn_id_to_dynamic(id)
    }
}

pub(crate) trait IcmpStackContext: StackContext
where
    <Self as StackContext>::Dispatcher: InnerValue<IcmpEchoSockets>,
{
}

impl<C> IcmpStackContext for C
where
    C: StackContext,
    C::Dispatcher: InnerValue<IcmpEchoSockets>,
{
}

/// An `Ip` extension trait that lets us write more generic code.
///
/// `IpExt` provides generic functionality backed by version-specific
/// implementations, allowing most code to be written agnostic to IP version.
pub(crate) trait IpExt: Ip + IcmpIpExt {
    /// Get the map of ICMP echo sockets.
    fn get_icmp_echo_sockets<D: InnerValue<IcmpEchoSockets>>(
        disp: &mut D,
    ) -> &mut HashMap<IcmpConnId<Self>, EchoSocket>;

    /// Create a new ICMP connection.
    ///
    /// `new_icmp_connection` calls the core functions `new_icmpv4_connection`
    /// or `new_icmpv6_connection` as appropriate.
    fn new_icmp_connection<C: IcmpStackContext>(
        ctx: &mut LockedStackContext<'_, C>,
        local_addr: Option<SpecifiedAddr<Self::Addr>>,
        remote_addr: SpecifiedAddr<Self::Addr>,
        icmp_id: u16,
    ) -> Result<IcmpConnId<Self>, SocketError>
    where
        C::Dispatcher: InnerValue<IcmpEchoSockets>;

    /// Convert a statically-typed ICMP connection ID to a dynamically-typed
    /// one.
    ///
    /// Callers should not call this directly, and should instead call
    /// `ConnId::from` or `ConnId::into`.
    fn icmp_conn_id_to_dynamic(id: IcmpConnId<Self>) -> InnerIcmpConnId;
}

impl IpExt for Ipv4 {
    fn get_icmp_echo_sockets<D: InnerValue<IcmpEchoSockets>>(
        disp: &mut D,
    ) -> &mut HashMap<IcmpConnId<Ipv4>, EchoSocket> {
        &mut disp.inner_mut().v4
    }

    fn new_icmp_connection<C: IcmpStackContext>(
        ctx: &mut LockedStackContext<'_, C>,
        local_addr: Option<SpecifiedAddr<Ipv4Addr>>,
        remote_addr: SpecifiedAddr<Ipv4Addr>,
        icmp_id: u16,
    ) -> Result<IcmpConnId<Ipv4>, SocketError>
    where
        C::Dispatcher: InnerValue<IcmpEchoSockets>,
    {
        core_icmp::new_icmpv4_connection(ctx, local_addr, remote_addr, icmp_id)
    }

    fn icmp_conn_id_to_dynamic(id: IcmpConnId<Ipv4>) -> InnerIcmpConnId {
        InnerIcmpConnId::V4(id)
    }
}

impl IpExt for Ipv6 {
    fn get_icmp_echo_sockets<D: InnerValue<IcmpEchoSockets>>(
        disp: &mut D,
    ) -> &mut HashMap<IcmpConnId<Ipv6>, EchoSocket> {
        &mut disp.inner_mut().v6
    }

    fn new_icmp_connection<C: IcmpStackContext>(
        ctx: &mut LockedStackContext<'_, C>,
        local_addr: Option<SpecifiedAddr<Ipv6Addr>>,
        remote_addr: SpecifiedAddr<Ipv6Addr>,
        icmp_id: u16,
    ) -> Result<IcmpConnId<Ipv6>, SocketError>
    where
        C::Dispatcher: InnerValue<IcmpEchoSockets>,
    {
        core_icmp::new_icmpv6_connection(ctx, local_addr, remote_addr, icmp_id)
    }

    fn icmp_conn_id_to_dynamic(id: IcmpConnId<Ipv6>) -> InnerIcmpConnId {
        InnerIcmpConnId::V6(id)
    }
}
