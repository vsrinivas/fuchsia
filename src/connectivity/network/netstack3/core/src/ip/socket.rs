// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! IPv4 and IPv6 sockets.

use alloc::vec::Vec;
use core::cmp::Ordering;
use core::convert::Infallible;
use core::num::NonZeroU8;

use net_types::ip::{Ip, Ipv4, Ipv6, Ipv6Addr};
use net_types::{SpecifiedAddr, UnicastAddr};
use packet::{Buf, BufferMut, SerializeError, Serializer};

use thiserror::Error;

use crate::{
    context::{CounterContext, InstantContext},
    ip::{
        device::state::{IpDeviceStateIpExt, Ipv6AddressEntry},
        forwarding::Destination,
        IpDeviceIdContext, IpExt, SendIpPacketMeta,
    },
};

/// An execution context defining a type of IP socket.
pub(crate) trait IpSocketHandler<I: IpExt, C>: IpDeviceIdContext<I> {
    /// Constructs a new [`IpSock`].
    ///
    /// `new_ip_socket` constructs a new `IpSock` to the given remote IP
    /// address from the given local IP address with the given IP protocol. If
    /// no local IP address is given, one will be chosen automatically. If
    /// `device` is `Some`, the socket will be bound to the given device - only
    /// routes which egress over the device will be used. If no route is
    /// available which egresses over the device - even if routes are available
    /// which egress over other devices - the socket will be considered
    /// unroutable.
    ///
    /// `new_ip_socket` returns an error if no route to the remote was found in
    /// the forwarding table or if the given local IP address is not valid for
    /// the found route.
    ///
    /// The builder may be used to override certain default parameters. Passing
    /// `None` for the `builder` parameter is equivalent to passing
    /// `Some(Default::default())`.
    fn new_ip_socket<O>(
        &mut self,
        ctx: &mut C,
        device: Option<&Self::DeviceId>,
        local_ip: Option<SpecifiedAddr<I::Addr>>,
        remote_ip: SpecifiedAddr<I::Addr>,
        proto: I::Proto,
        options: O,
    ) -> Result<IpSock<I, Self::DeviceId, O>, (IpSockCreationError, O)>;
}

/// An error in sending a packet on an IP socket.
#[derive(Error, Copy, Clone, Debug, Eq, PartialEq)]
pub enum IpSockSendError {
    /// An MTU was exceeded.
    ///
    /// This could be caused by an MTU at any layer of the stack, including both
    /// device MTUs and packet format body size limits.
    #[error("a maximum transmission unit (MTU) was exceeded")]
    Mtu,
    /// The socket is currently unroutable.
    #[error("the socket is currently unroutable: {}", _0)]
    Unroutable(#[from] IpSockUnroutableError),
}

impl From<SerializeError<Infallible>> for IpSockSendError {
    fn from(err: SerializeError<Infallible>) -> IpSockSendError {
        match err {
            SerializeError::Alloc(err) => match err {},
            SerializeError::Mtu => IpSockSendError::Mtu,
        }
    }
}

/// An error in sending a packet on a temporary IP socket.
#[derive(Error, Copy, Clone, Debug)]
pub enum IpSockCreateAndSendError {
    /// An MTU was exceeded.
    ///
    /// This could be caused by an MTU at any layer of the stack, including both
    /// device MTUs and packet format body size limits.
    #[error("a maximum transmission unit (MTU) was exceeded")]
    Mtu,
    /// The temporary socket could not be created.
    #[error("the temporary socket could not be created: {}", _0)]
    Create(#[from] IpSockCreationError),
}

/// An extension of [`IpSocketHandler`] adding the ability to send packets on an
/// IP socket.
pub(crate) trait BufferIpSocketHandler<I: IpExt, C, B: BufferMut>:
    IpSocketHandler<I, C>
{
    /// Sends an IP packet on a socket.
    ///
    /// The generated packet has its metadata initialized from `socket`,
    /// including the source and destination addresses, the Time To Live/Hop
    /// Limit, and the Protocol/Next Header. The outbound device is also chosen
    /// based on information stored in the socket.
    ///
    /// `mtu` may be used to optionally impose an MTU on the outgoing packet.
    /// Note that the device's MTU will still be imposed on the packet. That is,
    /// the smaller of `mtu` and the device's MTU will be imposed on the packet.
    ///
    /// If the socket is currently unroutable, an error is returned.
    fn send_ip_packet<S: Serializer<Buffer = B>, O: SendOptions<I>>(
        &mut self,
        ctx: &mut C,
        socket: &IpSock<I, Self::DeviceId, O>,
        body: S,
        mtu: Option<u32>,
    ) -> Result<(), (S, IpSockSendError)>;

    /// Creates a temporary IP socket and sends a single packet on it.
    ///
    /// `local_ip`, `remote_ip`, `proto`, and `builder` are passed directly to
    /// [`IpSocketHandler::new_ip_socket`]. `get_body_from_src_ip` is given the
    /// source IP address for the packet - which may have been chosen
    /// automatically if `local_ip` is `None` - and returns the body to be
    /// encapsulated. This is provided in case the body's contents depend on the
    /// chosen source IP address.
    ///
    /// If `device` is specified, the available routes are limited to those that
    /// egress over the device.
    ///
    /// `mtu` may be used to optionally impose an MTU on the outgoing packet.
    /// Note that the device's MTU will still be imposed on the packet. That is,
    /// the smaller of `mtu` and the device's MTU will be imposed on the packet.
    ///
    /// # Errors
    ///
    /// If an error is encountered while sending the packet, the body returned
    /// from `get_body_from_src_ip` will be returned along with the error. If an
    /// error is encountered while constructing the temporary IP socket,
    /// `get_body_from_src_ip` will be called on an arbitrary IP address in
    /// order to obtain a body to return. In the case where a buffer was passed
    /// by ownership to `get_body_from_src_ip`, this allows the caller to
    /// recover that buffer.
    fn send_oneshot_ip_packet<
        S: Serializer<Buffer = B>,
        F: FnOnce(SpecifiedAddr<I::Addr>) -> S,
        O: SendOptions<I>,
    >(
        &mut self,
        ctx: &mut C,
        device: Option<&Self::DeviceId>,
        local_ip: Option<SpecifiedAddr<I::Addr>>,
        remote_ip: SpecifiedAddr<I::Addr>,
        proto: I::Proto,
        options: O,
        get_body_from_src_ip: F,
        mtu: Option<u32>,
    ) -> Result<(), (S, IpSockCreateAndSendError, O)> {
        // We use a `match` instead of `map_err` because `map_err` would require passing a closure
        // which takes ownership of `get_body_from_src_ip`, which we also use in the success case.
        match self.new_ip_socket(ctx, device, local_ip, remote_ip, proto, options) {
            Err((err, options)) => {
                Err((get_body_from_src_ip(I::LOOPBACK_ADDRESS), err.into(), options))
            }
            Ok(tmp) => self
                .send_ip_packet(ctx, &tmp, get_body_from_src_ip(*tmp.local_ip()), mtu)
                .map_err(|(body, err)| match err {
                    IpSockSendError::Mtu => {
                        let IpSock { options, definition: _ } = tmp;
                        (body, IpSockCreateAndSendError::Mtu, options)
                    }
                    IpSockSendError::Unroutable(_) => {
                        unreachable!("socket which was just created should still be routable")
                    }
                }),
        }
    }
}

/// An error encountered when creating an IP socket.
#[derive(Error, Copy, Clone, Debug, Eq, PartialEq)]
pub enum IpSockCreationError {
    /// An error occurred while looking up a route.
    #[error("a route cannot be determined: {}", _0)]
    Route(#[from] IpSockRouteError),
}

/// An error encountered when looking up a route for an IP socket.
#[derive(Error, Copy, Clone, Debug, Eq, PartialEq)]
pub enum IpSockRouteError {
    /// No local IP address was specified, and one could not be automatically
    /// selected.
    #[error("a local IP address could not be automatically selected")]
    NoLocalAddrAvailable,
    /// The socket is unroutable.
    #[error("the socket is unroutable: {}", _0)]
    Unroutable(#[from] IpSockUnroutableError),
}

/// An error encountered when attempting to compute the routing information on
/// an IP socket.
///
/// An `IpSockUnroutableError` can occur when creating a socket or when updating
/// an existing socket in response to changes to the forwarding table or to the
/// set of IP addresses assigned to devices.
#[derive(Error, Copy, Clone, Debug, Eq, PartialEq)]
pub enum IpSockUnroutableError {
    /// The specified local IP address is not one of our assigned addresses.
    ///
    /// For IPv6, this error will also be returned if the specified local IP
    /// address exists on one of our devices, but it is in the "temporary"
    /// state.
    #[error("the specified local IP address is not one of our assigned addresses")]
    LocalAddrNotAssigned,
    /// No route exists to the specified remote IP address.
    #[error("no route exists to the remote IP address")]
    NoRouteToRemoteAddr,
}

/// An IP socket.
#[derive(Clone, Debug)]
#[cfg_attr(test, derive(PartialEq))]
pub(crate) struct IpSock<I: IpExt, D, O> {
    /// The definition of the socket.
    ///
    /// This does not change for the lifetime of the socket.
    definition: IpSockDefinition<I, D>,
    /// Options set on the socket that are independent of the socket definition.
    ///
    /// TODO(https://fxbug.dev/39479): use this to record multicast options.
    #[allow(unused)]
    options: O,
}

/// The definition of an IP socket.
///
/// These values are part of the socket's definition, and never change.
#[derive(Clone, Debug)]
#[cfg_attr(test, derive(PartialEq))]
pub(crate) struct IpSockDefinition<I: IpExt, D> {
    remote_ip: SpecifiedAddr<I::Addr>,
    // Guaranteed to be unicast in its subnet since it's always equal to an
    // address assigned to the local device. We can't use the `UnicastAddr`
    // witness type since `Ipv4Addr` doesn't implement `UnicastAddress`.
    //
    // TODO(joshlf): Support unnumbered interfaces. Once we do that, a few
    // issues arise: A) Does the unicast restriction still apply, and is that
    // even well-defined for IPv4 in the absence of a subnet? B) Presumably we
    // have to always bind to a particular interface?
    local_ip: SpecifiedAddr<I::Addr>,
    device: Option<D>,
    proto: I::Proto,
}

impl<I: IpExt, D, O> IpSock<I, D, O> {
    pub(crate) fn local_ip(&self) -> &SpecifiedAddr<I::Addr> {
        &self.definition.local_ip
    }

