// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The Internet Control Message Protocol (ICMP).

use byteorder::{ByteOrder, NetworkEndian};
use log::{debug, trace};
use net_types::ip::{Ip, IpAddress, Ipv4, Ipv4Addr, Ipv6, Ipv6Addr};
use net_types::MulticastAddress;
use packet::{BufferMut, Serializer, TruncateDirection, TruncatingSerializer};
use specialize_ip_macro::{specialize_ip, specialize_ip_address};

use crate::context::{CounterContext, StateContext};
use crate::device::ndp::NdpPacketHandler;
use crate::device::FrameDestination;
use crate::error;
use crate::ip::mld::MldHandler;
use crate::ip::path_mtu::PmtuHandler;
use crate::ip::{IpDeviceIdContext, IpProto, IPV6_MIN_MTU};
use crate::transport::ConnAddrMap;
use crate::wire::icmp::{
    peek_message_type, IcmpDestUnreachable, IcmpEchoRequest, IcmpIpExt, IcmpMessageType,
    IcmpPacketBuilder, IcmpParseArgs, IcmpTimeExceeded, IcmpUnusedCode, Icmpv4DestUnreachableCode,
    Icmpv4Packet, Icmpv4ParameterProblem, Icmpv4ParameterProblemCode, Icmpv4TimeExceededCode,
    Icmpv6DestUnreachableCode, Icmpv6Packet, Icmpv6PacketTooBig, Icmpv6ParameterProblem,
    Icmpv6ParameterProblemCode, Icmpv6TimeExceededCode, MessageBody,
};
use crate::{BufferDispatcher, Context, EventDispatcher, StackState};

/// A builder for ICMP state.
#[derive(Clone)]
pub struct IcmpStateBuilder {
    icmpv4_send_timestamp_reply: bool,
}

impl Default for IcmpStateBuilder {
    fn default() -> IcmpStateBuilder {
        IcmpStateBuilder { icmpv4_send_timestamp_reply: false }
    }
}

impl IcmpStateBuilder {
    /// Enable or disable replying to ICMPv4 Timestamp Request messages with
    /// Timestamp Reply messages (default: disabled).
    ///
    /// Enabling this can introduce a very minor vulnerability in which an
    /// attacker can learn the system clock's time, which in turn can aid in
    /// attacks against time-based authentication systems.
    pub fn icmpv4_send_timestamp_reply(&mut self, send_timestamp_reply: bool) -> &mut Self {
        self.icmpv4_send_timestamp_reply = send_timestamp_reply;
        self
    }

    pub(crate) fn build(self) -> IcmpState {
        IcmpState {
            v4: Icmpv4State {
                conns: ConnAddrMap::default(),
                send_timestamp_reply: self.icmpv4_send_timestamp_reply,
            },
            v6: Icmpv6State { conns: ConnAddrMap::default() },
        }
    }
}

/// The state associated with the ICMP layer.
pub(crate) struct IcmpState {
    pub(crate) v4: Icmpv4State,
    pub(crate) v6: Icmpv6State,
}

pub(crate) struct Icmpv4State {
    conns: ConnAddrMap<IcmpAddr<Ipv4Addr>>,
    send_timestamp_reply: bool,
}

// Used by `receive_icmp_echo_reply`.
impl AsRef<ConnAddrMap<IcmpAddr<Ipv4Addr>>> for Icmpv4State {
    fn as_ref(&self) -> &ConnAddrMap<IcmpAddr<Ipv4Addr>> {
        &self.conns
    }
}

pub(crate) struct Icmpv6State {
    conns: ConnAddrMap<IcmpAddr<Ipv6Addr>>,
}

// Used by `receive_icmp_echo_reply`.
impl AsRef<ConnAddrMap<IcmpAddr<Ipv6Addr>>> for Icmpv6State {
    fn as_ref(&self) -> &ConnAddrMap<IcmpAddr<Ipv6Addr>> {
        &self.conns
    }
}

#[derive(Copy, Clone, Eq, PartialEq, Hash)]
struct IcmpAddr<A: IpAddress> {
    // TODO(brunodalbo) for now, ICMP connections are only bound to a remote
    //  address and an icmp_id. This will be improved with the addition of
    //  sockets_v2
    remote_addr: A,
    icmp_id: u16,
}

/// The ID identifying an ICMP connection.
///
/// When a new ICMP connection is added, it is given a unique `IcmpConnId`.
/// These are opaque `usize`s which are intentionally allocated as densely as
/// possible around 0, making it possible to store any associated data in a
/// `Vec` indexed by the ID. `IcmpConnId` implements `Into<usize>`.
#[derive(Copy, Clone, Debug, Eq, PartialEq, Hash)]
pub struct IcmpConnId(usize);

impl From<IcmpConnId> for usize {
    fn from(id: IcmpConnId) -> usize {
        id.0
    }
}

/// An event dispatcher for the ICMP layer.
///
/// See the `EventDispatcher` trait in the crate root for more details.
pub trait IcmpEventDispatcher<B: BufferMut> {
    /// Receive an ICMP echo reply.
    fn receive_icmp_echo_reply(&mut self, conn: IcmpConnId, seq_num: u16, data: B) {
        log_unimplemented!((), "IcmpEventDispatcher::receive_icmp_echo_reply: not implemented");
    }
}

/// The execution context for ICMP sockets.
///
/// `IcmpSocketContext` provides support for receiving ICMP echo replies for
/// both ICMP(v4) and ICMPv6 sockets.
pub trait IcmpSocketContext<I: Ip, B: BufferMut> {
    /// Receive an ICMP echo reply.
    ///
    /// If `I` is `Ipv4`, then this is an ICMP(v4) echo reply, and if it's
    /// `Ipv6`, then this is an ICMPv6 echo reply.
    fn receive_icmp_echo_reply(&mut self, conn: IcmpConnId, seq_num: u16, data: B) {
        log_unimplemented!((), "IcmpContext::receive_icmp_echo_reply: not implemented");
    }
}

impl<I: Ip, B: BufferMut, D: BufferDispatcher<B>> IcmpSocketContext<I, B> for Context<D> {
    fn receive_icmp_echo_reply(&mut self, conn: IcmpConnId, seq_num: u16, data: B) {
        self.dispatcher_mut().receive_icmp_echo_reply(conn, seq_num, data);
    }
}

/// The execution context shared by ICMP(v4) and ICMPv6 for the internal
/// operations of the IP stack.
///
/// Unlike [`IcmpSocketContext`], `IcmpContext` is not exposed outside of this
/// crate.
pub(crate) trait IcmpContext<I: IcmpIpExt, B: BufferMut>:
    IpDeviceIdContext + IcmpSocketContext<I, B> + CounterContext
{
    /// Send an ICMP reply to a remote host.
    ///
    /// `send_icmp_reply` sends a reply to a non-error message (e.g., "echo
    /// request" or "timestamp request" messages). It takes the ingress device,
    /// source IP, and destination IP of the packet *being responded to*. It
    /// uses ICMP-specific logic to figure out whether and how to send an ICMP
    /// reply.
    ///
    /// `get_body` returns a `Serializer` with the bytes of the ICMP packet,
    /// and, when called, is given the source IP address chosen for the outbound
    /// packet. This allows `get_body` to properly compute the ICMP checksum,
    /// which relies on both the source and destination IP addresses of the IP
    /// packet it's encapsulated in.
    fn send_icmp_reply<S: Serializer<Buffer = B>, F: FnOnce(I::Addr) -> S>(
        &mut self,
        device: Option<Self::DeviceId>,
        src_ip: I::Addr,
        dst_ip: I::Addr,
        get_body: F,
    ) -> Result<(), S>;

    /// Send an ICMP error message to a remote host.
    ///
    /// `send_icmp_error_message` sends an ICMP error message. It takes the
    /// ingress device, source IP, and destination IP of the packet that
    /// generated the error. It uses ICMP-specific logic to figure out whether
    /// and how to send an ICMP error message. `ip_mtu` is an optional MTU size
    /// for the final IP packet generated by this ICMP response.
    ///
    /// get_body` returns a `Serializer` with the bytes of the ICMP packet, and,
    /// when called, is given the source IP address chosen for the outbound
    /// packet. This allows `get_body` to properly compute the ICMP checksum,
    /// which relies on both the source and destination IP addresses of the IP
    /// packet it's encapsulated in.
    fn send_icmp_error_message<S: Serializer<Buffer = B>, F: FnOnce(I::Addr) -> S>(
        &mut self,
        device: Self::DeviceId,
        src_ip: I::Addr,
        dst_ip: I::Addr,
        get_body: F,
        ip_mtu: Option<u32>,
    ) -> Result<(), S>;
}

