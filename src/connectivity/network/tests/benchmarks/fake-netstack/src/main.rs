// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A fake netstack that implements a limited subset of
//! `fuchsia.posix.socket/Provider` in order to support benchmarking API
//! overhead.

use anyhow::Context as _;
use async_utils::stream::{Tagged, TryFlattenUnorderedExt as _, WithTag as _};
use fidl::{
    endpoints::{ControlHandle as _, RequestStream as _},
    HandleBased as _, Peered as _,
};
use fidl_fuchsia_io as fio;
use fidl_fuchsia_net as fnet;
use fidl_fuchsia_net_ext as fnet_ext;
use fidl_fuchsia_posix as fposix;
use fidl_fuchsia_posix_socket as fposix_socket;
use fuchsia_component::server::{ServiceFs, ServiceFsDir};
use fuchsia_zircon as zx;
use futures::{stream::SelectAll, StreamExt as _};
use net_types::{
    ip::{Ip, Ipv4, Ipv6},
    SpecifiedAddr,
};
use packet::{ParseBuffer as _, Serializer as _};
use packet_formats::icmp::{
    IcmpEchoReply, IcmpEchoRequest, IcmpMessage, IcmpPacketBuilder, IcmpPacketRaw, IcmpUnusedCode,
};
use rand::Rng as _;
use std::{
    cell::RefCell,
    collections::{HashSet, VecDeque},
    convert::TryInto as _,
    rc::Rc,
};
use tracing::{error, info};

#[fuchsia::main]
async fn main() {
    info!("started");

    let mut fs = ServiceFs::new_local();
    let _: &mut ServiceFsDir<'_, _> =
        fs.dir("svc").add_fidl_service(|s: fposix_socket::ProviderRequestStream| s);
    let _: &mut ServiceFs<_> = fs.take_and_serve_directory_handle().expect("take startup handle");
    let mut provider_requests = fs.fuse().map(Ok).try_flatten_unordered();

    let mut datagram_requests = SelectAll::new();
    // Maintains a set of currently bound sockets.
    let mut datagram_sockets = HashSet::new();
    // TODO(https://fxbug.dev/102161): maintain a separate receive buffer for
    // each socket to support multiple concurrent sockets over loopback.
    let mut receive_buffer_size = RECEIVE_BUFFER_SIZE;
    let mut loopback_receive_buffer = VecDeque::new();
    let mut icmp_echo_receive_buffer = VecDeque::new();

    loop {
        futures::select! {
            provider_request = provider_requests.select_next_some() => {
                handle_provider_request(
                    provider_request,
                    &mut datagram_requests,
                )
                .await
                .unwrap_or_else(|e| error!("error handling socket provider request: {:?}", e));
            }
            datagram_request = datagram_requests.select_next_some() => {
                let (socket, request) = datagram_request;
                handle_datagram_request(
                    socket,
                    request,
                    &mut datagram_sockets,
                    &mut loopback_receive_buffer,
                    &mut receive_buffer_size,
                    &mut icmp_echo_receive_buffer,
                )
                .await
                .unwrap_or_else(|e| error!("error handling datagram socket request: {:?}", e));
            }
            complete => break,
        };
    }
}

