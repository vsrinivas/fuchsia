// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(missing_docs)]

//! Ping library.

#[cfg(target_os = "fuchsia")]
mod fuchsia;

#[cfg(target_os = "fuchsia")]
pub use fuchsia::{new_icmp_socket, IpExt};

use futures::{ready, Sink, SinkExt as _, Stream, TryStreamExt as _};
use std::{
    marker::PhantomData,
    pin::Pin,
    task::{Context, Poll},
};
use thiserror::Error;
use zerocopy::{byteorder::network_endian::U16, AsBytes, FromBytes, Unaligned};

/// The number of bytes of an ICMP (v4 or v6) header.
pub const ICMP_HEADER_LEN: usize = std::mem::size_of::<IcmpHeader>();

/// ICMP header representation.
#[repr(C)]
#[derive(FromBytes, AsBytes, Unaligned, Debug, PartialEq, Eq, Clone)]
struct IcmpHeader {
    r#type: u8,
    code: u8,
    checksum: U16,
    id: U16,
    sequence: U16,
}

impl IcmpHeader {
    fn new<I: Ip>(sequence: u16) -> Self {
        Self {
            r#type: I::ECHO_REQUEST_TYPE,
            code: 0,
            checksum: 0.into(),
            id: 0.into(),
            sequence: sequence.into(),
        }
    }
}