/// The execution context for ICMP(v4).
pub(crate) trait Icmpv4Context<B: BufferMut>:
    IcmpContext<Ipv4, B> + StateContext<(), Icmpv4State>
{
}
impl<B: BufferMut, C: IcmpContext<Ipv4, B> + StateContext<(), Icmpv4State>> Icmpv4Context<B> for C {}

/// The execution context for ICMPv6.
pub(crate) trait Icmpv6Context<B: BufferMut>:
    IcmpContext<Ipv6, B> + StateContext<(), Icmpv6State>
{
}
impl<B: BufferMut, C: IcmpContext<Ipv6, B> + StateContext<(), Icmpv6State>> Icmpv6Context<B> for C {}

/// Receive an ICMP(v4) packet.
pub(crate) fn receive_icmpv4_packet<B: BufferMut, C: Icmpv4Context<B> + PmtuHandler<Ipv4>>(
    ctx: &mut C,
    device: Option<C::DeviceId>,
    src_ip: Ipv4Addr,
    dst_ip: Ipv4Addr,
    mut buffer: B,
) {
    trace!("receive_icmpv4_packet({}, {})", src_ip, dst_ip);
    let packet = match buffer.parse_with::<_, Icmpv4Packet<_>>(IcmpParseArgs::new(src_ip, dst_ip)) {
        Ok(packet) => packet,
        Err(err) => return, // TODO(joshlf): Do something else here?
    };

    match packet {
        Icmpv4Packet::EchoRequest(echo_request) => {
            ctx.increment_counter("receive_icmpv4_packet::echo_request");

            let req = *echo_request.message();
            let code = echo_request.code();
            let (local_ip, remote_ip) = (dst_ip, src_ip);
            // TODO(joshlf): Do something if send_ip_packet returns an
            // error?
            ctx.send_icmp_reply(device, remote_ip, local_ip, |src_ip| {
                buffer.encapsulate(IcmpPacketBuilder::<Ipv4, &[u8], _>::new(
                    src_ip,
                    remote_ip,
                    code,
                    req.reply(),
                ))
            });
        }
        Icmpv4Packet::EchoReply(echo_reply) => {
            ctx.increment_counter("receive_icmpv4_packet::echo_reply");
            trace!("receive_icmpv4_packet: Received an EchoReply message");
            let id = echo_reply.message().id();
            let seq = echo_reply.message().seq();
            receive_icmp_echo_reply::<_, _, Icmpv4State, _>(ctx, src_ip, dst_ip, id, seq, buffer);
        }
        Icmpv4Packet::TimestampRequest(timestamp_request) => {
            ctx.increment_counter("receive_icmpv4_packet::timestamp_request");
            if StateContext::<(), Icmpv4State>::get_state(ctx, ()).send_timestamp_reply {
                trace!("receive_icmpv4_packet: Responding to Timestamp Request message");
                // We're supposed to respond with the time that we processed
                // this message as measured in milliseconds since midnight UT.
                // However, that would require that we knew the local time zone
                // and had a way to convert `InstantContext::Instant` to a `u32`
                // value. We can't do that, and probably don't want to introduce
                // all of the machinery necessary just to support this one use
                // case. Luckily, RFC 792 page 17 provides us with an out:
                //
                //   If the time is not available in miliseconds [sic] or cannot
                //   be provided with respect to midnight UT then any time can
                //   be inserted in a timestamp provided the high order bit of
                //   the timestamp is also set to indicate this non-standard
                //   value.
                //
                // Thus, we provide a zero timestamp with the high order bit
                // set.
                const NOW: u32 = 0x80000000;
                let reply = timestamp_request.message().reply(NOW, NOW);
                let (local_ip, remote_ip) = (dst_ip, src_ip);
                // We don't actually want to use any of the _contents_ of the
                // buffer, but we would like to reuse it as scratch space.
                // Eventually, `IcmpPacketBuilder` will implement
                // `InnerPacketBuilder` for messages without bodies, but until
                // that happens, we need to give it an empty buffer.
                buffer.shrink_front_to(0);
                // TODO(joshlf): Do something if send_icmp_reply returns an
                // error?
                ctx.send_icmp_reply(device, remote_ip, local_ip, |src_ip| {
                    buffer.encapsulate(IcmpPacketBuilder::<Ipv4, &[u8], _>::new(
                        src_ip,
                        remote_ip,
                        IcmpUnusedCode,
                        reply,
                    ))
                });
            } else {
                trace!("receive_icmpv4_packet: Silently ignoring Timestamp Request message");
            }
        }
        Icmpv4Packet::TimestampReply(_) => {
            // TODO(joshlf): Support sending Timestamp Requests and receiving
            // Timestamp Replies?
            debug!("receive_icmpv4_packet: Received unsolicited Timestamp Reply message");
        }
        Icmpv4Packet::DestUnreachable(dest_unreachable) => {
            ctx.increment_counter("receive_icmpv4_packet::dest_unreachable");
            trace!("receive_icmpv4_packet: Received a Destination Unreachable message");

            if dest_unreachable.code() == Icmpv4DestUnreachableCode::FragmentationRequired {
                let next_hop_mtu = dest_unreachable.message().next_hop_mtu();

                if let Some(next_hop_mtu) = dest_unreachable.message().next_hop_mtu() {
                    // We are updating the path MTU from the destination address
                    // of this `packet` (which is an IP address on this node) to
                    // some remote (identified by the source address of this
                    // `packet`).
                    //
                    // `update_pmtu_if_less` may return an error, but it will
                    // only happen if the Dest Unreachable message's mtu field
                    // had a value that was less than the IPv4 minimum mtu
                    // (which as per IPv4 RFC 791, must not happen).
                    ctx.update_pmtu_if_less(dst_ip, src_ip, u32::from(next_hop_mtu.get()));
                } else {
                    // If the Next-Hop MTU from an incoming ICMP message is `0`,
                    // then we assume the source node of the ICMP message does
                    // not implement RFC 1191 and therefore does not actually
                    // use the Next-Hop MTU field and still considers it as an
                    // unused field.
                    //
                    // In this case, the only information we have is the size of
                    // the original IP packet that was too big (the original
                    // packet header should be included in the ICMP response).
                    // Here we will simply reduce our PMTU estimate to a value
                    // less than the total length of the original packet. See
                    // RFC 1191 Section 5.
                    //
                    // `update_pmtu_next_lower` may return an error, but it will
                    // only happen if no valid lower value exists from the
                    // original packet's length. It is safe to silently ignore
                    // the error when we have no valid lower PMTU value as the
                    // node from `src_ip` would not be IP RFC compliant and we
                    // expect this to be very rare (for IPv4, the lowest MTU
                    // value for a link can be 68 bytes).
                    let original_packet_buf = dest_unreachable.body().bytes();
                    if original_packet_buf.len() >= 4 {
                        // We need the first 4 bytes as the total length field
                        // is at bytes 2/3 of the original packet buffer.
                        let total_len = NetworkEndian::read_u16(&original_packet_buf[2..4]);

                        trace!("receive_icmpv4_packet: Next-Hop MTU is 0 so using the next best PMTU value from {}", total_len);

                        ctx.update_pmtu_next_lower(dst_ip, src_ip, u32::from(total_len));
                    } else {
                        // Ok to silently ignore as RFC 792 requires nodes to
                        // send the original IP packet header + 64 bytes of the
                        // original IP packet's body so the node itself is
                        // already violating the RFC.
                        trace!("receive_icmpv4_packet: Original packet buf is too small to get original packet len so ignoring");
                    }
                }
            } else {
                log_unimplemented!((), "ip::icmp::receive_icmpv4_packet: Not implemented for this ICMP destination unreachable code {:?}", dest_unreachable.code());
            }
        }
        _ => log_unimplemented!(
            (),
            "ip::icmp::receive_icmpv4_packet: Not implemented for this packet type"
        ),
    }
}