async fn handle_provider_request(
    request: Result<fposix_socket::ProviderRequest, fidl::Error>,
    datagram_requests: &mut SelectAll<
        Tagged<Rc<RefCell<DatagramSocket>>, fposix_socket::SynchronousDatagramSocketRequestStream>,
    >,
) -> Result<(), anyhow::Error> {
    match request.context("receive request")? {
        fposix_socket::ProviderRequest::DatagramSocket { domain, proto, responder } => {
            let (client_end, request_stream) = fidl::endpoints::create_request_stream::<
                fposix_socket::SynchronousDatagramSocketMarker,
            >()
            .context("create request stream")?;
            responder
                .send(&mut Ok(
                    fposix_socket::ProviderDatagramSocketResponse::SynchronousDatagramSocket(
                        client_end,
                    ),
                ))
                .context("send DatagramSocket response")?;
            let socket = Rc::new(RefCell::new(
                DatagramSocket::new(proto, domain, request_stream.control_handle())
                    .context("create socket")?,
            ));
            datagram_requests.push(request_stream.tagged(socket));
            match proto {
                fposix_socket::DatagramSocketProtocol::Udp => {
                    info!("opened new UDP socket");
                }
                fposix_socket::DatagramSocketProtocol::IcmpEcho => {
                    info!("opened new ICMP echo socket");
                }
            }
        }
        fposix_socket::ProviderRequest::StreamSocket { .. } => {
            todo!("https://fxbug.dev/101918: implement stream sockets in fake netstack");
        }
        request @ (fposix_socket::ProviderRequest::InterfaceIndexToName { .. }
        | fposix_socket::ProviderRequest::InterfaceNameToIndex { .. }
        | fposix_socket::ProviderRequest::InterfaceNameToFlags { .. }
        | fposix_socket::ProviderRequest::GetInterfaceAddresses { .. }
        | fposix_socket::ProviderRequest::DatagramSocketDeprecated { .. }) => {
            panic!("unsupported fuchsia.posix.socket/Provider request: {:?}", request);
        }
    }
    Ok(())
}

const RECEIVE_BUFFER_SIZE: u64 = 32 << 10; // 32KiB

// These constants mirror those defined in
// https://cs.opensource.google/fuchsia/fuchsia/+/main:sdk/lib/fdio/socket.cc
const ZXSIO_SIGNAL_DATAGRAM_INCOMING: zx::Signals = zx::Signals::USER_0;
const ZXSIO_SIGNAL_DATAGRAM_OUTGOING: zx::Signals = zx::Signals::USER_1;

// The IANA suggests this range for ephemeral ports in RFC 6335, section 6.
const EPHEMERAL_PORTS: std::ops::RangeInclusive<u16> = 49152..=65535;

struct DatagramSocket {
    domain: fposix_socket::Domain,
    protocol: fposix_socket::DatagramSocketProtocol,
    bound: Option<fnet::SocketAddress>,
    connected: Option<fnet::SocketAddress>,
    control_handle: fposix_socket::SynchronousDatagramSocketControlHandle,
    _local_event: zx::EventPair,
    peer_event: zx::EventPair,
}

impl DatagramSocket {
    fn new(
        protocol: fposix_socket::DatagramSocketProtocol,
        domain: fposix_socket::Domain,
        control_handle: fposix_socket::SynchronousDatagramSocketControlHandle,
    ) -> Result<Self, anyhow::Error> {
        let (local_event, peer_event) = zx::EventPair::create().context("create event pair")?;
        local_event
            .signal_peer(
                zx::Signals::NONE,
                ZXSIO_SIGNAL_DATAGRAM_INCOMING | ZXSIO_SIGNAL_DATAGRAM_OUTGOING,
            )
            .context("signal peer")?;
        Ok(Self {
            protocol,
            domain,
            bound: None,
            connected: None,
            control_handle,
            _local_event: local_event,
            peer_event,
        })
    }

    fn bind(&mut self, addr: fnet::SocketAddress, sockets: &mut HashSet<fnet::SocketAddress>) {
        self.bound = Some(addr);
        assert!(sockets.insert(addr), "cannot bind socket to already-bound addr {:?}", addr);
    }
}

fn serialize_icmp_echo_reply<I>(buf: packet::Buf<Vec<u8>>, reply: IcmpEchoReply) -> Vec<u8>
where
    I: packet_formats::icmp::IcmpIpExt,
    <I as Ip>::Addr: From<SpecifiedAddr<<I as Ip>::Addr>>,
    IcmpEchoReply: IcmpMessage<I, &'static [u8], Code = IcmpUnusedCode>,
{
    buf.encapsulate(IcmpPacketBuilder::<I, &'static [u8], _>::new(
        I::LOOPBACK_ADDRESS,
        I::LOOPBACK_ADDRESS,
        IcmpUnusedCode,
        reply,
    ))
    .serialize_no_alloc_outer()
    .expect("serialize ICMP echo reply")
    .into_inner()
}