    pub(crate) fn remote_ip(&self) -> &SpecifiedAddr<I::Addr> {
        &self.definition.remote_ip
    }

    pub(crate) fn proto(&self) -> I::Proto {
        self.definition.proto
    }

    pub(crate) fn options(&self) -> &O {
        &self.options
    }

    pub(crate) fn options_mut(&mut self) -> &mut O {
        &mut self.options
    }

    /// Swaps in `new_options` for the existing options and returns the old
    /// options.
    pub(crate) fn replace_options(&mut self, new_options: O) -> O {
        core::mem::replace(self.options_mut(), new_options)
    }

    pub(crate) fn take_options(&mut self) -> O
    where
        O: Default,
    {
        self.replace_options(Default::default())
    }

    /// Consumes the socket and returns the contained options.
    pub(crate) fn into_options(self) -> O {
        let Self { definition: _, options } = self;
        options
    }
}

// TODO(joshlf): Once we support configuring transport-layer protocols using
// type parameters, use that to ensure that `proto` is the right protocol for
// the caller. We will still need to have a separate enforcement mechanism for
// raw IP sockets once we support those.

/// The route for a socket.
pub(super) struct IpSockRoute<I: Ip, D> {
    /// The local IP to use for the socket.
    pub(super) local_ip: SpecifiedAddr<I::Addr>,

    /// The destination for packets originating from the socket.
    pub(super) destination: Destination<I::Addr, D>,
}

/// The non-synchronized execution context for IP sockets.
pub(super) trait IpSocketNonSyncContext: InstantContext + CounterContext {}
impl<C: InstantContext + CounterContext> IpSocketNonSyncContext for C {}

/// The context required in order to implement [`IpSocketHandler`].
///
/// Blanket impls of `IpSocketHandler` are provided in terms of
/// `IpSocketContext`.
pub(super) trait IpSocketContext<I, C: IpSocketNonSyncContext>:
    IpDeviceIdContext<I>
where
    I: IpDeviceStateIpExt,
{
    /// Returns a route for a socket.
    ///
    /// If `device` is specified, the available routes are limited to those that
    /// egress over the device.
    fn lookup_route(
        &self,
        ctx: &mut C,
        device: Option<&Self::DeviceId>,
        src_ip: Option<SpecifiedAddr<I::Addr>>,
        dst_ip: SpecifiedAddr<I::Addr>,
    ) -> Result<IpSockRoute<I, Self::DeviceId>, IpSockRouteError>;
}

/// The context required in order to implement [`BufferIpSocketHandler`].
///
/// Blanket impls of `BufferIpSocketHandler` are provided in terms of
/// `BufferIpSocketContext`.
pub(super) trait BufferIpSocketContext<I, C: IpSocketNonSyncContext, B: BufferMut>:
    IpSocketContext<I, C>
where
    I: IpDeviceStateIpExt + packet_formats::ip::IpExt,
{
    /// Send an IP packet to the next-hop node.
    fn send_ip_packet<S: Serializer<Buffer = B>>(
        &mut self,
        ctx: &mut C,
        meta: SendIpPacketMeta<I, &Self::DeviceId, SpecifiedAddr<I::Addr>>,
        body: S,
    ) -> Result<(), S>;
}

impl<I: Ip + IpExt + IpDeviceStateIpExt, C: IpSocketNonSyncContext, SC: IpSocketContext<I, C>>
    IpSocketHandler<I, C> for SC
{
    fn new_ip_socket<O>(
        &mut self,
        ctx: &mut C,
        device: Option<&SC::DeviceId>,
        local_ip: Option<SpecifiedAddr<I::Addr>>,
        remote_ip: SpecifiedAddr<I::Addr>,
        proto: I::Proto,
        options: O,
    ) -> Result<IpSock<I, SC::DeviceId, O>, (IpSockCreationError, O)> {
        // Make sure the remote is routable with a local address before creating
        // the socket. We do not care about the actual destination here because
        // we will recalculate it when we send a packet so that the best route
        // available at the time is used for each outgoing packet.
        let IpSockRoute { local_ip, destination: _ } =
            match self.lookup_route(ctx, device, local_ip, remote_ip) {
                Ok(r) => r,
                Err(e) => return Err((e.into(), options)),
            };

        let definition = IpSockDefinition { local_ip, remote_ip, device: device.cloned(), proto };
        Ok(IpSock { definition: definition, options })
    }
}

/// Provides hooks for altering sending behavior of [`IpSock`].
///
/// Must be implemented by the socket option type of an `IpSock` when using it
/// to call [`BufferIpSocketHandler::send_ip_packet`]. This is implemented as a
/// trait instead of an inherent impl on a type so that users of sockets that
/// don't need certain option types, like TCP for anything multicast-related,
/// can avoid allocating space for those options.
pub trait SendOptions<I: Ip> {
    /// Returns the hop limit to set on a packet going to the given destination.
    ///
    /// If `Some(u)`, `u` will be used as the hop limit (IPv6) or TTL (IPv4) for
    /// a packet going to the given destination. Otherwise the default value
    /// will be used.
    fn hop_limit(&self, destination: &SpecifiedAddr<I::Addr>) -> Option<NonZeroU8>;
}

/// Empty send options that never overrides default values.
#[derive(Copy, Clone, Debug, Default, Eq, PartialEq)]
pub(crate) struct DefaultSendOptions;

impl<I: Ip> SendOptions<I> for DefaultSendOptions {
    fn hop_limit(&self, _destination: &SpecifiedAddr<I::Addr>) -> Option<NonZeroU8> {
        None
    }
}

fn send_ip_packet<
    I: IpExt + IpDeviceStateIpExt + packet_formats::ip::IpExt,
    B: BufferMut,
    S: Serializer<Buffer = B>,
    C: IpSocketNonSyncContext,
    SC: BufferIpSocketContext<I, C, B>
        + BufferIpSocketContext<I, C, Buf<Vec<u8>>>
        + IpSocketContext<I, C>,
    O: SendOptions<I>,
>(
    sync_ctx: &mut SC,
    ctx: &mut C,
    socket: &IpSock<I, SC::DeviceId, O>,
    body: S,
    mtu: Option<u32>,
) -> Result<(), (S, IpSockSendError)> {
    let IpSock { definition: IpSockDefinition { remote_ip, local_ip, device, proto }, options } =
        socket;
    let IpSockRoute { local_ip: got_local_ip, destination: Destination { device, next_hop } } =
        match sync_ctx.lookup_route(ctx, device.as_ref(), Some(*local_ip), *remote_ip) {
            Ok(o) => o,
            Err(IpSockRouteError::NoLocalAddrAvailable) => {
                unreachable!("local IP {} was specified", local_ip)
            }
            Err(IpSockRouteError::Unroutable(e)) => {
                return Err((body, IpSockSendError::Unroutable(e)))
            }
        };

    assert_eq!(*local_ip, got_local_ip);

    BufferIpSocketContext::send_ip_packet(
        sync_ctx,
        ctx,
        SendIpPacketMeta {
            device: &device,
            src_ip: *local_ip,
            dst_ip: *remote_ip,
            next_hop,
            ttl: options.hop_limit(remote_ip),
            proto: *proto,
            mtu,
        },
        body,
    )
    .map_err(|s| (s, IpSockSendError::Mtu))
}

impl<
        B: BufferMut,
        C: IpSocketNonSyncContext,
        SC: BufferIpSocketContext<Ipv4, C, B>
            + BufferIpSocketContext<Ipv4, C, Buf<Vec<u8>>>
            + IpSocketContext<Ipv4, C>,
    > BufferIpSocketHandler<Ipv4, C, B> for SC
{
    fn send_ip_packet<S: Serializer<Buffer = B>, O: SendOptions<Ipv4>>(
        &mut self,
        ctx: &mut C,
        ip_sock: &IpSock<Ipv4, SC::DeviceId, O>,
        body: S,
        mtu: Option<u32>,
    ) -> Result<(), (S, IpSockSendError)> {
        // TODO(joshlf): Call `trace!` with relevant fields from the socket.
        ctx.increment_counter("send_ipv4_packet");

        send_ip_packet(self, ctx, ip_sock, body, mtu)
    }
}

impl<
        B: BufferMut,
        C: IpSocketNonSyncContext,
        SC: BufferIpSocketContext<Ipv6, C, B> + BufferIpSocketContext<Ipv6, C, Buf<Vec<u8>>>,
    > BufferIpSocketHandler<Ipv6, C, B> for SC
{
    fn send_ip_packet<S: Serializer<Buffer = B>, O: SendOptions<Ipv6>>(
        &mut self,
        ctx: &mut C,
        ip_sock: &IpSock<Ipv6, SC::DeviceId, O>,
        body: S,
        mtu: Option<u32>,
    ) -> Result<(), (S, IpSockSendError)> {
        // TODO(joshlf): Call `trace!` with relevant fields from the socket.
        ctx.increment_counter("send_ipv6_packet");

        send_ip_packet(self, ctx, ip_sock, body, mtu)
    }
}

/// IPv6 source address selection as defined in [RFC 6724 Section 5].
pub(super) mod ipv6_source_address_selection {
    use net_types::ip::IpAddress as _;