/// Receive an ICMPv6 packet.
pub(crate) fn receive_icmpv6_packet<
    B: BufferMut,
    C: Icmpv6Context<B> + PmtuHandler<Ipv6> + MldHandler + NdpPacketHandler,
>(
    ctx: &mut C,
    device: Option<C::DeviceId>,
    src_ip: Ipv6Addr,
    dst_ip: Ipv6Addr,
    mut buffer: B,
) {
    trace!("receive_icmpv6_packet({}, {})", src_ip, dst_ip);

    let packet = match buffer.parse_with::<_, Icmpv6Packet<_>>(IcmpParseArgs::new(src_ip, dst_ip)) {
        Ok(packet) => packet,
        Err(err) => return, // TODO(joshlf): Do something else here?
    };

    match packet {
        Icmpv6Packet::EchoRequest(echo_request) => {
            ctx.increment_counter("receive_icmpv6_packet::echo_request");

            let req = *echo_request.message();
            let code = echo_request.code();
            let (local_ip, remote_ip) = (dst_ip, src_ip);
            // TODO(joshlf): Do something if send_ip_packet returns an error?
            ctx.send_icmp_reply(device, remote_ip, local_ip, |src_ip| {
                buffer.encapsulate(IcmpPacketBuilder::<Ipv6, &[u8], _>::new(
                    src_ip,
                    remote_ip,
                    code,
                    req.reply(),
                ))
            });
        }
        Icmpv6Packet::EchoReply(echo_reply) => {
            ctx.increment_counter("receive_icmpv6_packet::echo_reply");
            trace!("receive_icmpv6_packet: Received an EchoReply message");
            let id = echo_reply.message().id();
            let seq = echo_reply.message().seq();
            receive_icmp_echo_reply::<_, _, Icmpv6State, _>(ctx, src_ip, dst_ip, id, seq, buffer);
        }
        Icmpv6Packet::RouterSolicitation(_)
        | Icmpv6Packet::RouterAdvertisement(_)
        | Icmpv6Packet::NeighborSolicitation(_)
        | Icmpv6Packet::NeighborAdvertisement(_)
        | Icmpv6Packet::Redirect(_) => {
            ctx.receive_ndp_packet(device, src_ip, dst_ip, packet);
        }
        Icmpv6Packet::PacketTooBig(packet_too_big) => {
            ctx.increment_counter("receive_icmpv6_packet::packet_too_big");
            trace!("receive_icmpv6_packet: Received a Packet Too Big message");
            // We are updating the path MTU from the destination address of this
            // `packet` (which is an IP address on this node) to some remote
            // (identified by the source address of this `packet`).
            //
            // `update_pmtu_if_less` may return an error, but it will only
            // happen if the Packet Too Big message's mtu field had a value that
            // was less than the IPv6 minimum mtu (which as per IPv6 RFC 8200,
            // must not happen).
            ctx.update_pmtu_if_less(dst_ip, src_ip, packet_too_big.message().mtu());
        }
        Icmpv6Packet::MulticastListenerQuery(_)
        | Icmpv6Packet::MulticastListenerReport(_)
        | Icmpv6Packet::MulticastListenerDone(_) => {
            ctx.receive_mld_packet(
                device.expect("MLD messages must come from a device"),
                src_ip,
                dst_ip,
                packet,
            );
        }
        _ => log_unimplemented!(
            (),
            "ip::icmp::receive_icmpv6_packet: Not implemented for this packet type"
        ),
    }
}

/// Send an ICMP(v4) message in response to receiving a packet destined for an
/// unsupported IPv4 protocol.
///
/// `send_icmpv4_protocol_unreachable` sends the appropriate ICMP message in
/// response to receiving an IP packet from `src_ip` to `dst_ip` identifying an
/// unsupported protocol - in particular, a "destination unreachable" message
/// with a "protocol unreachable" code.
///
/// `original_packet` contains the contents of the entire original packet,
/// including the IP header. `header_len` is the length of the header including
/// all options.
pub(crate) fn send_icmpv4_protocol_unreachable<B: BufferMut, C: IcmpContext<Ipv4, B>>(
    ctx: &mut C,
    device: C::DeviceId,
    frame_dst: FrameDestination,
    src_ip: Ipv4Addr,
    dst_ip: Ipv4Addr,
    proto: IpProto,
    original_packet: B,
    header_len: usize,
) {
    ctx.increment_counter("send_icmpv4_protocol_unreachable");

    // Check whether we MUST NOT send an ICMP error message. Unlike other
    // send_icmpv4_xxx functions, we do not check to see whether the inbound
    // packet is an ICMP error message - we already know it's not since its
    // protocol was unsupported.
    if !should_send_icmpv4_error(frame_dst, src_ip, dst_ip) {
        return;
    }

    send_icmpv4_dest_unreachable(
        ctx,
        device,
        src_ip,
        dst_ip,
        Icmpv4DestUnreachableCode::DestProtocolUnreachable,
        original_packet,
        header_len,
    );
}

/// Send an ICMPv6 message in response to receiving a packet destined for an
/// unsupported Next Header.
///
/// `send_icmpv6_protocol_unreachable` is like
/// [`send_icmpv4_protocol_unreachable`], but for ICMPv6. It sends an ICMPv6
/// "parameter problem" message with an "unrecognized next header type" code.
///
/// `header_len` is the length of all IPv6 headers (including extension headers)
/// *before* the payload with the problematic Next Header type.
pub(crate) fn send_icmpv6_protocol_unreachable<B: BufferMut, C: IcmpContext<Ipv6, B>>(
    ctx: &mut C,
    device: C::DeviceId,
    frame_dst: FrameDestination,
    src_ip: Ipv6Addr,
    dst_ip: Ipv6Addr,
    proto: IpProto,
    original_packet: B,
    header_len: usize,
) {
    ctx.increment_counter("send_icmpv6_protocol_unreachable");

    // Check whether we MUST NOT send an ICMP error message. Unlike other
    // send_icmpv6_xxx functions, we do not check to see whether the inbound
    // packet is an ICMP error message - we already know it's not since its
    // protocol was unsupported.
    if !should_send_icmpv6_error(frame_dst, src_ip, dst_ip, false) {
        return;
    }

    send_icmpv6_parameter_problem(
        ctx,
        device,
        src_ip,
        dst_ip,
        Icmpv6ParameterProblemCode::UnrecognizedNextHeaderType,
        // Per RFC 4443, the pointer refers to the first byte of the packet
        // whose Next Header field was unrecognized. It is measured as an offset
        // from the beginning of the first IPv6 header. E.g., a pointer of 40
        // (the length of a single IPv6 header) would indicate that the Next
        // Header field from that header - and hence of the first encapsulated
        // packet - was unrecognized.
        //
        // NOTE: Since header_len is a usize, this could theoretically be a
        // lossy conversion. However, all that means in practice is that, if a
        // remote host somehow managed to get us to process a frame with a 4GB
        // IP header and send an ICMP response, the pointer value would be
        // wrong. It's not worth wasting special logic to avoid generating a
        // malformed packet in a case that will almost certainly never happen.
        Icmpv6ParameterProblem::new(header_len as u32),
        original_packet,
        header_len,
    );
}