async fn handle_datagram_request(
    socket: Rc<RefCell<DatagramSocket>>,
    request: Result<fposix_socket::SynchronousDatagramSocketRequest, fidl::Error>,
    sockets: &mut HashSet<fnet::SocketAddress>,
    loopback_receive_buffer: &mut VecDeque<(fnet::SocketAddress, Vec<u8>)>,
    receive_buffer_size: &mut u64,
    icmp_echo_receive_buffer: &mut VecDeque<(fnet::SocketAddress, Vec<u8>)>,
) -> Result<(), anyhow::Error> {
    match request.context("receive request")? {
        fposix_socket::SynchronousDatagramSocketRequest::Describe { responder } => {
            let event = socket
                .borrow()
                .peer_event
                .duplicate_handle(zx::Rights::BASIC)
                .context("duplicate peer event")?;
            responder
                .send(&mut fio::NodeInfo::SynchronousDatagramSocket(
                    fio::SynchronousDatagramSocket { event },
                ))
                .context("send Describe response")?;
        }
        fposix_socket::SynchronousDatagramSocketRequest::Bind { addr, responder } => {
            socket.borrow_mut().bind(addr, sockets);
            responder.send(&mut Ok(())).context("send Bind response")?;
        }
        fposix_socket::SynchronousDatagramSocketRequest::Close { responder } => {
            responder.send(&mut Ok(())).context("send Close response")?;
            let socket = socket.borrow();
            socket.control_handle.shutdown();
            if let Some(addr) = socket.bound {
                assert!(
                    sockets.remove(&addr),
                    "socket bound to {:?}, but was not tracked in set of bound sockets",
                    addr,
                );
            }
            info!("got close request for socket");
        }
        fposix_socket::SynchronousDatagramSocketRequest::GetReceiveBuffer { responder } => {
            responder
                .send(&mut Ok(*receive_buffer_size))
                .context("send GetReceiveBuffer response")?;
        }
        fposix_socket::SynchronousDatagramSocketRequest::SetReceiveBuffer {
            value_bytes,
            responder,
        } => {
            // NB: the fake netstack doesn't actually enforce receive buffer size.
            *receive_buffer_size = value_bytes;
            info!("modified size of receive buffer to {}", receive_buffer_size);
            responder.send(&mut Ok(())).context("send SetReceiveBuffer response")?;
        }
        fposix_socket::SynchronousDatagramSocketRequest::GetSockName { responder } => {
            if let Some(addr) = socket.borrow().bound {
                responder.send(&mut Ok(addr)).context("send GetSockName response")?
            } else {
                responder
                    .send(&mut Err(fposix::Errno::Enotsock))
                    .context("send GetSockName response")?;
            }
        }
        fposix_socket::SynchronousDatagramSocketRequest::Connect { addr, responder } => {
            socket.borrow_mut().connected = Some(addr);
            responder.send(&mut Ok(())).context("send Connect response")?;
        }
        fposix_socket::SynchronousDatagramSocketRequest::SendMsg {
            addr,
            data,
            control: _,
            flags: _,
            responder,
        } => {
            let mut socket = socket.borrow_mut();

            let dst = if let Some(addr) = addr {
                *addr
            } else if let Some(addr) = socket.connected {
                addr
            } else {
                responder
                    .send(&mut Err(fposix::Errno::Enotconn))
                    .context("send SendMsg response")?;
                return Ok(());
            };
            // Ensure this message is destined for the loopback address.
            let fnet_ext::SocketAddress(std_dst) = dst.into();
            assert!(std_dst.ip().is_loopback(), "cannot send to non-loopback IP address");

            let make_loopback_src_addr_with_port = |port| match socket.domain {
                fposix_socket::Domain::Ipv4 => fnet_ext::SocketAddress(std::net::SocketAddr::from(
                    (std::net::Ipv4Addr::LOCALHOST, port),
                ))
                .into(),
                fposix_socket::Domain::Ipv6 => fnet_ext::SocketAddress(std::net::SocketAddr::from(
                    (std::net::Ipv6Addr::LOCALHOST, port),
                ))
                .into(),
            };

            match socket.protocol {
                fposix_socket::DatagramSocketProtocol::Udp => {
                    // Ensure that there exists another socket bound to the destination port.
                    assert!(
                        sockets.contains(&dst),
                        "cannot send to port {} on which no socket is bound",
                        std_dst.port(),
                    );
                    let from = if let Some(addr) = socket.bound {
                        addr
                    } else {
                        // Auto-bind the socket by selecting a local port.
                        let mut rng = rand::thread_rng();
                        let addr = loop {
                            let port = rng.gen_range(EPHEMERAL_PORTS);
                            let addr = make_loopback_src_addr_with_port(port);
                            if !sockets.contains(&addr) {
                                break addr;
                            }
                        };
                        socket.bind(addr, sockets);
                        addr
                    };

                    let len = data.len().try_into().unwrap();
                    loopback_receive_buffer.push_back((from, data));
                    responder.send(&mut Ok(len)).context("send SendMsg response")?;
                }
                fposix_socket::DatagramSocketProtocol::IcmpEcho => {
                    let len = data.len().try_into().unwrap();
                    let mut buf = packet::Buf::new(data, ..);
                    let (reply, icmp_id) = match socket.domain {
                        fposix_socket::Domain::Ipv4 => {
                            let request = buf
                                .parse::<IcmpPacketRaw<Ipv4, _, IcmpEchoRequest>>()
                                .context("parse ICMPv4 echo request")?;
                            let reply = request.message().reply();
                            let id = reply.id();
                            (serialize_icmp_echo_reply::<Ipv4>(buf, reply), id)
                        }
                        fposix_socket::Domain::Ipv6 => {
                            let request = buf
                                .parse::<IcmpPacketRaw<Ipv6, _, IcmpEchoRequest>>()
                                .context("parse ICMPv6 echo request")?;
                            let reply = request.message().reply();
                            let id = reply.id();
                            (serialize_icmp_echo_reply::<Ipv6>(buf, reply), id)
                        }
                    };
                    // Queue up the ICMP echo reply in the receive buffer for the socket.
                    icmp_echo_receive_buffer
                        .push_back((make_loopback_src_addr_with_port(icmp_id), reply));
                    responder.send(&mut Ok(len)).context("send SendMsg response")?;
                }
            }
        }
        fposix_socket::SynchronousDatagramSocketRequest::RecvMsg {
            want_addr,
            data_len,
            want_control: _,
            flags: _,
            responder,
        } => {
            let socket = socket.borrow();

            let (from, mut data) = match socket.protocol {
                fposix_socket::DatagramSocketProtocol::Udp => {
                    assert!(socket.bound.is_some(), "socket must be bound to receive");
                    loopback_receive_buffer
                }
                fposix_socket::DatagramSocketProtocol::IcmpEcho => icmp_echo_receive_buffer,
            }
            .pop_front()
            // TODO(https://fxbug.dev/102839): block until data is available (or return
            // `EWOULDBLOCK` if `O_NONBLOCK` is set on the socket).
            //
            // It's OK to expect data to be in the receive buffer because the benchmarks
            // currently always send synchronously before receiving.
            .expect("receive buffer should not be empty");

            let want_len = data_len.try_into().unwrap();
            let truncated = data.len().saturating_sub(want_len);
            data.truncate(want_len);
            let from = want_addr.then(|| Box::new(from));
            responder
                .send(&mut Ok((
                    from,
                    data,
                    fposix_socket::DatagramSocketRecvControlData::EMPTY,
                    truncated.try_into().unwrap(),
                )))
                .context("send RecvMsg response")?;
        }
        other => error!("got unexpected datagram socket request: {:#?}", other),
    }
    Ok(())
}