    use super::*;

    /// Selects the source address for an IPv6 socket using the algorithm
    /// defined in [RFC 6724 Section 5].
    ///
    /// This algorithm is only applicable when the user has not explicitly
    /// specified a source address.
    ///
    /// `remote_ip` is the remote IP address of the socket, `outbound_device` is
    /// the device over which outbound traffic to `remote_ip` is sent (according
    /// to the forwarding table), and `addresses` is an iterator of all
    /// addresses on all devices. The algorithm works by iterating over
    /// `addresses` and selecting the address which is most preferred according
    /// to a set of selection criteria.
    pub(crate) fn select_ipv6_source_address<
        'a,
        D: PartialEq,
        Instant: 'a,
        I: Iterator<Item = (&'a Ipv6AddressEntry<Instant>, D)>,
    >(
        remote_ip: SpecifiedAddr<Ipv6Addr>,
        outbound_device: &D,
        addresses: I,
    ) -> Option<UnicastAddr<Ipv6Addr>> {
        // Source address selection as defined in RFC 6724 Section 5.
        //
        // The algorithm operates by defining a partial ordering on available
        // source addresses, and choosing one of the best address as defined by
        // that ordering (given multiple best addresses, the choice from among
        // those is implementation-defined). The partial order is defined in
        // terms of a sequence of rules. If a given rule defines an order
        // between two addresses, then that is their order. Otherwise, the next
        // rule must be consulted, and so on until all of the rules are
        // exhausted.

        addresses
            // Tentative addresses are not considered available to the source
            // selection algorithm.
            .filter(|(a, _)| !a.state.is_tentative())
            .max_by(|(a, a_device), (b, b_device)| {
                select_ipv6_source_address_cmp(remote_ip, outbound_device, a, a_device, b, b_device)
            })
            .map(|(addr, _device)| addr.addr_sub().addr())
    }

    /// Comparison operator used by `select_ipv6_source_address`.
    fn select_ipv6_source_address_cmp<Instant, D: PartialEq>(
        remote_ip: SpecifiedAddr<Ipv6Addr>,
        outbound_device: &D,
        a: &Ipv6AddressEntry<Instant>,
        a_device: &D,
        b: &Ipv6AddressEntry<Instant>,
        b_device: &D,
    ) -> Ordering {
        // TODO(fxbug.dev/46822): Implement rules 2, 4, 5.5, 6, and 7.

        let a_addr = a.addr_sub().addr().into_specified();
        let b_addr = b.addr_sub().addr().into_specified();

        // Assertions required in order for this implementation to be valid.

        // Required by the implementation of Rule 1.
        debug_assert!(!(a_addr == remote_ip && b_addr == remote_ip));

        // Tentative addresses are not valid source addresses since they are
        // not considered assigned.
        debug_assert!(!a.state.is_tentative());
        debug_assert!(!b.state.is_tentative());

        rule_1(remote_ip, a_addr, b_addr)
            .then_with(|| rule_3(a.deprecated, b.deprecated))
            .then_with(|| rule_5(outbound_device, a_device, b_device))
            .then_with(|| rule_8(remote_ip, a, b))
    }

    // Assumes that `a` and `b` are not both equal to `remote_ip`.
    fn rule_1(
        remote_ip: SpecifiedAddr<Ipv6Addr>,
        a: SpecifiedAddr<Ipv6Addr>,
        b: SpecifiedAddr<Ipv6Addr>,
    ) -> Ordering {
        if (a == remote_ip) != (b == remote_ip) {
            // Rule 1: Prefer same address.
            //
            // Note that both `a` and `b` cannot be equal to `remote_ip` since
            // that would imply that we had added the same address twice to the
            // same device.
            //
            // If `(a == remote_ip) != (b == remote_ip)`, then exactly one of
            // them is equal. If this inequality does not hold, then they must
            // both be unequal to `remote_ip`. In the first case, we have a tie,
            // and in the second case, the rule doesn't apply. In either case,
            // we move onto the next rule.
            if a == remote_ip {
                Ordering::Greater
            } else {
                Ordering::Less
            }
        } else {
            Ordering::Equal
        }
    }

    fn rule_3(a_deprecated: bool, b_deprecated: bool) -> Ordering {
        match (a_deprecated, b_deprecated) {
            (true, false) => Ordering::Less,
            (true, true) | (false, false) => Ordering::Equal,
            (false, true) => Ordering::Greater,
        }
    }

    fn rule_5<D: PartialEq>(outbound_device: &D, a_device: &D, b_device: &D) -> Ordering {
        if (a_device == outbound_device) != (b_device == outbound_device) {
            // Rule 5: Prefer outgoing interface.
            if a_device == outbound_device {
                Ordering::Greater
            } else {
                Ordering::Less
            }
        } else {
            Ordering::Equal
        }
    }

    fn rule_8<Instant>(
        remote_ip: SpecifiedAddr<Ipv6Addr>,
        a: &Ipv6AddressEntry<Instant>,
        b: &Ipv6AddressEntry<Instant>,
    ) -> Ordering {
        // Per RFC 6724 Section 2.2:
        //
        //   We define the common prefix length CommonPrefixLen(S, D) of a
        //   source address S and a destination address D as the length of the
        //   longest prefix (looking at the most significant, or leftmost, bits)
        //   that the two addresses have in common, up to the length of S's
        //   prefix (i.e., the portion of the address not including the
        //   interface ID).  For example, CommonPrefixLen(fe80::1, fe80::2) is
        //   64.
        fn common_prefix_len<Instant>(
            src: &Ipv6AddressEntry<Instant>,
            dst: SpecifiedAddr<Ipv6Addr>,
        ) -> u8 {
            core::cmp::min(
                src.addr_sub().addr().common_prefix_len(&dst),
                src.addr_sub().subnet().prefix(),
            )
        }

        // Rule 8: Use longest matching prefix.
        //
        // Note that, per RFC 6724 Section 5:
        //
        //   Rule 8 MAY be superseded if the implementation has other means of
        //   choosing among source addresses.  For example, if the
        //   implementation somehow knows which source address will result in
        //   the "best" communications performance.
        //
        // We don't currently make use of this option, but it's an option for
        // the future.
        common_prefix_len(a, remote_ip).cmp(&common_prefix_len(b, remote_ip))
    }

    #[cfg(test)]
    mod tests {
        use net_types::ip::AddrSubnet;

        use super::*;
        use crate::ip::device::state::{AddrConfig, AddressState};

        #[test]
        fn test_select_ipv6_source_address() {
            // Test the comparison operator used by `select_ipv6_source_address`
            // by separately testing each comparison condition.

            let remote = SpecifiedAddr::new(Ipv6Addr::from_bytes([
                0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 192, 168, 0, 1,
            ]))
            .unwrap();
            let local0 = SpecifiedAddr::new(Ipv6Addr::from_bytes([
                0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 192, 168, 0, 2,
            ]))
            .unwrap();
            let local1 = SpecifiedAddr::new(Ipv6Addr::from_bytes([
                0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 192, 168, 0, 3,
            ]))
            .unwrap();
            let dev0 = &0;
            let dev1 = &1;
            let dev2 = &2;

            // Rule 1: Prefer same address
            assert_eq!(rule_1(remote, remote, local0), Ordering::Greater);
            assert_eq!(rule_1(remote, local0, remote), Ordering::Less);
            assert_eq!(rule_1(remote, local0, local1), Ordering::Equal);

            // Rule 3: Avoid deprecated states
            assert_eq!(rule_3(false, true), Ordering::Greater);
            assert_eq!(rule_3(true, false), Ordering::Less);
            assert_eq!(rule_3(true, true), Ordering::Equal);
            assert_eq!(rule_3(false, false), Ordering::Equal);

            // Rule 5: Prefer outgoing interface
            assert_eq!(rule_5(dev0, dev0, dev2), Ordering::Greater);
            assert_eq!(rule_5(dev0, dev2, dev0), Ordering::Less);
            assert_eq!(rule_5(dev0, dev0, dev0), Ordering::Equal);
            assert_eq!(rule_5(dev0, dev2, dev2), Ordering::Equal);

            // Rule 8: Use longest matching prefix.
            {
                let new_addr_entry = |bytes, prefix_len| {
                    Ipv6AddressEntry::<()>::new(
                        AddrSubnet::new(Ipv6Addr::from_bytes(bytes), prefix_len).unwrap(),
                        AddressState::Assigned,
                        AddrConfig::Manual,
                    )
                };

                // First, test that the longest prefix match is preferred when
                // using addresses whose common prefix length is shorter than
                // the subnet prefix length.

                // 4 leading 0x01 bytes.
                let remote = SpecifiedAddr::new(Ipv6Addr::from_bytes([
                    1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                ]))
                .unwrap();
                // 3 leading 0x01 bytes.
                let local0 = new_addr_entry([1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0], 64);
                // 2 leading 0x01 bytes.
                let local1 = new_addr_entry([1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0], 64);

                assert_eq!(rule_8(remote, &local0, &local1), Ordering::Greater);
                assert_eq!(rule_8(remote, &local1, &local0), Ordering::Less);
                assert_eq!(rule_8(remote, &local0, &local0), Ordering::Equal);
                assert_eq!(rule_8(remote, &local1, &local1), Ordering::Equal);

                // Second, test that the common prefix length is capped at the
                // subnet prefix length.

                // 3 leading 0x01 bytes, but a subnet prefix length of 8 (1 byte).
                let local0 = new_addr_entry([1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0], 8);
                // 2 leading 0x01 bytes, but a subnet prefix length of 8 (1 byte).
                let local1 = new_addr_entry([1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0], 8);

                assert_eq!(rule_8(remote, &local0, &local1), Ordering::Equal);
                assert_eq!(rule_8(remote, &local1, &local0), Ordering::Equal);
                assert_eq!(rule_8(remote, &local0, &local0), Ordering::Equal);
                assert_eq!(rule_8(remote, &local1, &local1), Ordering::Equal);
            }

            {
                let new_addr_entry = |addr| {
                    Ipv6AddressEntry::<()>::new(
                        AddrSubnet::new(addr, 128).unwrap(),
                        AddressState::Assigned,
                        AddrConfig::Manual,
                    )
                };

                // If no rules apply, then the two address entries are equal.
                assert_eq!(
                    select_ipv6_source_address_cmp(
                        remote,
                        dev0,
                        &new_addr_entry(*local0),
                        dev1,
                        &new_addr_entry(*local1),
                        dev2
                    ),
                    Ordering::Equal
                );
            }
        }
    }
}

/// Test fake implementations of the traits defined in the `socket` module.
#[cfg(test)]
pub(crate) mod testutil {
    use alloc::{collections::HashMap, vec::Vec};
    use core::fmt::Debug;