/// Send an ICMP(v4) message in response to receiving a packet destined for an
/// unreachable local transport-layer port.
///
/// `send_icmpv4_port_unreachable` sends the appropriate ICMP message in
/// response to receiving an IP packet from `src_ip` to `dst_ip` with an
/// unreachable local transport-layer port. In particular, this is an ICMP
/// "destination unreachable" message with a "port unreachable" code.
///
/// `original_packet` contains the contents of the entire original packet,
/// including the IP packet header. `header_len` is the length of the entire
/// header, including options.
pub(crate) fn send_icmpv4_port_unreachable<B: BufferMut, C: IcmpContext<Ipv4, B>>(
    ctx: &mut C,
    device: C::DeviceId,
    frame_dst: FrameDestination,
    src_ip: Ipv4Addr,
    dst_ip: Ipv4Addr,
    original_packet: B,
    header_len: usize,
) {
    ctx.increment_counter("send_icmpv4_port_unreachable");

    // Check whether we MUST NOT send an ICMP error message. Unlike other
    // send_icmpv4_xxx functions, we do not check to see whether the inbound
    // packet is an ICMP error message - we already know it's not since ICMP is
    // not one of the protocols that can generate "port unreachable" errors.
    if !should_send_icmpv4_error(frame_dst, src_ip, dst_ip) {
        return;
    }

    send_icmpv4_dest_unreachable(
        ctx,
        device,
        src_ip,
        dst_ip,
        Icmpv4DestUnreachableCode::DestPortUnreachable,
        original_packet,
        header_len,
    );
}

/// Send an ICMPv6 message in response to receiving a packet destined for an
/// unreachable local transport-layer port.
///
/// `send_icmpv6_port_unreachable` is like [`send_icmpv4_port_unreachable`], but
/// for ICMPv6.
///
/// `original_packet` contains the contents of the entire original packet,
/// including extension headers.
pub(crate) fn send_icmpv6_port_unreachable<B: BufferMut, C: IcmpContext<Ipv6, B>>(
    ctx: &mut C,
    device: C::DeviceId,
    frame_dst: FrameDestination,
    src_ip: Ipv6Addr,
    dst_ip: Ipv6Addr,
    original_packet: B,
) {
    ctx.increment_counter("send_icmpv6_port_unreachable");

    // Check whether we MUST NOT send an ICMP error message. Unlike other
    // send_icmpv4_xxx functions, we do not check to see whether the inbound
    // packet is an ICMP error message - we already know it's not since ICMP is
    // not one of the protocols that can generate "port unreachable" errors.
    if !should_send_icmpv6_error(frame_dst, src_ip, dst_ip, false) {
        return;
    }

    send_icmpv6_dest_unreachable(
        ctx,
        device,
        src_ip,
        dst_ip,
        Icmpv6DestUnreachableCode::PortUnreachable,
        original_packet,
    );
}

/// Send an ICMP(v4) message in response to receiving a packet destined for an
/// unreachable network.
///
/// `send_icmpv4_net_unreachable` sends the appropriate ICMP message in response
/// to receiving an IP packet from `src_ip` to an unreachable `dst_ip`. In
/// particular, this is an ICMP "destination unreachable" message with a "net
/// unreachable" code.
///
/// `original_packet` contains the contents of the entire original packet -
/// including all IP headers. `header_len` is the length of the IPv4 header. It
/// is ignored for IPv6.
pub(crate) fn send_icmpv4_net_unreachable<B: BufferMut, C: IcmpContext<Ipv4, B>>(
    ctx: &mut C,
    device: C::DeviceId,
    frame_dst: FrameDestination,
    src_ip: Ipv4Addr,
    dst_ip: Ipv4Addr,
    proto: IpProto,
    original_packet: B,
    header_len: usize,
) {
    ctx.increment_counter("send_icmpv4_net_unreachable");

    // Check whether we MUST NOT send an ICMP error message.
    // should_send_icmpv4_error does not handle the "ICMP error message" case,
    // so we check that separately with a call to is_icmp_error_message.
    if !should_send_icmpv4_error(frame_dst, src_ip, dst_ip)
        || is_icmp_error_message::<Ipv4>(proto, &original_packet.as_ref()[header_len..])
    {
        return;
    }

    send_icmpv4_dest_unreachable(
        ctx,
        device,
        src_ip,
        dst_ip,
        Icmpv4DestUnreachableCode::DestNetworkUnreachable,
        original_packet,
        header_len,
    );
}

/// Send an ICMPv6 message in response to receiving a packet destined for an
/// unreachable network.
///
/// `send_icmpv6_net_unreachable` is like [`send_icmpv4_net_unreachable`], but
/// for ICMPv6. It sends an ICMPv6 "destination unreachable" message with a "no
/// route to destination" code.
///
/// `original_packet` contains the contents of the entire original packet
/// including extension headers. `header_len` is the length of the IP header and
/// all extension headers.
pub(crate) fn send_icmpv6_net_unreachable<B: BufferMut, C: IcmpContext<Ipv6, B>>(
    ctx: &mut C,
    device: C::DeviceId,
    frame_dst: FrameDestination,
    src_ip: Ipv6Addr,
    dst_ip: Ipv6Addr,
    proto: IpProto,
    original_packet: B,
    header_len: usize,
) {
    ctx.increment_counter("send_icmpv6_net_unreachable");

    // Check whether we MUST NOT send an ICMPv6 error message.
    // should_send_icmpv6_error does not handle the "ICMP error message" case,
    // so we check that separately with a call to is_icmp_error_message.
    if !should_send_icmpv6_error(frame_dst, src_ip, dst_ip, false)
        || is_icmp_error_message::<Ipv6>(proto, &original_packet.as_ref()[header_len..])
    {
        return;
    }

    send_icmpv6_dest_unreachable(
        ctx,
        device,
        src_ip,
        dst_ip,
        Icmpv6DestUnreachableCode::NoRoute,
        original_packet,
    );
}

/// Send an ICMP(v4) message in response to receiving a packet whose TTL has
/// expired.
///
/// `send_icmpv4_ttl_expired` sends the appropriate ICMP in response to
/// receiving an IP packet from `src_ip` to `dst_ip` whose TTL has expired. In
/// particular, this is an ICMP "time exceeded" message with a "time to live
/// exceeded in transit" code.
///
/// `original_packet` contains the contents of the entire original packet,
/// including the header. `header_len` is the length of the IP header including
/// options.
pub(crate) fn send_icmpv4_ttl_expired<B: BufferMut, C: IcmpContext<Ipv4, B>>(
    ctx: &mut C,
    device: C::DeviceId,
    frame_dst: FrameDestination,
    src_ip: Ipv4Addr,
    dst_ip: Ipv4Addr,
    proto: IpProto,
    mut original_packet: B,
    header_len: usize,
) {
    ctx.increment_counter("send_icmpv4_ttl_expired");

    // Check whether we MUST NOT send an ICMP error message.
    // should_send_icmpv4_error does not handle the "ICMP error message" case,
    // so we check that separately with a call to is_icmp_error_message.
    if !should_send_icmpv4_error(frame_dst, src_ip, dst_ip)
        || is_icmp_error_message::<Ipv4>(proto, &original_packet.as_ref()[header_len..])
    {
        return;
    }

    // Per RFC 792, body contains entire IPv4 header + 64 bytes of original
    // body.
    original_packet.shrink_back_to(header_len + 64);
    // TODO(joshlf): Do something if send_icmp_error_message returns an error?
    ctx.send_icmp_error_message(
        device,
        src_ip,
        dst_ip,
        |local_ip| {
            original_packet.encapsulate(IcmpPacketBuilder::<Ipv4, &[u8], _>::new(
                local_ip,
                src_ip,
                Icmpv4TimeExceededCode::TtlExpired,
                IcmpTimeExceeded::default(),
            ))
        },
        None,
    );
}