/// Ping error.
#[derive(Debug, Error)]
pub enum PingError {
    /// Send error.
    #[error("send error")]
    Send(#[source] std::io::Error),
    /// Send length mismatch.
    #[error("wrong number of bytes sent, got: {got}, want: {want}")]
    SendLength {
        /// Number of bytes sent.
        got: usize,
        /// Number of bytes expected to be sent.
        want: usize,
    },
    /// Recv error.
    #[error("recv error")]
    Recv(#[source] std::io::Error),
    /// ICMP header parsing error.
    #[error("failed to parse ICMP header")]
    Parse,
    /// Reply type mismatch.
    #[error("wrong reply type, got: {got}, want: {want}")]
    ReplyType {
        /// ICMP type received in reply.
        got: u8,
        /// ICMP type expected in reply.
        want: u8,
    },
    /// Reply code mismatch.
    #[error("non-zero reply code: {0}")]
    ReplyCode(u8),
    /// ICMP message body mismatch.
    #[error("reply message body mismatch, got: {got:?}, want: {want:?}")]
    Body {
        /// Body received in reply.
        got: Vec<u8>,
        /// Body expected in reply.
        want: Vec<u8>,
    },
}

/// Addresses which can be converted from `socket2::SockAddr`.
///
/// This trait exists to get around not being able to implement the foreign trait
/// `TryFrom<socket2::SockAddr>` for the foreign types `std::net::SocketAddr(V4|V6)?`.
pub trait TryFromSockAddr: Sized {
    /// Try to convert from `socket2::SockAddr`.
    fn try_from(value: socket2::SockAddr) -> std::io::Result<Self>;
}

impl TryFromSockAddr for std::net::SocketAddrV4 {
    fn try_from(addr: socket2::SockAddr) -> std::io::Result<Self> {
        addr.as_socket_ipv4().ok_or_else(|| {
            std::io::Error::new(
                std::io::ErrorKind::Other,
                format!("socket address is not v4 {:?}", addr),
            )
        })
    }
}

impl TryFromSockAddr for std::net::SocketAddrV6 {
    fn try_from(addr: socket2::SockAddr) -> std::io::Result<Self> {
        addr.as_socket_ipv6().ok_or_else(|| {
            std::io::Error::new(
                std::io::ErrorKind::Other,
                format!("socket address is not v6 {:?}", addr),
            )
        })
    }
}

/// Trait for IP protocol versions.
pub trait Ip:
    Sized
    + Clone
    + Copy
    + std::fmt::Debug
    + Eq
    + std::hash::Hash
    + Ord
    + PartialEq
    + PartialOrd
    + Send
    + Sync
    + Unpin
    + 'static
{
    /// IP Socket address type.
    type Addr: Into<socket2::SockAddr>
        + TryFromSockAddr
        + Clone
        + Unpin
        + PartialEq
        + std::fmt::Debug
        + std::fmt::Display
        + Eq
        + std::hash::Hash;

    /// ICMP socket domain.
    const DOMAIN: socket2::Domain;
    /// ICMP socket protocol.
    const PROTOCOL: socket2::Protocol;

    /// ICMP echo request type.
    const ECHO_REQUEST_TYPE: u8;
    /// ICMP echo reply type.
    const ECHO_REPLY_TYPE: u8;
}

/// Uninstantiable type representing IPv4.
#[derive(Clone, Copy, Debug, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub enum Ipv4 {}

impl Ip for Ipv4 {
    type Addr = std::net::SocketAddrV4;

    const DOMAIN: socket2::Domain = socket2::Domain::IPV4;
    const PROTOCOL: socket2::Protocol = socket2::Protocol::ICMPV4;

    const ECHO_REQUEST_TYPE: u8 = 8;
    const ECHO_REPLY_TYPE: u8 = 0;
}

/// Uninstantiable type representing IPv6.
#[derive(Clone, Copy, Debug, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub enum Ipv6 {}

impl Ip for Ipv6 {
    type Addr = std::net::SocketAddrV6;

    const DOMAIN: socket2::Domain = socket2::Domain::IPV6;
    const PROTOCOL: socket2::Protocol = socket2::Protocol::ICMPV6;

    const ECHO_REQUEST_TYPE: u8 = 128;
    const ECHO_REPLY_TYPE: u8 = 129;
}

/// Async ICMP socket.
pub trait IcmpSocket<I>: Unpin
where
    I: Ip,
{
    /// Async method for receiving an ICMP packet.
    ///
    /// Upon successful return, `buf` will contain an ICMP packet.
    fn async_recv_from(
        &self,
        buf: &mut [u8],
        cx: &mut Context<'_>,
    ) -> Poll<std::io::Result<(usize, I::Addr)>>;

    /// Async method for sending an ICMP packet.
    ///
    /// `bufs` must contain a valid ICMP packet.
    fn async_send_to_vectored(
        &self,
        bufs: &[std::io::IoSlice<'_>],
        addr: &I::Addr,
        cx: &mut Context<'_>,
    ) -> Poll<std::io::Result<usize>>;

    /// Binds this to an interface so that packets can only flow in/out via the specified
    /// interface.
    ///
    /// If `interface` is `None`, the binding is removed.
    fn bind_device(&self, interface: Option<&[u8]>) -> std::io::Result<()>;
}

/// Parameters of a ping request/reply.
#[derive(Clone, Debug, PartialEq, Eq, Hash)]
pub struct PingData<I: Ip> {
    /// The destination address of a ping request; or the source address of a ping reply.
    pub addr: I::Addr,
    /// The sequence number in the ICMP header.
    pub sequence: u16,
    /// The body of the echo request/reply.
    pub body: Vec<u8>,
}

// TODO(https://github.com/rust-lang/rust/issues/76560): Define N as the length of the message body
// rather than the length of the ICMP packet.
/// Create a ping sink and stream for pinging a unicast destination with the same body for every
/// packet.
///
/// Echo replies received with a source address not equal to `addr` will be silently dropped. Echo
/// replies with a body not equal to `body` will result in an error on the stream.
pub fn new_unicast_sink_and_stream<'a, I, S, const N: usize>(
    socket: &'a S,
    addr: &'a I::Addr,
    body: &'a [u8],
) -> (impl Sink<u16, Error = PingError> + 'a, impl Stream<Item = Result<u16, PingError>> + 'a)
where
    I: Ip,
    S: IcmpSocket<I>,
{
    (
        PingSink::new(socket).with(move |sequence| {
            futures::future::ok(PingData { addr: addr.clone(), sequence, body: body.to_vec() })
        }),
        PingStream::<I, S, N>::new(socket).try_filter_map(
            move |PingData { addr: got_addr, sequence, body: got_body }| {
                futures::future::ready(if got_addr == *addr {
                    if got_body == body {
                        Ok(Some(sequence))
                    } else {
                        Err(PingError::Body { got: got_body, want: body.to_vec() })
                    }
                } else {
                    Ok(None)
                })
            },
        ),
    )
}

// TODO(https://github.com/rust-lang/rust/issues/76560): Define N as the length of the message body
// rather than the length of the ICMP packet.
/// Stream of received ping replies.
pub struct PingStream<'a, I, S, const N: usize>
where
    I: Ip,
    S: IcmpSocket<I>,
{
    socket: &'a S,
    recv_buf: [u8; N],
    _marker: PhantomData<I>,
}

impl<'a, I, S, const N: usize> PingStream<'a, I, S, N>
where
    I: Ip,
    S: IcmpSocket<I>,
{
    /// Construct a stream from an `IcmpSocket`.
    ///
    /// `N` must be set to the length of the largest ICMP body expected
    /// to be received plus the 8 bytes of overhead due to the ICMP
    /// header, otherwise received packets may be truncated. Note
    /// that this does not need to include the 8-byte overhead of
    /// the ICMP header.
    pub fn new(socket: &'a S) -> Self {
        Self { socket, recv_buf: [0; N], _marker: PhantomData::<I> }
    }
}

impl<'a, I, S, const N: usize> futures::stream::Stream for PingStream<'a, I, S, N>
where
    I: Ip,
    S: IcmpSocket<I>,
{
    type Item = Result<PingData<I>, PingError>;

    fn poll_next(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        let ping_stream = Pin::into_inner(self);
        let buf = &mut ping_stream.recv_buf[..];
        let socket = &ping_stream.socket;
        Poll::Ready(Some(
            ready!(socket.async_recv_from(buf, cx))
                .map_err(PingError::Recv)
                .and_then(|(len, addr)| verify_packet::<I>(addr, &ping_stream.recv_buf[..len])),
        ))
    }
}

/// Sink for sending ping requests.
pub struct PingSink<'a, I, S>
where
    I: Ip,
    S: IcmpSocket<I>,
{
    socket: &'a S,
    packet: Option<(I::Addr, IcmpHeader, Vec<u8>)>,
    _marker: PhantomData<I>,
}

impl<'a, I, S> PingSink<'a, I, S>
where
    I: Ip,
    S: IcmpSocket<I>,
{
    /// Construct a sink from an `IcmpSocket`.
    pub fn new(socket: &'a S) -> Self {
        Self { socket, packet: None, _marker: PhantomData::<I> }
    }
}

impl<'a, I, S> futures::sink::Sink<PingData<I>> for PingSink<'a, I, S>
where
    I: Ip,
    S: IcmpSocket<I>,
{
    type Error = PingError;

    fn poll_ready(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Result<(), Self::Error>> {
        self.poll_flush(cx)
    }

    fn start_send(
        mut self: Pin<&mut Self>,
        PingData { addr, sequence, body }: PingData<I>,
    ) -> Result<(), Self::Error> {
        let header = IcmpHeader::new::<I>(sequence);
        assert_eq!(
            self.packet.replace((addr, header, body)),
            None,
            "start_send called while element has yet to be flushed"
        );
        Ok(())
    }

    fn poll_flush(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Result<(), Self::Error>> {
        Poll::Ready(match &self.packet {
            Some((addr, header, body)) => {
                match ready!(self.socket.async_send_to_vectored(
                    &[
                        std::io::IoSlice::new(header.as_bytes()),
                        std::io::IoSlice::new(body.as_bytes()),
                    ],
                    addr,
                    cx
                )) {
                    Ok(got) => {
                        let want = std::mem::size_of_val(&header) + body.len();
                        if got != want {
                            Err(PingError::SendLength { got, want })
                        } else {
                            self.packet = None;
                            Ok(())
                        }
                    }
                    Err(e) => Err(PingError::Send(e)),
                }
            }
            None => Ok(()),
        })
    }

    fn poll_close(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Result<(), Self::Error>> {
        self.poll_flush(cx)
    }
}

fn verify_packet<I: Ip>(addr: I::Addr, packet: &[u8]) -> Result<PingData<I>, PingError> {
    let (reply, body): (zerocopy::LayoutVerified<_, IcmpHeader>, _) =
        zerocopy::LayoutVerified::new_unaligned_from_prefix(packet)
            .ok_or_else(|| PingError::Parse)?;

    // The identifier cannot be verified, since ICMP socket implementations rewrites the field on
    // send and uses its value to demultiplex packets for delivery to sockets on receive.
    //
    // Also, don't bother verifying the checksum, since ICMP socket implementations must have
    // verified the checksum since the code and identifier fields must be inspected. Also, the
    // ICMPv6 checksum computation includes a pseudo header which includes the src and dst
    // addresses, and the dst/local address is not readily available.
    let &IcmpHeader { r#type, code, checksum: _, id: _, sequence } = reply.into_ref();

    if r#type != I::ECHO_REPLY_TYPE {
        return Err(PingError::ReplyType { got: r#type, want: I::ECHO_REPLY_TYPE });
    }

    if code != 0 {
        return Err(PingError::ReplyCode(code));
    }

    Ok(PingData { addr, sequence: sequence.into(), body: body.to_vec() })
}

#[cfg(test)]
mod test {
    use super::{IcmpHeader, IcmpSocket, Ip, Ipv4, Ipv6, PingData, PingSink, PingStream};

    use futures::{FutureExt as _, SinkExt as _, StreamExt as _, TryStreamExt as _};
    use net_declare::{std_socket_addr_v4, std_socket_addr_v6};
    use std::{
        cell::RefCell,
        collections::VecDeque,
        task::{Context, Poll},
    };
    use zerocopy::AsBytes as _;

    // A fake impl of a IcmpSocket which computes and buffers a reply when `send_to` is called,
    // which is then returned when `recv_from` is called. The order in which replies are returned
    // is guaranteed to be FIFO.
    #[derive(Default, Debug)]
    struct FakeSocket<I: Ip> {
        // NB: interior mutability is necessary here because the `IcmpSocket` trait's methods
        // operate on &self.
        buffer: RefCell<VecDeque<(Vec<u8>, I::Addr)>>,
    }

    impl<I: Ip> FakeSocket<I> {
        fn new() -> Self {
            Self { buffer: RefCell::new(VecDeque::new()) }
        }
    }

    impl<I: Ip> IcmpSocket<I> for FakeSocket<I> {
        fn async_recv_from(
            &self,
            buf: &mut [u8],
            _cx: &mut Context<'_>,
        ) -> Poll<std::io::Result<(usize, I::Addr)>> {
            Poll::Ready(
                self.buffer
                    .borrow_mut()
                    .pop_front()
                    .ok_or_else(|| {
                        std::io::Error::new(
                            std::io::ErrorKind::WouldBlock,
                            "fake socket request buffer is empty",
                        )
                    })
                    .and_then(|(reply, addr)| {
                        if buf.len() < reply.len() {
                            Err(std::io::Error::new(
                                std::io::ErrorKind::Other,
                                format!(
                                    "recv buffer too small, got: {}, want: {}",
                                    buf.len(),
                                    reply.len()
                                ),
                            ))
                        } else {
                            buf[..reply.len()].copy_from_slice(&reply);
                            Ok((reply.len(), addr))
                        }
                    }),
            )
        }

        fn async_send_to_vectored(
            &self,
            bufs: &[std::io::IoSlice<'_>],
            addr: &I::Addr,
            _cx: &mut Context<'_>,
        ) -> Poll<std::io::Result<usize>> {
            let mut buf = bufs
                .iter()
                .map(|io_slice| io_slice.as_bytes())
                .flatten()
                .copied()
                .collect::<Vec<u8>>();
            let (mut header, _): (zerocopy::LayoutVerified<_, IcmpHeader>, _) =
                match zerocopy::LayoutVerified::new_unaligned_from_prefix(&mut buf[..]) {
                    Some(layout_verified) => layout_verified,
                    None => {
                        return Poll::Ready(Err(std::io::Error::new(
                            std::io::ErrorKind::InvalidInput,
                            "failed to parse ICMP header from provided bytes",
                        )))
                    }
                };
            header.r#type = I::ECHO_REPLY_TYPE;
            let len = buf.len();
            let () = self.buffer.borrow_mut().push_back((buf, addr.clone()));
            Poll::Ready(Ok(len))
        }

        fn bind_device(&self, interface: Option<&[u8]>) -> std::io::Result<()> {
            panic!("unexpected call to bind_device({:?})", interface);
        }
    }

    trait IpExt: Ip {
        // NB: This is only a function because there is no way to create a constant for any of the
        // socket address types.
        fn test_addr() -> Self::Addr;
    }

    impl IpExt for Ipv4 {
        fn test_addr() -> Self::Addr {
            // A port must be specified in the socket addr, but it is irrelevant for ICMP sockets,
            // so just set it to 0.
            std_socket_addr_v4!("1.2.3.4:0")
        }
    }

    impl IpExt for Ipv6 {
        fn test_addr() -> Self::Addr {
            // A port must be specified in the socket addr, but it is irrelevant for ICMP sockets,
            // so just set it to 0.
            std_socket_addr_v6!("[abcd::1]:0")
        }
    }

    const PING_MESSAGE: &str = "Hello from ping library unit test!";
    const PING_COUNT: u16 = 3;
    const PING_SEQ_RANGE: std::ops::RangeInclusive<u16> = 1..=PING_COUNT;

    #[test]
    fn test_ipv4() {
        test_ping::<Ipv4>();
    }

    #[test]
    fn test_ipv6() {
        test_ping::<Ipv6>();
    }

    fn test_ping<I: IpExt>() {
        let socket = FakeSocket::<I>::new();

        let packets = PING_SEQ_RANGE
            .into_iter()
            .map(|sequence| PingData {
                addr: I::test_addr(),
                sequence,
                body: PING_MESSAGE.as_bytes().to_vec(),
            })
            .collect::<Vec<_>>();
        let packet_stream = futures::stream::iter(packets.iter().cloned());
        let () = PingSink::new(&socket)
            .send_all(&mut packet_stream.map(Ok))
            .now_or_never()
            .expect("ping request send blocked unexpectedly")
            .expect("ping send error");

        let replies =
            PingStream::<_, _, { PING_MESSAGE.len() + std::mem::size_of::<IcmpHeader>() }>::new(
                &socket,
            )
            .take(PING_COUNT.into())
            .try_collect::<Vec<_>>()
            .now_or_never()
            .expect("ping reply stream blocked unexpectedly")
            .expect("failed to collect ping reply stream");
        assert_eq!(packets, replies);
    }
}