    use derivative::Derivative;
    use net_types::{
        ip::{AddrSubnet, IpAddress, Ipv4Addr, Subnet},
        Witness,
    };

    use super::*;
    use crate::{
        context::{
            testutil::{FakeCounterCtx, FakeInstant, FakeInstantCtx, FakeNonSyncCtx, FakeSyncCtx},
            FrameContext,
        },
        ip::{
            device::state::{AddrConfig, AddressState, AssignedAddress as _, IpDeviceState},
            forwarding::ForwardingTable,
            testutil::FakeDeviceId,
            HopLimits, IpDeviceId, SendIpPacketMeta, TransportIpContext, DEFAULT_HOP_LIMITS,
        },
    };

    /// A fake implementation of [`IpSocketContext`].
    ///
    /// `IpSocketContext` is implemented for `FakeIpSocketCtx` and any
    /// `FakeCtx<S>` where `S` implements `AsRef` and `AsMut` for
    /// `FakeIpSocketCtx`.
    #[derive(Derivative)]
    #[derivative(Default(bound = ""))]
    pub(crate) struct FakeIpSocketCtx<I: IpDeviceStateIpExt, D> {
        pub(crate) table: ForwardingTable<I, D>,
        device_state: HashMap<D, IpDeviceState<FakeInstant, I>>,
    }

    impl<
            I: IpExt + IpDeviceStateIpExt,
            C: AsMut<FakeCounterCtx> + AsRef<FakeInstantCtx>,
            DeviceId: IpDeviceId + 'static,
        > TransportIpContext<I, C> for FakeIpSocketCtx<I, DeviceId>
    {
        fn get_default_hop_limits(&self, device: Option<&Self::DeviceId>) -> HopLimits {
            device.map_or(DEFAULT_HOP_LIMITS, |device| {
                let hop_limit = self.get_device_state(device).default_hop_limit;
                HopLimits { unicast: hop_limit, multicast: hop_limit }
            })
        }

        fn get_device_with_assigned_addr(
            &self,
            addr: SpecifiedAddr<<I>::Addr>,
        ) -> Option<Self::DeviceId> {
            self.find_device_with_addr(addr)
        }
    }

    impl<I: IpDeviceStateIpExt, DeviceId: IpDeviceId + 'static> IpDeviceIdContext<I>
        for FakeIpSocketCtx<I, DeviceId>
    {
        type DeviceId = DeviceId;

        fn loopback_id(&self) -> Option<Self::DeviceId> {
            None
        }
    }

    impl<
            I: IpDeviceStateIpExt,
            C: AsMut<FakeCounterCtx> + AsRef<FakeInstantCtx>,
            DeviceId: IpDeviceId + 'static,
        > IpSocketContext<I, C> for FakeIpSocketCtx<I, DeviceId>
    {
        fn lookup_route(
            &self,
            _ctx: &mut C,
            device: Option<&Self::DeviceId>,
            local_ip: Option<SpecifiedAddr<I::Addr>>,
            addr: SpecifiedAddr<I::Addr>,
        ) -> Result<IpSockRoute<I, Self::DeviceId>, IpSockRouteError> {
            let FakeIpSocketCtx { device_state, table } = self;
            let destination =
                table.lookup(device, addr).ok_or(IpSockUnroutableError::NoRouteToRemoteAddr)?;

            let Destination { device, next_hop: _ } = &destination;
            local_ip
                .map_or_else(
                    || {
                        device_state
                            .get(&device)
                            .unwrap()
                            .iter_addrs()
                            .map(|e| e.addr())
                            .next()
                            .ok_or(IpSockRouteError::NoLocalAddrAvailable)
                    },
                    |local_ip| {
                        device_state
                            .get(&device)
                            .unwrap()
                            .iter_addrs()
                            .any(|e| e.addr() == local_ip)
                            .then(|| local_ip)
                            .ok_or(IpSockUnroutableError::LocalAddrNotAssigned.into())
                    },
                )
                .map(|local_ip| IpSockRoute { local_ip, destination })
        }
    }

    impl<
            I: IpDeviceStateIpExt,
            S: AsRef<FakeIpSocketCtx<I, DeviceId>> + AsMut<FakeIpSocketCtx<I, DeviceId>>,
            Id,
            Meta,
            Event: Debug,
            DeviceId: IpDeviceId + 'static,
            NonSyncCtxState,
        > IpSocketContext<I, FakeNonSyncCtx<Id, Event, NonSyncCtxState>>
        for FakeSyncCtx<S, Meta, DeviceId>
    {
        fn lookup_route(
            &self,
            ctx: &mut FakeNonSyncCtx<Id, Event, NonSyncCtxState>,
            device: Option<&Self::DeviceId>,
            local_ip: Option<SpecifiedAddr<I::Addr>>,
            addr: SpecifiedAddr<I::Addr>,
        ) -> Result<IpSockRoute<I, Self::DeviceId>, IpSockRouteError> {
            self.get_ref().as_ref().lookup_route(ctx, device, local_ip, addr)
        }
    }