/// Send an ICMPv6 message in response to receiving a packet whose hop limit has
/// expired.
///
/// `send_icmpv6_ttl_expired` is like [`send_icmpv4_ttl_expired`], but for
/// ICMPv6. It sends an ICMPv6 "time exceeded" message with a "hop limit
/// exceeded in transit" code.
///
/// `original_packet` contains the contents of the entire original packet
/// including extension headers. `header_len` is the length of the IP header and
/// all extension headers.
pub(crate) fn send_icmpv6_ttl_expired<B: BufferMut, C: IcmpContext<Ipv6, B>>(
    ctx: &mut C,
    device: C::DeviceId,
    frame_dst: FrameDestination,
    src_ip: Ipv6Addr,
    dst_ip: Ipv6Addr,
    proto: IpProto,
    mut original_packet: B,
    header_len: usize,
) {
    ctx.increment_counter("send_icmpv6_ttl_expired");

    // Check whether we MUST NOT send an ICMP error message.
    // should_send_icmpv6_error does not handle the "ICMP error message" case,
    // so we check that separately with a call to is_icmp_error_message.
    if !should_send_icmpv6_error(frame_dst, src_ip, dst_ip, false)
        || is_icmp_error_message::<Ipv6>(proto, &original_packet.as_ref()[header_len..])
    {
        return;
    }

    // TODO(joshlf): Do something if send_icmp_error_message returns an error?
    ctx.send_icmp_error_message(
        device,
        src_ip,
        dst_ip,
        |local_ip| {
            let icmp_builder = IcmpPacketBuilder::<Ipv6, &[u8], _>::new(
                local_ip,
                src_ip,
                Icmpv6TimeExceededCode::HopLimitExceeded,
                IcmpTimeExceeded::default(),
            );

            // Per RFC 4443, body contains as much of the original body as
            // possible without exceeding IPv6 minimum MTU.
            TruncatingSerializer::new(original_packet, TruncateDirection::DiscardBack)
                .encapsulate(icmp_builder)
        },
        Some(IPV6_MIN_MTU),
    );
}

// TODO(joshlf): Test send_icmpv6_packet_too_big once we support dummy IPv6 test
// setups.

/// Send an ICMPv6 message in response to receiving a packet whose size exceeds
/// the MTU of the next hop interface.
///
/// `send_icmpv6_packet_too_big` sends an ICMPv6 "packet too big" message in
/// response to receiving an IP packet from `src_ip` to `dst_ip` whose size
/// exceeds the `mtu` of the next hop interface.
pub(crate) fn send_icmpv6_packet_too_big<B: BufferMut, C: IcmpContext<Ipv6, B>>(
    ctx: &mut C,
    device: C::DeviceId,
    frame_dst: FrameDestination,
    src_ip: Ipv6Addr,
    dst_ip: Ipv6Addr,
    proto: IpProto,
    mtu: u32,
    original_packet: B,
    header_len: usize,
) {
    ctx.increment_counter("send_icmpv6_packet_too_big");

    // Check whether we MUST NOT send an ICMP error message.
    // should_send_icmp_error does not handle the "ICMP error message" case, so
    // we check that separately with a call to is_icmp_error_message.
    //
    // Note, here we explicitly let `should_send_icmpv6_error` allow a multicast
    // destination (link-layer or destination ip) as RFC 4443 Section 2.4.e
    // explicitly allows sending an ICMP response if the original packet was
    // sent to a multicast IP or link layer if the ICMP response message will be
    // a Packet Too Big Message.
    if !should_send_icmpv6_error(frame_dst, src_ip, dst_ip, true)
        || is_icmp_error_message::<Ipv6>(proto, &original_packet.as_ref()[header_len..])
    {
        return;
    }

    ctx.send_icmp_error_message(
        device,
        src_ip,
        dst_ip,
        |local_ip| {
            let icmp_builder = IcmpPacketBuilder::<Ipv6, &[u8], _>::new(
                local_ip,
                src_ip,
                IcmpUnusedCode,
                Icmpv6PacketTooBig::new(mtu),
            );

            // Per RFC 4443, body contains as much of the original body as
            // possible without exceeding IPv6 minimum MTU.
            //
            // The final IP packet must fit within the MTU, so we shrink the
            // original packet to the MTU minus the IPv6 and ICMP header sizes.
            TruncatingSerializer::new(original_packet, TruncateDirection::DiscardBack)
                .encapsulate(icmp_builder)
        },
        Some(IPV6_MIN_MTU),
    );
}

pub(crate) fn send_icmpv4_parameter_problem<B: BufferMut, C: IcmpContext<Ipv4, B>>(
    ctx: &mut C,
    device: C::DeviceId,
    src_ip: Ipv4Addr,
    dst_ip: Ipv4Addr,
    code: Icmpv4ParameterProblemCode,
    parameter_problem: Icmpv4ParameterProblem,
    mut original_packet: B,
    header_len: usize,
) {
    ctx.increment_counter("send_icmpv4_parameter_problem");

    // Per RFC 792, body contains entire IPv4 header + 64 bytes of original
    // body.
    original_packet.shrink_back_to(header_len + 64);
    // TODO(joshlf): Do something if send_icmp_error_message returns an error?
    ctx.send_icmp_error_message(
        device,
        src_ip,
        dst_ip,
        |local_ip| {
            original_packet.encapsulate(IcmpPacketBuilder::<Ipv4, &[u8], _>::new(
                local_ip,
                src_ip,
                code,
                parameter_problem,
            ))
        },
        None,
    );
}

pub(crate) fn send_icmpv6_parameter_problem<B: BufferMut, C: IcmpContext<Ipv6, B>>(
    ctx: &mut C,
    device: C::DeviceId,
    src_ip: Ipv6Addr,
    dst_ip: Ipv6Addr,
    code: Icmpv6ParameterProblemCode,
    parameter_problem: Icmpv6ParameterProblem,
    original_packet: B,
    header_len: usize,
) {
    ctx.increment_counter("send_icmpv6_parameter_problem");

    // TODO(joshlf): Do something if send_icmp_error_message returns an error?
    ctx.send_icmp_error_message(
        device,
        src_ip,
        dst_ip,
        |local_ip| {
            let icmp_builder =
                IcmpPacketBuilder::<Ipv6, &[u8], _>::new(local_ip, src_ip, code, parameter_problem);

            // Per RFC 4443, body contains as much of the original body as
            // possible without exceeding IPv6 minimum MTU.
            TruncatingSerializer::new(original_packet, TruncateDirection::DiscardBack)
                .encapsulate(icmp_builder)
        },
        Some(IPV6_MIN_MTU),
    );
}

fn send_icmpv4_dest_unreachable<B: BufferMut, C: IcmpContext<Ipv4, B>>(
    ctx: &mut C,
    device: C::DeviceId,
    src_ip: Ipv4Addr,
    dst_ip: Ipv4Addr,
    code: Icmpv4DestUnreachableCode,
    mut original_packet: B,
    header_len: usize,
) {
    ctx.increment_counter("send_icmpv4_dest_unreachable");

    // Per RFC 792, body contains entire IPv4 header + 64 bytes of original
    // body.
    original_packet.shrink_back_to(header_len + 64);
    // TODO(joshlf): Do something if send_icmp_error_message returns an error?
    ctx.send_icmp_error_message(
        device,
        src_ip,
        dst_ip,
        |local_ip| {
            original_packet.encapsulate(IcmpPacketBuilder::<Ipv4, &[u8], _>::new(
                local_ip,
                src_ip,
                code,
                IcmpDestUnreachable::default(),
            ))
        },
        None,
    );
}

fn send_icmpv6_dest_unreachable<B: BufferMut, C: IcmpContext<Ipv6, B>>(
    ctx: &mut C,
    device: C::DeviceId,
    src_ip: Ipv6Addr,
    dst_ip: Ipv6Addr,
    code: Icmpv6DestUnreachableCode,
    original_packet: B,
) {
    // TODO(joshlf): Do something if send_icmp_error_message returns an error?
    ctx.send_icmp_error_message(
        device,
        src_ip,
        dst_ip,
        |local_ip| {
            let icmp_builder = IcmpPacketBuilder::<Ipv6, &[u8], _>::new(
                local_ip,
                src_ip,
                code,
                IcmpDestUnreachable::default(),
            );

            // Per RFC 4443, body contains as much of the original body as
            // possible without exceeding IPv6 minimum MTU.
            TruncatingSerializer::new(original_packet, TruncateDirection::DiscardBack)
                .encapsulate(icmp_builder)
        },
        Some(IPV6_MIN_MTU),
    );
}

/// Should we send an ICMP(v4) response?
///
/// `should_send_icmpv4_error` implements the logic described in RFC 1122
/// Section 3.2.2. It decides whether, upon receiving an incoming packet with
/// the given parameters, we should send an ICMP response or not. In particular,
/// we do not send an ICMP response if we've received:
/// - a packet destined to a broadcast or multicast address
/// - a packet sent in a link-layer broadcast
/// - a non-initial fragment
/// - a packet whose source address does not define a single host (a
///   zero/unspecified address, a loopback address, a broadcast address, a
///   multicast address, or a Class E address)
///
/// Note that `should_send_icmpv4_error` does NOT check whether the incoming
/// packet contained an ICMP error message. This is because that check is
/// unnecessary for some ICMP error conditions. The ICMP error message check can
/// be performed separately with `is_icmp_error_message`.
pub(crate) fn should_send_icmpv4_error(
    frame_dst: FrameDestination,
    src_ip: Ipv4Addr,
    dst_ip: Ipv4Addr,
) -> bool {
    // TODO(joshlf): Implement the rest of the rules:
    // - a packet destined to a subnet broadcast address
    // - a non-initial fragment
    // - a packet whose source address is a subnet broadcast address

    // NOTE: The FrameDestination type has variants for unicast, multicast, and
    // broadcast. One implication of the fact that we only check for broadcast
    // here (in compliance with the RFC) is that we could, in one very unlikely
    // edge case, respond with an ICMP error message to an IP packet which was
    // sent in a link-layer multicast frame. In particular, that can happen if
    // we subscribe to a multicast IP group and, as a result, subscribe to the
    // corresponding multicast MAC address, and we receive a unicast IP packet
    // in a multicast link-layer frame destined to that MAC address.
    //
    // TODO(joshlf): Should we filter incoming multicast IP traffic to make sure
    // that it matches the multicast MAC address of the frame it was
    // encapsulated in?
    !(dst_ip.is_multicast()
        || dst_ip.is_global_broadcast()
        || frame_dst.is_broadcast()
        || src_ip.is_unspecified()
        || src_ip.is_loopback()
        || src_ip.is_global_broadcast()
        || src_ip.is_multicast()
        || src_ip.is_class_e())
}

/// Should we send an ICMPv6 response?
///
/// `should_send_icmpv6_error` implements the logic described in RFC 4443
/// Section 2.4.e. It decides whether, upon receiving an incoming packet with
/// the given parameters, we should send an ICMP response or not. In particular,
/// we do not send an ICMP response if we've received:
/// - a packet destined to a multicast address
///   - Two exceptions to this rules:
///     1) the Packet Too Big Message to allow Path MTU discovery to work for
///        IPv6 multicast
///     2) the Parameter Problem Message, Code 2 reporting an unrecognized IPv6
///        option that has the Option Type highest-order two bits set to 10
/// - a packet sent as a link-layer multicast or broadcast
///   - same exceptions apply here as well.
/// - a packet whose source address does not define a single host (a
///   zero/unspecified address, a loopback address, or a multicast address)
///
/// If an ICMP response will be a Packet Too Big Message or a Parameter Problem
/// Message, Code 2 reporting an unrecognized IPv6 option that has the Option
/// Type highest-order two bits set to 10, `allow_dst_multicast` must be set to
/// `true` so this function will allow the exception mentioned above.
///
/// Note that `should_send_icmpv6_error` does NOT check whether the incoming
/// packet contained an ICMP error message. This is because that check is
/// unnecessary for some ICMP error conditions. The ICMP error message check can
/// be performed separately with `is_icmp_error_message`.
pub(crate) fn should_send_icmpv6_error(
    frame_dst: FrameDestination,
    src_ip: Ipv6Addr,
    dst_ip: Ipv6Addr,
    allow_dst_multicast: bool,
) -> bool {
    !((!allow_dst_multicast
        && (dst_ip.is_multicast() || frame_dst.is_multicast() || frame_dst.is_broadcast()))
        || src_ip.is_unspecified()
        || src_ip.is_loopback()
        || src_ip.is_multicast())
}

/// Determine whether or not an IP packet body contains an ICMP error message
/// for the purposes of determining whether or not to send an ICMP response.
///
/// `is_icmp_error_message` checks whether `proto` is ICMP(v4) for IPv4 or
/// ICMPv6 for IPv6 and, if so, attempts to parse `buf` as an ICMP packet in
/// order to determine whether it is an error message or not. If parsing fails,
/// it conservatively assumes that it is an error packet in order to avoid
/// violating the MUST NOT directives of RFC 1122 Section 3.2.2 and [RFC 4443
/// Section 2.4.e].
///
/// [RFC 4443 Section 2.4.e]: https://tools.ietf.org/html/rfc4443#section-2.4
fn is_icmp_error_message<I: IcmpIpExt>(proto: IpProto, buf: &[u8]) -> bool {
    proto == I::IP_PROTO
        && peek_message_type::<I::IcmpMessageType>(buf).map(IcmpMessageType::is_err).unwrap_or(true)
}

/// Common logic for receiving an ICMP echo reply.
fn receive_icmp_echo_reply<
    I: IcmpIpExt,
    B: BufferMut,
    S: AsRef<ConnAddrMap<IcmpAddr<I::Addr>>>,
    C: IcmpContext<I, B> + StateContext<(), S>,
>(
    ctx: &mut C,
    src_ip: I::Addr,
    dst_ip: I::Addr,
    id: u16,
    seq: u16,
    body: B,
) {
    if let Some(conn) =
        ctx.get_state(()).as_ref().get_by_addr(&IcmpAddr { remote_addr: src_ip, icmp_id: id })
    {
        ctx.receive_icmp_echo_reply(IcmpConnId(conn), seq, body);
    } else {
        // NOTE(brunodalbo): Neither the ICMPv4 or ICMPv6 RFCs explicitly state
        // what to do in case we receive an "unsolicited" echo reply. We only
        // expose the replies if we have a registered connection for the
        // IcmpAddr of the incoming reply for now. Given that a reply should
        // only be sent in response to a request, an ICMP unreachable-type
        // message is probably not appropriate for unsolicited replies.
    }
}

/// Send an ICMP echo request on an existing connection.
///
/// # Panics
///
/// `send_icmp_echo_request` panics if `conn` is not associated with a
/// connection for this IP version.
#[specialize_ip]
pub fn send_icmp_echo_request<B: BufferMut, D: BufferDispatcher<B>, I: Ip>(
    ctx: &mut Context<D>,
    conn: IcmpConnId,
    seq_num: u16,
    body: B,
) {
    let conns = get_conns::<_, I::Addr>(ctx.state_mut());
    let IcmpAddr { remote_addr, icmp_id } =
        conns.get_by_conn(conn.0).expect("icmp::send_icmp_echo_request: no such conn").clone();

    let req = IcmpEchoRequest::new(icmp_id, seq_num);

    #[ipv4]
    let proto = IpProto::Icmp;
    #[ipv6]
    let proto = IpProto::Icmpv6;

    // TODO(brunodalbo) for now, ICMP connections are only bound to remote
    //  addresses, which allow us to just send the IP packet with whatever src
    //  ip we resolve the route to. With sockets v2, IcmpAddr will be bound to a
    //  local address and sending this request out will be done differently.
    crate::ip::send_ip_packet(ctx, remote_addr, proto, |a| {
        body.encapsulate(IcmpPacketBuilder::<I, &[u8], _>::new(a, remote_addr, IcmpUnusedCode, req))
    });
}

/// Creates a new ICMP connection.
///
/// Creates a new ICMP connection with the provided parameters `local_addr`,
/// `remote_addr` and `icmp_id`, and returns its newly-allocated ID.
///
/// If a connection with the conflicting parameters already exists, the call
/// fails and returns an [`error::NetstackError`].
pub fn new_icmp_connection<D: EventDispatcher, A: IpAddress>(
    ctx: &mut Context<D>,
    local_addr: A,
    remote_addr: A,
    icmp_id: u16,
) -> Result<IcmpConnId, error::NetstackError> {
    let conns = get_conns::<_, A>(ctx.state_mut());
    // TODO(brunodalbo) icmp connections are currently only bound to the remote
    //  address. Sockets api v2 will improve this.
    let addr = IcmpAddr { remote_addr, icmp_id };
    if conns.get_by_addr(&addr).is_some() {
        return Err(error::NetstackError::Exists);
    }
    Ok(IcmpConnId(conns.insert(addr)))
}