    impl<
            I: IpDeviceStateIpExt + packet_formats::ip::IpExt,
            B: BufferMut,
            S: AsRef<FakeIpSocketCtx<I, DeviceId>> + AsMut<FakeIpSocketCtx<I, DeviceId>>,
            Id,
            Meta,
            Event: Debug,
            DeviceId,
            NonSyncCtxState,
        > BufferIpSocketContext<I, FakeNonSyncCtx<Id, Event, NonSyncCtxState>, B>
        for FakeSyncCtx<S, Meta, DeviceId>
    where
        FakeSyncCtx<S, Meta, DeviceId>: FrameContext<
                FakeNonSyncCtx<Id, Event, NonSyncCtxState>,
                B,
                SendIpPacketMeta<I, Self::DeviceId, SpecifiedAddr<I::Addr>>,
            > + IpSocketContext<I, FakeNonSyncCtx<Id, Event, NonSyncCtxState>>,
    {
        fn send_ip_packet<SS: Serializer<Buffer = B>>(
            &mut self,
            ctx: &mut FakeNonSyncCtx<Id, Event, NonSyncCtxState>,
            SendIpPacketMeta {  device, src_ip, dst_ip, next_hop, proto, ttl, mtu }: SendIpPacketMeta<I, &Self::DeviceId, SpecifiedAddr<I::Addr>>,
            body: SS,
        ) -> Result<(), SS> {
            let meta = SendIpPacketMeta {
                device: device.clone(),
                src_ip,
                dst_ip,
                next_hop,
                proto,
                ttl,
                mtu,
            };
            self.send_frame(ctx, meta, body)
        }
    }

    impl<I: IpDeviceStateIpExt, D: IpDeviceId> FakeIpSocketCtx<I, D> {
        pub(crate) fn with_devices_state(
            devices: impl IntoIterator<
                Item = (D, IpDeviceState<FakeInstant, I>, Vec<SpecifiedAddr<I::Addr>>),
            >,
        ) -> Self {
            let mut table = ForwardingTable::default();
            let mut device_state = HashMap::default();
            for (device, state, addrs) in devices {
                for ip in addrs {
                    assert_eq!(
                        table.add_device_route(
                            Subnet::new(ip.get(), <I::Addr as IpAddress>::BYTES * 8).unwrap(),
                            device.clone(),
                        ),
                        Ok(())
                    );
                }
                assert!(
                    device_state.insert(device.clone(), state).is_none(),
                    "duplicate entries for {}",
                    device
                );
            }

            FakeIpSocketCtx { table, device_state }
        }

        pub(crate) fn find_device_with_addr(&self, addr: SpecifiedAddr<I::Addr>) -> Option<D> {
            let Self { table: _, device_state } = self;
            device_state.iter().find_map(|(device, state)| {
                state.find_addr(&addr).map(|_: &I::AssignedAddress<FakeInstant>| device.clone())
            })
        }

        pub(crate) fn get_device_state(&self, device: &D) -> &IpDeviceState<FakeInstant, I> {
            let Self { device_state, table: _ } = self;
            device_state.get(device).unwrap_or_else(|| panic!("no device {}", device))
        }
    }

    #[derive(Default)]
    pub(crate) struct FakeBufferIpSocketCtx<I: IpDeviceStateIpExt, D> {
        ip_socket_ctx: FakeIpSocketCtx<I, D>,
    }

    impl<I: IpDeviceStateIpExt, D> FakeBufferIpSocketCtx<I, D> {
        pub(crate) fn with_ctx(ip_socket_ctx: FakeIpSocketCtx<I, D>) -> Self {
            Self { ip_socket_ctx }
        }
    }

    impl<I: IpDeviceStateIpExt, D> AsMut<FakeIpSocketCtx<I, D>> for FakeBufferIpSocketCtx<I, D> {
        fn as_mut(&mut self) -> &mut FakeIpSocketCtx<I, D> {
            &mut self.ip_socket_ctx
        }
    }

    impl<I: IpDeviceStateIpExt, D> AsRef<FakeIpSocketCtx<I, D>> for FakeBufferIpSocketCtx<I, D> {
        fn as_ref(&self) -> &FakeIpSocketCtx<I, D> {
            &self.ip_socket_ctx
        }
    }

    impl<
            I: IpExt + IpDeviceStateIpExt,
            C: AsMut<FakeCounterCtx> + AsRef<FakeInstantCtx>,
            D: IpDeviceId + 'static,
            Meta,
        > TransportIpContext<I, C> for FakeSyncCtx<FakeBufferIpSocketCtx<I, D>, Meta, D>
    where
        Self: IpSocketContext<I, C, DeviceId = D>,
    {
        fn get_device_with_assigned_addr(&self, addr: SpecifiedAddr<I::Addr>) -> Option<D> {
            let FakeBufferIpSocketCtx { ip_socket_ctx } = self.get_ref();
            TransportIpContext::<I, C>::get_device_with_assigned_addr(ip_socket_ctx, addr)
        }

        fn get_default_hop_limits(&self, device: Option<&Self::DeviceId>) -> HopLimits {
            let FakeBufferIpSocketCtx { ip_socket_ctx } = self.get_ref();
            TransportIpContext::<I, C>::get_default_hop_limits(ip_socket_ctx, device)
        }
    }

    #[derive(Clone)]
    pub(crate) struct FakeDeviceConfig<D, A: IpAddress> {
        pub(crate) device: D,
        pub(crate) local_ips: Vec<SpecifiedAddr<A>>,
        pub(crate) remote_ips: Vec<SpecifiedAddr<A>>,
    }

    impl<D: IpDeviceId> FakeIpSocketCtx<Ipv4, D> {
        /// Creates a new `FakeIpSocketCtx<Ipv4>` with the given device
        /// configs.
        pub(crate) fn new_ipv4(
            devices: impl IntoIterator<Item = FakeDeviceConfig<D, Ipv4Addr>>,
        ) -> Self {
            FakeIpSocketCtx::with_devices_state(devices.into_iter().map(
                |FakeDeviceConfig { device, local_ips, remote_ips }| {
                    let mut device_state = IpDeviceState::default();
                    for ip in local_ips {
                        // Users of this utility don't care about subnet prefix length,
                        // so just pick a reasonable one.
                        device_state
                            .add_addr(AddrSubnet::new(ip.get(), 32).unwrap())
                            .expect("add address");
                    }
                    (device, device_state, remote_ips)
                },
            ))
        }
    }

    impl FakeIpSocketCtx<Ipv4, FakeDeviceId> {
        /// Creates a new `FakeIpSocketCtx<Ipv4>`.
        pub(crate) fn new_fake_ipv4(
            local_ips: Vec<SpecifiedAddr<Ipv4Addr>>,
            remote_ips: Vec<SpecifiedAddr<Ipv4Addr>>,
        ) -> Self {
            Self::new_ipv4([FakeDeviceConfig { device: FakeDeviceId, local_ips, remote_ips }])
        }
    }

    impl<D: IpDeviceId> FakeIpSocketCtx<Ipv6, D> {
        /// Creates a new `FakeIpSocketCtx<Ipv6>` with the given device
        /// configs.
        pub(crate) fn new_ipv6(
            devices: impl IntoIterator<Item = FakeDeviceConfig<D, Ipv6Addr>>,
        ) -> Self {
            FakeIpSocketCtx::with_devices_state(devices.into_iter().map(
                |FakeDeviceConfig { device, local_ips, remote_ips }| {
                    let mut device_state = IpDeviceState::default();
                    for ip in local_ips {
                        // Users of this utility don't care about subnet prefix length,
                        // so just pick a reasonable one.
                        device_state
                            .add_addr(Ipv6AddressEntry::new(
                                // Users of this utility don't care about subnet prefix
                                // length, so just pick a reasonable one.
                                AddrSubnet::new(ip.get(), 128).unwrap(),
                                AddressState::Assigned,
                                AddrConfig::Manual,
                            ))
                            .expect("add address");
                    }
                    (device, device_state, remote_ips)
                },
            ))
        }
    }

    impl FakeIpSocketCtx<Ipv6, FakeDeviceId> {
        /// Creates a new `FakeIpSocketCtx<Ipv6>`.
        pub(crate) fn new_fake_ipv6(
            local_ips: Vec<SpecifiedAddr<Ipv6Addr>>,
            remote_ips: Vec<SpecifiedAddr<Ipv6Addr>>,
        ) -> Self {
            Self::new_ipv6([FakeDeviceConfig { device: FakeDeviceId, local_ips, remote_ips }])
        }
    }
}

#[cfg(test)]
mod tests {
    use assert_matches::assert_matches;
    use ip_test_macro::ip_test;
    use net_types::{
        ip::{AddrSubnet, IpAddr, IpAddress, Ipv4Addr, SubnetEither},
        Witness,
    };
    use nonzero_ext::nonzero;
    use packet::{Buf, InnerPacketBuilder, ParseBuffer};
    use packet_formats::{
        icmp::{IcmpEchoReply, IcmpIpExt, IcmpMessage, IcmpUnusedCode},
        ip::{IpExt, IpPacket, Ipv4Proto},
        ipv4::{Ipv4OnlyMeta, Ipv4Packet},
        testutil::{parse_ethernet_frame, parse_ip_packet_in_ethernet_frame},
    };
    use test_case::test_case;