#[specialize_ip_address]
fn get_conns<D: EventDispatcher, A: IpAddress>(
    state: &mut StackState<D>,
) -> &mut ConnAddrMap<IcmpAddr<A>> {
    #[ipv4addr]
    return &mut state.ip.icmp.v4.conns;
    #[ipv6addr]
    return &mut state.ip.icmp.v6.conns;
}

#[cfg(test)]
mod tests {
    use net_types::ip::{Ip, Ipv4, Ipv4Addr, Ipv6, Ipv6Addr};
    use packet::{Buf, Serializer};
    use std::fmt::Debug;
    #[cfg(feature = "udp-icmp-port-unreachable")]
    use std::num::NonZeroU16;

    use super::*;
    use crate::device::{set_forwarding_enabled, DeviceId, FrameDestination};
    use crate::ip::{receive_ip_packet, IpExt};
    use crate::testutil::{
        DummyEventDispatcher, DummyEventDispatcherBuilder, DUMMY_CONFIG_V4, DUMMY_CONFIG_V6,
    };
    use crate::wire::icmp::{
        IcmpEchoRequest, IcmpMessage, IcmpPacket, IcmpUnusedCode, Icmpv4TimestampRequest,
        MessageBody,
    };
    #[cfg(feature = "udp-icmp-port-unreachable")]
    use crate::wire::udp::UdpPacketBuilder;