    use super::*;
    use crate::{
        context::{self, testutil::FakeInstant, EventContext},
        device::DeviceId,
        ip::{
            device::{
                IpDeviceContext as DeviceIpDeviceContext, IpDeviceEvent, IpDeviceIpExt,
                IpDeviceNonSyncContext,
            },
            IpDeviceContext, IpLayerEvent, IpLayerIpExt, IpStateContext,
        },
        testutil::*,
        Ctx, TimerContext, TimerId,
    };

    enum AddressType {
        LocallyOwned,
        Remote,
        Unspecified {
            // Indicates whether or not it should be possible for the stack to
            // select an address when the client fails to specify one.
            can_select: bool,
        },
        Unroutable,
    }

    enum DeviceType {
        Unspecified,
        OtherDevice,
        LocalDevice,
    }

    struct NewSocketTestCase {
        local_ip_type: AddressType,
        remote_ip_type: AddressType,
        device_type: DeviceType,
        expected_result: Result<(), IpSockCreationError>,
    }

    trait IpSocketIpExt: Ip + TestIpExt + IcmpIpExt + IpExt + crate::ip::IpExt {
        const DISPATCH_RECEIVE_COUNTER: &'static str;
        fn multicast_addr(host: u8) -> SpecifiedAddr<Self::Addr>;
    }

    impl IpSocketIpExt for Ipv4 {
        const DISPATCH_RECEIVE_COUNTER: &'static str = "dispatch_receive_ipv4_packet";
        fn multicast_addr(host: u8) -> SpecifiedAddr<Self::Addr> {
            let [a, b, c, _] = Ipv4::MULTICAST_SUBNET.network().ipv4_bytes();
            SpecifiedAddr::new(Ipv4Addr::new([a, b, c, host])).unwrap()
        }
    }
    impl IpSocketIpExt for Ipv6 {
        const DISPATCH_RECEIVE_COUNTER: &'static str = "dispatch_receive_ipv6_packet";

        fn multicast_addr(host: u8) -> SpecifiedAddr<Self::Addr> {
            let mut bytes = Ipv6::MULTICAST_SUBNET.network().ipv6_bytes();
            bytes[15] = host;
            SpecifiedAddr::new(Ipv6Addr::from_bytes(bytes)).unwrap()
        }
    }

    #[derive(Copy, Clone, Debug, Eq, PartialEq)]
    struct WithHopLimit(Option<NonZeroU8>);

    impl<I: Ip> SendOptions<I> for WithHopLimit {
        fn hop_limit(&self, _destination: &SpecifiedAddr<I::Addr>) -> Option<NonZeroU8> {
            let Self(hop_limit) = self;
            *hop_limit
        }
    }

    fn remove_all_local_addrs<I: Ip + IpLayerIpExt + IpDeviceIpExt>(
        sync_ctx: &mut &FakeSyncCtx,
        ctx: &mut FakeNonSyncCtx,
    ) where
        for<'a> &'a FakeSyncCtx:
            DeviceIpDeviceContext<I, FakeNonSyncCtx, DeviceId = DeviceId<FakeInstant>>,
        FakeNonSyncCtx: IpDeviceNonSyncContext<I, DeviceId<FakeInstant>, Instant = FakeInstant>,
    {
        let devices = DeviceIpDeviceContext::<I, _>::with_devices(sync_ctx, |devices| {
            devices.collect::<Vec<_>>()
        });
        for device in devices {
            let subnets = crate::ip::device::with_assigned_addr_subnets::<I, FakeNonSyncCtx, _, _, _>(
                sync_ctx,
                &device,
                |addrs| addrs.collect::<Vec<_>>(),
            );

            for subnet in subnets {
                crate::device::del_ip_addr(sync_ctx, ctx, &device, &subnet.addr())
                    .expect("failed to remove addr from device");
            }
        }
    }