    /// Test that receiving a particular IP packet results in a particular ICMP
    /// response.
    ///
    /// Test that receiving an IP packet from remote host
    /// `DUMMY_CONFIG.remote_ip` to host `dst_ip` with `ttl` and `proto` results
    /// in all of the counters in `assert_counters` being triggered at least
    /// once, and exactly one ICMP packet being sent in response with
    /// `expect_message` and `expect_code`.
    ///
    /// The state is initialized to `testutil::DUMMY_CONFIG` when testing.
    #[allow(clippy::too_many_arguments)]
    fn test_receive_ip_packet<
        C: PartialEq + Debug,
        M: for<'a> IcmpMessage<Ipv4, &'a [u8], Code = C> + PartialEq + Debug,
        F: for<'a> Fn(&IcmpPacket<Ipv4, &'a [u8], M>),
    >(
        body: &mut [u8],
        dst_ip: Ipv4Addr,
        ttl: u8,
        proto: IpProto,
        assert_counters: &[&str],
        expect_message: M,
        expect_code: C,
        f: F,
    ) {
        crate::testutil::set_logger_for_test();
        let buffer = Buf::new(body, ..)
            .encapsulate(<Ipv4 as IpExt>::PacketBuilder::new(
                DUMMY_CONFIG_V4.remote_ip,
                dst_ip,
                ttl,
                proto,
            ))
            .serialize_vec_outer()
            .unwrap();

        let mut ctx = DummyEventDispatcherBuilder::from_config(DUMMY_CONFIG_V4)
            .build::<DummyEventDispatcher>();

        // currently only used by test_receive_timestamp
        ctx.state_mut().ip.icmp.v4.send_timestamp_reply = true;
        let device = DeviceId::new_ethernet(0);
        // currently only used by test_ttl_exceeded
        ctx.state_mut().ip.v4.forward = true;
        set_forwarding_enabled::<_, Ipv4>(&mut ctx, device, true);
        receive_ip_packet::<_, _, Ipv4>(&mut ctx, device, FrameDestination::Unicast, buffer);

        for counter in assert_counters {
            assert!(*ctx.state().test_counters.get(counter) > 0, "counter at zero: {}", counter);
        }

        assert_eq!(ctx.dispatcher().frames_sent().len(), 1);
        let (src_mac, dst_mac, src_ip, dst_ip, message, code) =
            crate::testutil::parse_icmp_packet_in_ip_packet_in_ethernet_frame::<Ipv4, _, M, _>(
                &ctx.dispatcher().frames_sent()[0].1,
                f,
            )
            .unwrap();

        assert_eq!(src_mac, DUMMY_CONFIG_V4.local_mac);
        assert_eq!(dst_mac, DUMMY_CONFIG_V4.remote_mac);
        assert_eq!(src_ip, DUMMY_CONFIG_V4.local_ip);
        assert_eq!(dst_ip, DUMMY_CONFIG_V4.remote_ip);
        assert_eq!(message, expect_message);
        assert_eq!(code, expect_code);
    }

    #[test]
    fn test_receive_echo() {
        crate::testutil::set_logger_for_test();

        let req = IcmpEchoRequest::new(0, 0);
        let req_body = &[1, 2, 3, 4];
        let mut buffer = Buf::new(req_body.to_vec(), ..)
            .encapsulate(IcmpPacketBuilder::<Ipv4, &[u8], _>::new(
                DUMMY_CONFIG_V4.remote_ip,
                DUMMY_CONFIG_V4.local_ip,
                IcmpUnusedCode,
                req,
            ))
            .serialize_vec_outer()
            .unwrap();
        test_receive_ip_packet(
            buffer.as_mut(),
            DUMMY_CONFIG_V4.local_ip,
            64,
            IpProto::Icmp,
            &["receive_icmpv4_packet::echo_request", "send_ip_packet"],
            req.reply(),
            IcmpUnusedCode,
            |packet| assert_eq!(packet.original_packet().bytes(), req_body),
        );
    }

    #[test]
    fn test_receive_timestamp() {
        crate::testutil::set_logger_for_test();

        let req = Icmpv4TimestampRequest::new(1, 2, 3);
        let mut buffer = Buf::new(vec![], ..)
            .encapsulate(IcmpPacketBuilder::<Ipv4, &[u8], _>::new(
                DUMMY_CONFIG_V4.remote_ip,
                DUMMY_CONFIG_V4.local_ip,
                IcmpUnusedCode,
                req,
            ))
            .serialize_vec_outer()
            .unwrap();
        test_receive_ip_packet(
            buffer.as_mut(),
            DUMMY_CONFIG_V4.local_ip,
            64,
            IpProto::Icmp,
            &["receive_icmpv4_packet::timestamp_request", "send_ip_packet"],
            req.reply(0x80000000, 0x80000000),
            IcmpUnusedCode,
            |_| {},
        );
    }

    #[test]
    fn test_protocol_unreachable() {
        // Receive an IP packet for an unreachable protocol (255). Check to make
        // sure that we respond with the appropriate ICMP message.
        //
        // TODO(joshlf): Also perform the check for IPv6 once we support dummy
        // IPv6 networks.
        test_receive_ip_packet(
            &mut [0u8; 128],
            DUMMY_CONFIG_V4.local_ip,
            64,
            IpProto::Other(255),
            &["send_icmpv4_protocol_unreachable", "send_icmp_error_message"],
            IcmpDestUnreachable::default(),
            Icmpv4DestUnreachableCode::DestProtocolUnreachable,
            // ensure packet is truncated to the right length
            |packet| assert_eq!(packet.original_packet().bytes().len(), 84),
        );
    }

    #[test]
    #[cfg(feature = "udp-icmp-port-unreachable")]
    fn test_port_unreachable() {
        // TODO(joshlf): Use TCP in addition to UDP since UDP only works with
        // the udp-icmp-port-unreachable feature enabled.

        // Receive an IP packet for an unreachable UDP port (1234). Check to
        // make sure that we respond with the appropriate ICMP message.
        //
        // TODO(joshlf):
        // - Also perform the check for IPv6 once we support dummy IPv6
        //   networks.
        // - Perform the same check for TCP once the logic is implemented
        let mut buf = [0u8; 128];
        let mut buffer = Buf::new(&mut buf[..], ..)
            .encapsulate(UdpPacketBuilder::new(
                DUMMY_CONFIG_V4.remote_ip,
                DUMMY_CONFIG_V4.local_ip,
                None,
                NonZeroU16::new(1234).unwrap(),
            ))
            .serialize_vec_outer();
        test_receive_ip_packet(
            buffer.as_mut(),
            DUMMY_CONFIG_V4.local_ip,
            64,
            IpProto::Udp,
            &["send_icmpv4_port_unreachable", "send_icmp_error_message"],
            IcmpDestUnreachable::default(),
            Icmpv4DestUnreachableCode::DestPortUnreachable,
            // ensure packet is truncated to the right length
            |packet| assert_eq!(packet.original_packet().bytes().len(), 84),
        );
    }

    #[test]
    fn test_net_unreachable() {
        // Receive an IP packet for an unreachable destination address. Check to
        // make sure that we respond with the appropriate ICMP message.
        //
        // TODO(joshlf): Also perform the check for IPv6 once we support dummy
        // IPv6 networks.
        test_receive_ip_packet(
            &mut [0u8; 128],
            Ipv4Addr::new([1, 2, 3, 4]),
            64,
            IpProto::Udp,
            &["send_icmpv4_net_unreachable", "send_icmp_error_message"],
            IcmpDestUnreachable::default(),
            Icmpv4DestUnreachableCode::DestNetworkUnreachable,
            // ensure packet is truncated to the right length
            |packet| assert_eq!(packet.original_packet().bytes().len(), 84),
        );
    }

    #[test]
    fn test_ttl_expired() {
        // Receive an IP packet with an expired TTL. Check to make sure that we
        // respond with the appropriate ICMP message.
        //
        // TODO(joshlf): Also perform the check for IPv6 once we support dummy
        // IPv6 networks.
        test_receive_ip_packet(
            &mut [0u8; 128],
            DUMMY_CONFIG_V4.remote_ip,
            1,
            IpProto::Udp,
            &["send_icmpv4_ttl_expired", "send_icmp_error_message"],
            IcmpTimeExceeded::default(),
            Icmpv4TimeExceededCode::TtlExpired,
            // ensure packet is truncated to the right length
            |packet| assert_eq!(packet.original_packet().bytes().len(), 84),
        );
    }

    #[test]
    fn test_should_send_icmpv4_error() {
        let src_ip = DUMMY_CONFIG_V4.local_ip;
        let dst_ip = DUMMY_CONFIG_V4.remote_ip;
        let frame_dst = FrameDestination::Unicast;
        let multicast_ip_1 = Ipv4Addr::new([224, 0, 0, 1]);
        let multicast_ip_2 = Ipv4Addr::new([224, 0, 0, 2]);

        // Should Send.
        assert!(should_send_icmpv4_error(frame_dst, src_ip, dst_ip));

        // Should not send because destined for IP broadcast addr
        assert!(!should_send_icmpv4_error(frame_dst, src_ip, Ipv4Addr::new([255, 255, 255, 255])));

        // Should not send because destined for multicast addr
        assert!(!should_send_icmpv4_error(frame_dst, src_ip, multicast_ip_1));

        // Should not send because Link Layer Broadcast.
        assert!(!should_send_icmpv4_error(FrameDestination::Broadcast, src_ip, dst_ip));

        // Should not send because from unspecified addr
        assert!(!should_send_icmpv4_error(frame_dst, Ipv4::UNSPECIFIED_ADDRESS, dst_ip));

        // Should not send because from loopback addr
        assert!(!should_send_icmpv4_error(frame_dst, Ipv4::LOOPBACK_ADDRESS, dst_ip));

        // Should not send because from global broadcast addr
        assert!(!should_send_icmpv4_error(frame_dst, Ipv4::GLOBAL_BROADCAST_ADDRESS, dst_ip));

        // Should not send because from multicast addr
        assert!(!should_send_icmpv4_error(frame_dst, multicast_ip_2, dst_ip));

        // Should not send because from class e addr
        assert!(!should_send_icmpv4_error(frame_dst, Ipv4Addr::new([240, 0, 0, 1]), dst_ip));
    }

    #[test]
    fn test_should_send_icmpv6_error() {
        let src_ip = DUMMY_CONFIG_V6.local_ip;
        let dst_ip = DUMMY_CONFIG_V6.remote_ip;
        let frame_dst = FrameDestination::Unicast;
        let multicast_ip_1 = Ipv6Addr::new([255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1]);
        let multicast_ip_2 = Ipv6Addr::new([255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2]);

        // Should Send.
        assert!(should_send_icmpv6_error(frame_dst, src_ip, dst_ip, false));
        assert!(should_send_icmpv6_error(frame_dst, src_ip, dst_ip, true));

        // Should not send because destined for multicast addr,
        // unless exception applies
        assert!(!should_send_icmpv6_error(frame_dst, src_ip, multicast_ip_1, false));
        assert!(should_send_icmpv6_error(frame_dst, src_ip, multicast_ip_1, true));

        // Should not send because Link Layer Broadcast.,
        // unless exception applies
        assert!(!should_send_icmpv6_error(FrameDestination::Broadcast, src_ip, dst_ip, false));
        assert!(should_send_icmpv6_error(FrameDestination::Broadcast, src_ip, dst_ip, true));

        // Should not send because from unspecified addr
        assert!(!should_send_icmpv6_error(frame_dst, Ipv6::UNSPECIFIED_ADDRESS, dst_ip, false));
        assert!(!should_send_icmpv6_error(frame_dst, Ipv6::UNSPECIFIED_ADDRESS, dst_ip, true));

        // Should not send because from loopback addr
        assert!(!should_send_icmpv6_error(frame_dst, Ipv6::LOOPBACK_ADDRESS, dst_ip, false));
        assert!(!should_send_icmpv6_error(frame_dst, Ipv6::LOOPBACK_ADDRESS, dst_ip, true));

        // Should not send because from multicast addr
        assert!(!should_send_icmpv6_error(frame_dst, multicast_ip_2, dst_ip, false));
        assert!(!should_send_icmpv6_error(frame_dst, multicast_ip_2, dst_ip, true));

        // Should not send becuase from multicast addr,
        // even though dest multicast exception applies
        assert!(!should_send_icmpv6_error(
            FrameDestination::Broadcast,
            multicast_ip_2,
            dst_ip,
            false
        ));
        assert!(!should_send_icmpv6_error(
            FrameDestination::Broadcast,
            multicast_ip_2,
            dst_ip,
            true
        ));
        assert!(!should_send_icmpv6_error(frame_dst, multicast_ip_2, multicast_ip_1, false));
        assert!(!should_send_icmpv6_error(frame_dst, multicast_ip_2, multicast_ip_1, true));
    }

    fn test_icmp_connections<I: Ip>(recv_icmp_packet_name: &str) {
        crate::testutil::set_logger_for_test();
        let config = crate::testutil::get_dummy_config::<I::Addr>();
        let mut net =
            crate::testutil::new_dummy_network_from_config("alice", "bob", config.clone());

        let icmp_id = 13;

        let conn =
            new_icmp_connection(net.context("alice"), config.local_ip, config.remote_ip, icmp_id)
                .unwrap();

        let echo_body = vec![1, 2, 3, 4];

        send_icmp_echo_request::<_, _, I>(
            net.context("alice"),
            conn,
            7,
            Buf::new(echo_body.clone(), ..),
        );

        net.run_until_idle().unwrap();
        assert_eq!(
            *net.context("bob")
                .state()
                .test_counters
                .get(&format!("{}::echo_request", recv_icmp_packet_name)),
            1
        );
        assert_eq!(
            *net.context("alice")
                .state()
                .test_counters
                .get(&format!("{}::echo_reply", recv_icmp_packet_name)),
            1
        );
        let replies = net.context("alice").dispatcher_mut().take_icmp_replies(conn);
        assert!(!replies.is_empty());
        let (seq, body) = &replies[0];
        assert_eq!(*seq, 7);
        assert_eq!(*body, echo_body);
    }

    #[test]
    fn test_icmp_connections_v4() {
        test_icmp_connections::<Ipv4>("receive_icmpv4_packet");
    }

    #[test]
    fn test_icmp_connections_v6() {
        test_icmp_connections::<Ipv6>("receive_icmpv6_packet");
    }
}