    #[ip_test]
    #[test_case(NewSocketTestCase {
            local_ip_type: AddressType::Unroutable,
            remote_ip_type: AddressType::Remote,
            device_type: DeviceType::Unspecified,
            expected_result: Err(IpSockRouteError::Unroutable(
                IpSockUnroutableError::LocalAddrNotAssigned,
            )
            .into()),
        }; "unroutable local to remote")]
    #[test_case(NewSocketTestCase {
            local_ip_type: AddressType::LocallyOwned,
            remote_ip_type: AddressType::Unroutable,
            device_type: DeviceType::Unspecified,
            expected_result: Err(IpSockRouteError::Unroutable(
                IpSockUnroutableError::NoRouteToRemoteAddr,
            )
            .into()),
        }; "local to unroutable remote")]
    #[test_case(NewSocketTestCase {
            local_ip_type: AddressType::LocallyOwned,
            remote_ip_type: AddressType::Remote,
            device_type: DeviceType::Unspecified,
            expected_result: Ok(()),
        }; "local to remote")]
    #[test_case(NewSocketTestCase {
            local_ip_type: AddressType::Unspecified { can_select: true },
            remote_ip_type: AddressType::Remote,
            device_type: DeviceType::Unspecified,
            expected_result: Ok(()),
        }; "unspecified to remote")]
    #[test_case(NewSocketTestCase {
            local_ip_type: AddressType::Unspecified { can_select: true },
            remote_ip_type: AddressType::Remote,
            device_type: DeviceType::LocalDevice,
            expected_result: Ok(()),
        }; "unspecified to remote through local device")]
    #[test_case(NewSocketTestCase {
            local_ip_type: AddressType::Unspecified { can_select: true },
            remote_ip_type: AddressType::Remote,
            device_type: DeviceType::OtherDevice,
            expected_result: Err(IpSockRouteError::Unroutable(
                IpSockUnroutableError::NoRouteToRemoteAddr,
            )
            .into()),
        }; "unspecified to remote through other device")]
    #[test_case(NewSocketTestCase {
            local_ip_type: AddressType::Unspecified { can_select: false },
            remote_ip_type: AddressType::Remote,
            device_type: DeviceType::Unspecified,
            expected_result: Err(IpSockRouteError::NoLocalAddrAvailable.into()),
        }; "new unspcified to remote can't select")]
    #[test_case(NewSocketTestCase {
            local_ip_type: AddressType::Remote,
            remote_ip_type: AddressType::Remote,
            device_type: DeviceType::Unspecified,
            expected_result: Err(IpSockRouteError::Unroutable(
                IpSockUnroutableError::LocalAddrNotAssigned,
            )
            .into()),
        }; "new remote to remote")]
    #[test_case(NewSocketTestCase {
            local_ip_type: AddressType::LocallyOwned,
            remote_ip_type: AddressType::LocallyOwned,
            device_type: DeviceType::Unspecified,
            expected_result: Ok(()),
        }; "new local to local")]
    #[test_case(NewSocketTestCase {
            local_ip_type: AddressType::Unspecified { can_select: true },
            remote_ip_type: AddressType::LocallyOwned,
            device_type: DeviceType::Unspecified,
            expected_result: Ok(()),
        }; "new unspecified to local")]
    #[test_case(NewSocketTestCase {
            local_ip_type: AddressType::Remote,
            remote_ip_type: AddressType::LocallyOwned,
            device_type: DeviceType::Unspecified,
            expected_result: Err(IpSockRouteError::Unroutable(
                IpSockUnroutableError::LocalAddrNotAssigned,
            )
            .into()),
        }; "new remote to local")]
    fn test_new<I: Ip + IpSocketIpExt + IpLayerIpExt + IpDeviceIpExt>(test_case: NewSocketTestCase)
    where
        for<'a> &'a FakeSyncCtx: IpSocketHandler<I, FakeNonSyncCtx>
            + IpDeviceIdContext<I, DeviceId = DeviceId<FakeInstant>>
            + DeviceIpDeviceContext<I, FakeNonSyncCtx, DeviceId = DeviceId<FakeInstant>>,
        FakeNonSyncCtx: TimerContext<I::Timer<DeviceId<FakeInstant>>>,
        context::testutil::FakeNonSyncCtx<
            TimerId<FakeInstant>,
            DispatchedEvent,
            FakeNonSyncCtxState,
        >: EventContext<IpDeviceEvent<DeviceId<FakeInstant>, I>>,
    {
        let cfg = I::FAKE_CONFIG;
        let proto = I::ICMP_IP_PROTO;

        let FakeEventDispatcherConfig { local_ip, remote_ip, subnet, local_mac: _, remote_mac: _ } =
            cfg;
        let (Ctx { sync_ctx, mut non_sync_ctx }, device_ids) =
            FakeEventDispatcherBuilder::from_config(cfg).build();
        let mut sync_ctx = &sync_ctx;
        let loopback_device_id =
            crate::device::add_loopback_device(&mut sync_ctx, &mut non_sync_ctx, u16::MAX.into())
                .expect("create the loopback interface");
        crate::device::testutil::enable_device(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &loopback_device_id,
        );

        let NewSocketTestCase { local_ip_type, remote_ip_type, expected_result, device_type } =
            test_case;

        let local_device = match device_type {
            DeviceType::Unspecified => None,
            DeviceType::LocalDevice => Some(device_ids[0].clone()),
            DeviceType::OtherDevice => Some(loopback_device_id.clone()),
        };

        let (expected_from_ip, from_ip) = match local_ip_type {
            AddressType::LocallyOwned => (local_ip, Some(local_ip)),
            AddressType::Remote => (remote_ip, Some(remote_ip)),
            AddressType::Unspecified { can_select } => {
                if !can_select {
                    remove_all_local_addrs::<I>(&mut sync_ctx, &mut non_sync_ctx);
                }
                (local_ip, None)
            }
            AddressType::Unroutable => {
                remove_all_local_addrs::<I>(&mut sync_ctx, &mut non_sync_ctx);
                (local_ip, Some(local_ip))
            }
        };

        let to_ip = match remote_ip_type {
            AddressType::LocallyOwned => local_ip,
            AddressType::Remote => remote_ip,
            AddressType::Unspecified { can_select: _ } => {
                panic!("remote_ip_type cannot be unspecified")
            }
            AddressType::Unroutable => {
                match subnet.into() {
                    SubnetEither::V4(subnet) => {
                        crate::ip::del_route::<Ipv4, _, _>(&mut sync_ctx, &mut non_sync_ctx, subnet)
                            .expect("failed to delete IPv4 device route")
                    }
                    SubnetEither::V6(subnet) => {
                        crate::ip::del_route::<Ipv6, _, _>(&mut sync_ctx, &mut non_sync_ctx, subnet)
                            .expect("failed to delete IPv6 device route")
                    }
                }

                remote_ip
            }
        };

        let get_expected_result = |template| expected_result.map(|()| template);

        let template = IpSock {
            definition: IpSockDefinition {
                remote_ip: to_ip,
                local_ip: expected_from_ip,
                device: local_device.clone(),
                proto,
            },
            options: WithHopLimit(None),
        };

        let res = IpSocketHandler::<I, _>::new_ip_socket(
            &mut sync_ctx,
            &mut non_sync_ctx,
            local_device.as_ref(),
            from_ip,
            to_ip,
            proto,
            WithHopLimit(None),
        )
        .map_err(|(e, WithHopLimit(_))| e);
        assert_eq!(res, get_expected_result(template.clone()));

        // Hop Limit is specified.
        const SPECIFIED_HOP_LIMIT: NonZeroU8 = nonzero!(1u8);
        assert_eq!(
            IpSocketHandler::new_ip_socket(
                &mut sync_ctx,
                &mut non_sync_ctx,
                local_device.as_ref(),
                from_ip,
                to_ip,
                proto,
                WithHopLimit(Some(SPECIFIED_HOP_LIMIT)),
            )
            .map_err(|(e, WithHopLimit(_))| e),
            {
                // The template socket, but with the TTL set to 1.
                let mut template_with_hop_limit = template.clone();
                let IpSock { definition: _, options } = &mut template_with_hop_limit;
                *options = WithHopLimit(Some(SPECIFIED_HOP_LIMIT));
                get_expected_result(template_with_hop_limit)
            }
        );
    }

    #[ip_test]
    #[test_case(AddressType::LocallyOwned, AddressType::LocallyOwned; "local to local")]
    #[test_case(AddressType::Unspecified { can_select: true },
            AddressType::LocallyOwned; "unspecified to local")]
    #[test_case(AddressType::LocallyOwned, AddressType::Remote; "local to remote")]
    fn test_send_local<I: Ip + IpSocketIpExt>(
        from_addr_type: AddressType,
        to_addr_type: AddressType,
    ) where
        for<'a> &'a FakeSyncCtx: BufferIpSocketHandler<I, FakeNonSyncCtx, packet::EmptyBuf>
            + IpDeviceIdContext<I, DeviceId = DeviceId<FakeInstant>>,
        IcmpEchoReply: IcmpMessage<I, &'static [u8], Code = IcmpUnusedCode>,
    {
        set_logger_for_test();

        use packet_formats::icmp::{IcmpEchoRequest, IcmpPacketBuilder};

        let FakeEventDispatcherConfig::<I::Addr> {
            subnet,
            local_ip,
            remote_ip,
            local_mac,
            remote_mac: _,
        } = I::FAKE_CONFIG;

        let mut builder = FakeEventDispatcherBuilder::default();
        let device_idx = builder.add_device(local_mac);
        let (Ctx { sync_ctx, mut non_sync_ctx }, device_ids) = builder.build();
        let device_id = &device_ids[device_idx];
        let mut sync_ctx = &sync_ctx;
        crate::device::add_ip_addr_subnet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &device_id,
            AddrSubnet::new(local_ip.get(), 16).unwrap(),
        )
        .unwrap();
        crate::device::add_ip_addr_subnet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &device_id,
            AddrSubnet::new(remote_ip.get(), 16).unwrap(),
        )
        .unwrap();
        match subnet.into() {
            SubnetEither::V4(subnet) => crate::ip::add_device_route::<Ipv4, _, _>(
                &mut sync_ctx,
                &mut non_sync_ctx,
                subnet,
                device_id.clone(),
            )
            .expect("install IPv4 device route on a fresh stack without routes"),
            SubnetEither::V6(subnet) => crate::ip::add_device_route::<Ipv6, _, _>(
                &mut sync_ctx,
                &mut non_sync_ctx,
                subnet,
                device_id.clone(),
            )
            .expect("install IPv6 device route on a fresh stack without routes"),
        }

        let loopback_device_id =
            crate::device::add_loopback_device(&mut sync_ctx, &mut non_sync_ctx, u16::MAX.into())
                .expect("create the loopback interface");
        crate::device::testutil::enable_device(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &loopback_device_id,
        );

        let (expected_from_ip, from_ip) = match from_addr_type {
            AddressType::LocallyOwned => (local_ip, Some(local_ip)),
            AddressType::Remote => panic!("from_addr_type cannot be remote"),
            AddressType::Unspecified { can_select: _ } => (local_ip, None),
            AddressType::Unroutable => panic!("from_addr_type cannot be unroutable"),
        };

        let to_ip = match to_addr_type {
            AddressType::LocallyOwned => local_ip,
            AddressType::Remote => remote_ip,
            AddressType::Unspecified { can_select: _ } => {
                panic!("to_addr_type cannot be unspecified")
            }
            AddressType::Unroutable => panic!("to_addr_type cannot be unroutable"),
        };

        let sock = IpSocketHandler::<I, _>::new_ip_socket(
            &mut sync_ctx,
            &mut non_sync_ctx,
            None,
            from_ip,
            to_ip,
            I::ICMP_IP_PROTO,
            DefaultSendOptions,
        )
        .unwrap();

        let reply = IcmpEchoRequest::new(0, 0).reply();
        let body = &[1, 2, 3, 4];
        let buffer = Buf::new(body.to_vec(), ..)
            .encapsulate(IcmpPacketBuilder::<I, &[u8], _>::new(
                expected_from_ip.get(),
                to_ip.get(),
                IcmpUnusedCode,
                reply,
            ))
            .serialize_vec_outer()
            .unwrap();

        // Send an echo packet on the socket and validate that the packet is
        // delivered locally.
        BufferIpSocketHandler::<I, _, _>::send_ip_packet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &sock,
            buffer.into_inner().buffer_view().as_ref().into_serializer(),
            None,
        )
        .unwrap();

        handle_queued_rx_packets(&sync_ctx, &mut non_sync_ctx);

        assert_eq!(non_sync_ctx.frames_sent().len(), 0);

        assert_eq!(get_counter_val(&non_sync_ctx, I::DISPATCH_RECEIVE_COUNTER), 1);
    }

    #[ip_test]
    fn test_send<I: Ip + IpSocketIpExt + IpLayerIpExt>()
    where
        for<'a> &'a FakeSyncCtx: BufferIpSocketHandler<I, FakeNonSyncCtx, packet::EmptyBuf>
            + IpDeviceContext<I, FakeNonSyncCtx, DeviceId = DeviceId<FakeInstant>>
            + IpStateContext<I, <FakeNonSyncCtx as InstantContext>::Instant>,
        context::testutil::FakeNonSyncCtx<
            TimerId<FakeInstant>,
            DispatchedEvent,
            FakeNonSyncCtxState,
        >: EventContext<IpLayerEvent<DeviceId<FakeInstant>, I>>,
    {
        // Test various edge cases of the
        // `BufferIpSocketContext::send_ip_packet` method.

        let cfg = I::FAKE_CONFIG;
        let proto = I::ICMP_IP_PROTO;
        let socket_options = WithHopLimit(Some(nonzero!(1u8)));

        let FakeEventDispatcherConfig::<_> { local_mac, remote_mac, local_ip, remote_ip, subnet } =
            cfg;

        let (Ctx { sync_ctx, mut non_sync_ctx }, device_ids) =
            FakeEventDispatcherBuilder::from_config(cfg.clone()).build();
        let mut sync_ctx = &sync_ctx;
        let device_id = &device_ids[0];
        // Create a normal, routable socket.
        let sock = IpSocketHandler::<I, _>::new_ip_socket(
            &mut sync_ctx,
            &mut non_sync_ctx,
            None,
            None,
            remote_ip,
            proto,
            socket_options,
        )
        .unwrap();

        let curr_id = crate::ip::gen_ipv4_packet_id(&mut sync_ctx);

        let check_frame = move |frame: &[u8], packet_count| match [local_ip.get(), remote_ip.get()]
            .into()
        {
            IpAddr::V4([local_ip, remote_ip]) => {
                let (mut body, src_mac, dst_mac, _ethertype) = parse_ethernet_frame(frame).unwrap();
                let packet = (&mut body).parse::<Ipv4Packet<&[u8]>>().unwrap();
                assert_eq!(src_mac, local_mac.get());
                assert_eq!(dst_mac, remote_mac.get());
                assert_eq!(packet.src_ip(), local_ip);
                assert_eq!(packet.dst_ip(), remote_ip);
                assert_eq!(packet.proto(), Ipv4::ICMP_IP_PROTO);
                assert_eq!(packet.ttl(), 1);
                let Ipv4OnlyMeta { id } = packet.version_specific_meta();
                assert_eq!(usize::from(id), usize::from(curr_id) + packet_count);
                assert_eq!(body, [0]);
            }
            IpAddr::V6([local_ip, remote_ip]) => {
                let (body, src_mac, dst_mac, src_ip, dst_ip, ip_proto, ttl) =
                    parse_ip_packet_in_ethernet_frame::<Ipv6>(frame).unwrap();
                assert_eq!(body, [0]);
                assert_eq!(src_mac, local_mac.get());
                assert_eq!(dst_mac, remote_mac.get());
                assert_eq!(src_ip, local_ip);
                assert_eq!(dst_ip, remote_ip);
                assert_eq!(ip_proto, Ipv6::ICMP_IP_PROTO);
                assert_eq!(ttl, 1);
            }
        };
        let mut packet_count = 0;
        assert_eq!(non_sync_ctx.frames_sent().len(), packet_count);

        // Send a packet on the socket and make sure that the right contents
        // are sent.
        BufferIpSocketHandler::<I, _, _>::send_ip_packet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &sock,
            (&[0u8][..]).into_serializer(),
            None,
        )
        .unwrap();
        let mut check_sent_frame = |non_sync_ctx: &crate::testutil::FakeNonSyncCtx| {
            packet_count += 1;
            assert_eq!(non_sync_ctx.frames_sent().len(), packet_count);
            let (dev, frame) = &non_sync_ctx.frames_sent()[packet_count - 1];
            assert_eq!(dev, device_id);
            check_frame(&frame, packet_count);
        };
        check_sent_frame(&non_sync_ctx);

        // Send a packet while imposing an MTU that is large enough to fit the
        // packet.
        let small_body = [0; 1];
        let small_body_serializer = (&small_body).into_serializer();
        let res = BufferIpSocketHandler::<I, _, _>::send_ip_packet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &sock,
            small_body_serializer,
            Some(Ipv6::MINIMUM_LINK_MTU.into()),
        );
        assert_matches::assert_matches!(res, Ok(()));
        check_sent_frame(&non_sync_ctx);

        // Send a packet on the socket while imposing an MTU which will not
        // allow a packet to be sent.
        let res = BufferIpSocketHandler::<I, _, _>::send_ip_packet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &sock,
            small_body_serializer,
            Some(1), // mtu
        );
        assert_matches::assert_matches!(res, Err((_, IpSockSendError::Mtu)));

        assert_eq!(non_sync_ctx.frames_sent().len(), packet_count);
        // Try sending a packet which will be larger than the device's MTU,
        // and make sure it fails.
        let res = BufferIpSocketHandler::<I, _, _>::send_ip_packet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &sock,
            (&[0; crate::ip::Ipv6::MINIMUM_LINK_MTU as usize][..]).into_serializer(),
            None,
        );
        assert_matches::assert_matches!(res, Err((_, IpSockSendError::Mtu)));

        // Make sure that sending on an unroutable socket fails.
        crate::ip::del_route::<I, _, _>(&mut sync_ctx, &mut non_sync_ctx, subnet).unwrap();
        let res = BufferIpSocketHandler::<I, _, _>::send_ip_packet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &sock,
            small_body_serializer,
            None,
        );
        assert_matches::assert_matches!(
            res,
            Err((_, IpSockSendError::Unroutable(IpSockUnroutableError::NoRouteToRemoteAddr)))
        );
    }

    #[ip_test]
    fn test_send_hop_limits<I: Ip + IpSocketIpExt + IpLayerIpExt>()
    where
        for<'a> &'a FakeSyncCtx: BufferIpSocketHandler<I, FakeNonSyncCtx, packet::EmptyBuf>
            + IpDeviceContext<I, FakeNonSyncCtx, DeviceId = DeviceId<FakeInstant>>
            + IpStateContext<I, <FakeNonSyncCtx as InstantContext>::Instant>,
    {
        set_logger_for_test();

        #[derive(Copy, Clone, Debug)]
        struct SetHopLimitFor<A>(SpecifiedAddr<A>);

        const SET_HOP_LIMIT: NonZeroU8 = nonzero!(42u8);

        impl<A: IpAddress> SendOptions<A::Version> for SetHopLimitFor<A> {
            fn hop_limit(&self, destination: &SpecifiedAddr<A>) -> Option<NonZeroU8> {
                let Self(expected_destination) = self;
                (destination == expected_destination).then_some(SET_HOP_LIMIT)
            }
        }

        let FakeEventDispatcherConfig::<I::Addr> {
            local_ip,
            remote_ip: _,
            local_mac,
            subnet: _,
            remote_mac: _,
        } = I::FAKE_CONFIG;

        let mut builder = FakeEventDispatcherBuilder::default();
        let device_idx = builder.add_device(local_mac);
        let (Ctx { sync_ctx, mut non_sync_ctx }, device_ids) = builder.build();
        let device_id = &device_ids[device_idx];
        let mut sync_ctx = &sync_ctx;
        crate::device::add_ip_addr_subnet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &device_id,
            AddrSubnet::new(local_ip.get(), 16).unwrap(),
        )
        .unwrap();

        // Use multicast remote addresses since there are default routes
        // installed for them.
        let remote_ip = I::multicast_addr(0);
        let options = SetHopLimitFor(remote_ip);
        let other_remote_ip = I::multicast_addr(1);

        let mut send_to = |destination_ip| {
            let sock = IpSocketHandler::<I, _>::new_ip_socket(
                &mut sync_ctx,
                &mut non_sync_ctx,
                None,
                None,
                destination_ip,
                I::ICMP_IP_PROTO,
                options,
            )
            .unwrap();

            BufferIpSocketHandler::<I, _, _>::send_ip_packet(
                &mut sync_ctx,
                &mut non_sync_ctx,
                &sock,
                (&[0u8][..]).into_serializer(),
                None,
            )
            .unwrap();
        };

        // Send to two remote addresses: `remote_ip` and `other_remote_ip` and
        // check that the frames were sent with the correct hop limits.
        send_to(remote_ip);
        send_to(other_remote_ip);

        let [df_remote, df_other_remote] =
            assert_matches!(non_sync_ctx.frames_sent(), [df1, df2] => [df1, df2]);
        {
            let (_dev, frame) = df_remote;
            let (_body, _src_mac, _dst_mac, _src_ip, dst_ip, _ip_proto, hop_limit) =
                parse_ip_packet_in_ethernet_frame::<I>(frame).unwrap();
            assert_eq!(dst_ip, remote_ip.get());
            // The `SetHopLimit`-returned value should take precedence.
            assert_eq!(hop_limit, SET_HOP_LIMIT.get());
        }

        {
            let (_dev, frame) = df_other_remote;
            let (_body, _src_mac, _dst_mac, _src_ip, dst_ip, _ip_proto, hop_limit) =
                parse_ip_packet_in_ethernet_frame::<I>(frame).unwrap();
            assert_eq!(dst_ip, other_remote_ip.get());
            // When the options object does not provide a hop limit the default
            // is used.
            assert_eq!(hop_limit, crate::ip::DEFAULT_HOP_LIMITS.unicast.get());
        }
    }

    #[test]
    fn manipulate_options() {
        // The values here don't matter since we won't actually be using this
        // socket to send anything.
        let definition = IpSockDefinition::<Ipv4, ()> {
            remote_ip: Ipv4::LOOPBACK_ADDRESS,
            local_ip: Ipv4::LOOPBACK_ADDRESS,
            device: None,
            proto: Ipv4Proto::Icmp,
        };

        const START_OPTION: usize = 23;
        const DEFAULT_OPTION: usize = 0;
        const NEW_OPTION: usize = 55;
        let mut socket = IpSock { definition, options: START_OPTION };

        assert_eq!(socket.take_options(), START_OPTION);
        assert_eq!(socket.replace_options(NEW_OPTION), DEFAULT_OPTION);
        assert_eq!(socket.options(), &NEW_OPTION);
        assert_eq!(socket.into_options(), NEW_OPTION);
    }
}
