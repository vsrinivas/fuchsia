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
use fidl_fuchsia_net as fnet;
use fidl_fuchsia_net_ext as fnet_ext;
use fidl_fuchsia_posix as fposix;
use fidl_fuchsia_posix_socket as fposix_socket;
use fuchsia_component::server::{ServiceFs, ServiceFsDir};
use fuchsia_zircon as zx;
use futures::{io::AsyncReadExt as _, stream::SelectAll, FutureExt as _, StreamExt as _};
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
    collections::{HashMap, HashSet, VecDeque},
    convert::{TryFrom as _, TryInto as _},
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
    let mut stream_requests = SelectAll::new();
    // Maintains a set of currently bound sockets.
    let mut datagram_sockets = HashSet::new();
    let mut stream_sockets = HashMap::new();
    // TODO(https://fxbug.dev/102161): maintain a separate receive buffer for
    // each socket to support multiple concurrent sockets over loopback.
    let mut receive_buffer_size = DEFAULT_BUFFER_SIZE;
    let mut loopback_receive_buffer = VecDeque::new();
    let mut icmp_echo_receive_buffer = VecDeque::new();

    loop {
        futures::select! {
            provider_request = provider_requests.select_next_some() => {
                handle_provider_request(
                    provider_request,
                    &mut datagram_requests,
                    &mut stream_requests,
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
            },
            stream_request = stream_requests.select_next_some() => {
                let (socket, request) = stream_request;
                handle_stream_request(
                    socket,
                    request,
                    &mut stream_sockets,
                    &mut stream_requests,
                )
                .await
                .unwrap_or_else(|e| error!("error handling stream socket request: {:?}", e));
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
    stream_requests: &mut SelectAll<
        Tagged<StreamSocketCell, fposix_socket::StreamSocketRequestStream>,
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
        fposix_socket::ProviderRequest::StreamSocket { domain, proto, responder } => {
            let (client_end, request_stream) =
                fidl::endpoints::create_request_stream::<fposix_socket::StreamSocketMarker>()
                    .context("create request stream")?;
            responder.send(&mut Ok(client_end)).context("send StreamSocket response")?;
            let socket = Rc::new(RefCell::new(
                StreamSocket::new(proto, domain, request_stream.control_handle())
                    .context("create socket")?,
            ));
            info!("opened new stream socket");
            stream_requests.push(request_stream.tagged(socket));
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

const DEFAULT_BUFFER_SIZE: u64 = 32 << 10; // 32KiB

// These constants mirror those defined in
// https://cs.opensource.google/fuchsia/fuchsia/+/main:sdk/lib/fdio/socket.cc
const ZXSIO_SIGNAL_DATAGRAM_INCOMING: zx::Signals = zx::Signals::USER_0;
const ZXSIO_SIGNAL_DATAGRAM_OUTGOING: zx::Signals = zx::Signals::USER_1;
const ZXSIO_SIGNAL_STREAM_CONNECTED: zx::Signals = zx::Signals::USER_3;

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

#[derive(Debug)]
enum StreamSocketState {
    Created,
    Bound(fnet::SocketAddress),
    Listening {
        addr: fnet::SocketAddress,
        accept_queue: VecDeque<(
            fnet::SocketAddress,
            fidl::endpoints::ClientEnd<fposix_socket::StreamSocketMarker>,
        )>,
    },
    Connected {
        local: fnet::SocketAddress,
        _remote: fnet::SocketAddress,
        data_task: Option<fuchsia_async::Task<()>>,
    },
}

#[derive(Debug)]
struct StreamSocket {
    domain: fposix_socket::Domain,
    protocol: fposix_socket::StreamSocketProtocol,
    control_handle: fposix_socket::StreamSocketControlHandle,
    state: StreamSocketState,
    receive_buffer_size: u64,
    send_buffer_size: u64,
    // NB: Optional because the local end is extracted out to run the data task
    // future.
    local_socket: Option<zx::Socket>,
    peer_socket: zx::Socket,
}

type StreamSocketCell = Rc<RefCell<StreamSocket>>;
type StreamSocketMap = HashMap<fnet::SocketAddress, StreamSocketCell>;

impl StreamSocket {
    fn new(
        protocol: fposix_socket::StreamSocketProtocol,
        domain: fposix_socket::Domain,
        control_handle: fposix_socket::StreamSocketControlHandle,
    ) -> Result<Self, anyhow::Error> {
        let (local_socket, peer_socket) =
            zx::Socket::create(zx::SocketOpts::STREAM).context("create socket")?;
        Ok(Self {
            domain,
            protocol,
            control_handle,
            state: StreamSocketState::Created,
            receive_buffer_size: DEFAULT_BUFFER_SIZE,
            send_buffer_size: DEFAULT_BUFFER_SIZE,
            local_socket: Some(local_socket),
            peer_socket,
        })
    }

    fn bind(
        cell: &Rc<RefCell<Self>>,
        addr: fnet::SocketAddress,
        sockets: &mut StreamSocketMap,
    ) -> Result<(), fposix::Errno> {
        let mut this = cell.borrow_mut();
        match &mut this.state {
            state @ StreamSocketState::Created => {
                *state = StreamSocketState::Bound(addr);
            }
            state => {
                error!("can't bind socket in state {:?}", state);
                return Err(fposix::Errno::Ealready);
            }
        }
        assert_matches::assert_matches!(
            sockets.insert(addr, cell.clone()),
            None,
            "cannot bind socket to already-bound addr {:?}",
            addr
        );
        Ok(())
    }

    fn connect(
        cell: &Rc<RefCell<Self>>,
        addr: fnet::SocketAddress,
        sockets: &mut StreamSocketMap,
    ) -> Result<Tagged<StreamSocketCell, fposix_socket::StreamSocketRequestStream>, fposix::Errno>
    {
        let local_addr = {
            let borrowed = cell.borrow();
            match &borrowed.state {
                StreamSocketState::Bound(local_addr) => local_addr.clone(),
                StreamSocketState::Created => {
                    let domain = borrowed.domain;
                    // Bind is going to borrow again to change state, drop our
                    // ref.
                    drop(borrowed);
                    let addr =
                        gen_available_loopback_addr(domain, |addr| !sockets.contains_key(addr));
                    let () = Self::bind(cell, addr, sockets)?;
                    addr
                }
                state @ StreamSocketState::Listening { .. }
                | state @ StreamSocketState::Connected { .. } => {
                    error!("can't connect socket in state {:?}", state);
                    // NB: The error we return here is irrelevant since we don't
                    // expect errors in benchmarks.
                    return Err(fposix::Errno::Eio);
                }
            }
        };

        let mut this = cell.borrow_mut();

        let mut other_sock = sockets
            .get(&addr)
            .ok_or_else(|| {
                error!("attempted to connect to unknown address {:?}", addr);
                fposix::Errno::Ehostunreach
            })?
            .borrow_mut();
        let accept_queue = match &mut other_sock.state {
            StreamSocketState::Listening { addr: _, accept_queue } => accept_queue,
            state @ StreamSocketState::Created
            | state @ StreamSocketState::Bound(_)
            | state @ StreamSocketState::Connected { .. } => {
                error!("can't connect to peer socket with addr {:?} in state {:?}", addr, state);
                return Err(fposix::Errno::Ehostunreach);
            }
        };

        let (client_end, request_stream) =
            fidl::endpoints::create_request_stream::<fposix_socket::StreamSocketMarker>()
                .expect("create request stream");
        // Create the new connected socket.
        let mut other_connected =
            StreamSocket::new(this.protocol, this.domain, request_stream.control_handle())
                .map_err(|e| {
                    error!("failed to create connected socket {:?}", e);
                    fposix::Errno::Eio
                })?;

        accept_queue.push_back((local_addr.clone(), client_end));

        other_connected.state = StreamSocketState::Connected {
            local: addr.clone(),
            _remote: local_addr.clone(),
            // The connecting socket keeps the data task, we use that as a
            // signal to remove references from the demux map when the socket is
            // closed.
            data_task: None,
        };
        other_connected.receive_buffer_size = other_sock.receive_buffer_size;
        other_connected.send_buffer_size = other_sock.send_buffer_size;

        let signal_connected_and_create_io = |sock: &mut StreamSocket| {
            let zx_socket = sock.local_socket.take().unwrap();
            zx_socket
                .signal_peer(zx::Signals::NONE, ZXSIO_SIGNAL_STREAM_CONNECTED)
                .expect("signal connected");
            fuchsia_async::Socket::from_socket(zx_socket).expect("create fasync socket").split()
        };

        let (left_reader, left_writer) = signal_connected_and_create_io(&mut *this);
        let left_reader_buffer = this.send_buffer_size + other_connected.receive_buffer_size;

        let (right_reader, right_writer) = signal_connected_and_create_io(&mut other_connected);
        let right_reader_buffer = other_connected.send_buffer_size + this.receive_buffer_size;

        let data_task = async move {
            let result = futures::future::try_join(
                copy_bytes(left_reader, right_writer, left_reader_buffer),
                copy_bytes(right_reader, left_writer, right_reader_buffer),
            )
            .await;
            // NB: We don't expect the data task to finish, it is dropped when
            // the socket is closed. Log at error to ensure the assumption
            // holds.
            error!("unexpectedly finished data copying task {:?}", result);
        };
        this.state = StreamSocketState::Connected {
            local: local_addr,
            _remote: addr,
            data_task: Some(fuchsia_async::Task::local(data_task)),
        };

        Ok(request_stream.tagged(Rc::new(RefCell::new(other_connected))))
    }

    fn listen(&mut self) -> Result<(), fposix::Errno> {
        self.state = match &mut self.state {
            StreamSocketState::Bound(local_addr) => StreamSocketState::Listening {
                addr: local_addr.clone(),
                accept_queue: VecDeque::new(),
            },
            state @ StreamSocketState::Created
            | state @ StreamSocketState::Listening { .. }
            | state @ StreamSocketState::Connected { .. } => {
                error!("can't listen on socket in invalid state {:?}", state);
                // NB: The error we return here is a bit irrelevant we don't
                // need to be compliant.
                return Err(fposix::Errno::Eio);
            }
        };
        Ok(())
    }

    fn accept(
        &mut self,
    ) -> Result<
        (fnet::SocketAddress, fidl::endpoints::ClientEnd<fposix_socket::StreamSocketMarker>),
        fposix::Errno,
    > {
        match &mut self.state {
            StreamSocketState::Listening { addr: _, accept_queue } => {
                accept_queue.pop_back().ok_or_else(|| {
                    error!("calling accept with an empty queue is not supported");
                    fposix::Errno::Enoent
                })
            }
            state @ StreamSocketState::Created
            | state @ StreamSocketState::Bound(_)
            | state @ StreamSocketState::Connected { .. } => {
                error!("can't accept on socket in invalid state {:?}", state);
                // NB: The error we return here is irrelevant since we don't
                // expect errors in benchmarks.
                Err(fposix::Errno::Eio)
            }
        }
    }

    fn close(&self, sockets: &mut StreamSocketMap) {
        match &self.state {
            // A created socket is not in the map because it doesn't have a
            // local address. A connected socket with no data_task never makes
            // it into the map (see connect).
            StreamSocketState::Created
            | StreamSocketState::Connected { local: _, _remote: _, data_task: None } => (),
            StreamSocketState::Bound(addr)
            | StreamSocketState::Listening { addr, accept_queue: _ }
            | StreamSocketState::Connected { local: addr, _remote: _, data_task: Some(_) } => {
                assert_matches::assert_matches!(sockets.remove(&addr), Some(_));
            }
        }
        self.control_handle.shutdown();
    }

    fn get_sock_name(&self) -> Result<fnet::SocketAddress, fposix::Errno> {
        match &self.state {
            state @ StreamSocketState::Created => {
                // NB: An unbound socket actually returns all zeroes for IP and
                // port, but it's not exercised by the flow in fake netstack. We
                // log an error to cause loud failures in case that changes.
                error!("called getsockname on socket with bad state {:?}", state);
                Err(fposix::Errno::Enotconn)
            }
            StreamSocketState::Bound(addr)
            | StreamSocketState::Listening { addr, accept_queue: _ }
            | StreamSocketState::Connected { local: addr, _remote: _, data_task: _ } => {
                Ok(addr.clone())
            }
        }
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
                .send(fposix_socket::SynchronousDatagramSocketDescribeResponse {
                    event: Some(event),
                    ..fposix_socket::SynchronousDatagramSocketDescribeResponse::EMPTY
                })
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
                        let addr = gen_available_loopback_addr(socket.domain, |addr| {
                            !sockets.contains(addr)
                        });
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
                    icmp_echo_receive_buffer.push_back((
                        make_loopback_src_addr_with_port(socket.domain, icmp_id),
                        reply,
                    ));
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

async fn handle_stream_request(
    socket: StreamSocketCell,
    request: Result<fposix_socket::StreamSocketRequest, fidl::Error>,
    sockets: &mut StreamSocketMap,
    stream_requests: &mut SelectAll<
        Tagged<StreamSocketCell, fposix_socket::StreamSocketRequestStream>,
    >,
) -> Result<(), anyhow::Error> {
    match request.context("receive request")? {
        fposix_socket::StreamSocketRequest::Close { responder } => {
            responder.send(&mut Ok(())).context("send Close response")?;
            socket.borrow().close(sockets);
        }
        fposix_socket::StreamSocketRequest::Describe { responder } => {
            let socket = socket
                .borrow()
                .peer_socket
                .duplicate_handle(zx::Rights::SAME_RIGHTS)
                .context("duplicate socket")?;
            responder
                .send(fposix_socket::StreamSocketDescribeResponse {
                    socket: Some(socket),
                    ..fposix_socket::StreamSocketDescribeResponse::EMPTY
                })
                .context("send Describe response")?;
        }
        fposix_socket::StreamSocketRequest::Bind { addr, responder } => {
            responder
                .send(&mut StreamSocket::bind(&socket, addr, sockets))
                .context("send Bind response")?;
        }
        fposix_socket::StreamSocketRequest::Connect { addr, responder } => {
            responder
                .send(
                    &mut StreamSocket::connect(&socket, addr, sockets)
                        .map(|stream| stream_requests.push(stream)),
                )
                .context("send Connect response")?;
        }
        fposix_socket::StreamSocketRequest::Listen { backlog: _, responder } => {
            responder.send(&mut socket.borrow_mut().listen()).context("send Listen response")?;
        }
        fposix_socket::StreamSocketRequest::Accept { want_addr, responder } => {
            responder
                .send(
                    &mut socket.borrow_mut().accept().map(|(addr, client_end)| {
                        (want_addr.then(move || Box::new(addr)), client_end)
                    }),
                )
                .context("send Accept response")?;
        }
        fposix_socket::StreamSocketRequest::SetSendBuffer { value_bytes, responder } => {
            let mut socket = socket.borrow_mut();
            let mut result = match socket.state {
                StreamSocketState::Created
                | StreamSocketState::Bound(_)
                | StreamSocketState::Listening { .. } => {
                    socket.send_buffer_size = value_bytes;
                    Ok(())
                }
                StreamSocketState::Connected { .. } => {
                    error!("setting buffer size in connected state is not supported");
                    Err(fposix::Errno::Eopnotsupp)
                }
            };
            responder.send(&mut result).context("send SetSendBuffer response")?;
        }

        fposix_socket::StreamSocketRequest::GetSendBuffer { responder } => {
            responder
                .send(&mut Ok(socket.borrow().send_buffer_size))
                .context("send GetSendBuffer response")?;
        }
        fposix_socket::StreamSocketRequest::SetReceiveBuffer { value_bytes, responder } => {
            let mut socket = socket.borrow_mut();
            let mut result = match socket.state {
                StreamSocketState::Created
                | StreamSocketState::Bound(_)
                | StreamSocketState::Listening { .. } => {
                    socket.receive_buffer_size = value_bytes;
                    Ok(())
                }
                StreamSocketState::Connected { .. } => {
                    error!("setting buffer size in connected state is not supported");
                    Err(fposix::Errno::Eopnotsupp)
                }
            };
            responder.send(&mut result).context("send SetReceiveBuffer response")?;
        }
        fposix_socket::StreamSocketRequest::GetReceiveBuffer { responder } => {
            responder
                .send(&mut Ok(socket.borrow().receive_buffer_size))
                .context("send GetReceiveBuffer response")?;
        }
        fposix_socket::StreamSocketRequest::GetSockName { responder } => {
            responder
                .send(&mut socket.borrow().get_sock_name())
                .context("send GetSockName response")?;
        }
        fposix_socket::StreamSocketRequest::SetTcpNoDelay { value: _, responder } => {
            responder.send(&mut Ok(())).context("send SetTcpNoDelay response")?;
        }
        other => {
            error!("got unexpected stream socket request: {:#?}", other);
            socket.borrow().close(sockets);
        }
    }
    Ok(())
}

/// This function hand-rolls a very simple way of greedily shuttling
/// bytes between a reader and a writer using a single buffer between
/// them. Something like this, wrapped in a single type that is both
/// AsyncWrite and AsyncRead with a preferably circular buffer backing
/// it would probably be better and useful, alas it doesn't exist in
/// futures or async_utils at the moment of writing.
async fn copy_bytes<
    R: futures::io::AsyncReadExt + std::marker::Unpin,
    W: futures::io::AsyncWriteExt + std::marker::Unpin,
>(
    mut reader: R,
    mut writer: W,
    buffer_size: u64,
) -> Result<(), anyhow::Error> {
    let mut buffer = Vec::new();
    // We have a single buffer with two pointers on it. write_pos encodes the
    // position of the buffer we're going to write new bytes into (from the
    // reader). And read_pos is the position of the buffer we read from (into
    // the writer).
    let buffer_size = usize::try_from(buffer_size).unwrap();
    buffer.resize(buffer_size, 0u8);
    let mut write_pos = 0;
    let mut read_pos = 0;
    let mut read_done = false;
    loop {
        assert!(
            read_pos <= write_pos && write_pos <= buffer_size,
            "position invariant violation r={} <= w={} <= t={}",
            read_pos,
            write_pos,
            buffer_size,
        );
        // Split the buffer into 3 disjoint regions:
        //  - receive is from the writing position to the end of the buffer,
        //    which is where we can put data received from the reader.
        //  - available is from the current read position to the written
        //    position (exclusive). That's the region of the buffer we can feed
        //    into the writer.
        //  - The remaining data, from the start of the buffer to the read
        //    position, is data that has already been shuttled from the reader
        //    to the writer.
        //
        // This simple algorithm supports the oneshot payload shuttling that
        // happens in benchmarks with the minimum amount of data copying
        // possible. We reset the positions when the read and write pointers
        // meet each other, but a more robust algorithm with a circular buffer
        // would be needed for sustained performant operation.
        let (written, receive) = (&mut buffer[..]).split_at_mut(write_pos);
        let (_, available) = written.split_at_mut(read_pos);

        // Prepare a future that takes bytes from the reader into receive if
        // there is enough receiving space and we have not observed EOF.
        let mut rd_fut = if receive.is_empty() || read_done {
            futures::future::pending().left_future()
        } else {
            reader.read(receive).right_future()
        }
        .fuse();
        // Prepare a future that takes bytes from available into the writer if
        // there are any.
        let mut wr_fut = if available.is_empty() {
            futures::future::pending().left_future()
        } else {
            writer.write(available).right_future()
        }
        .fuse();
        futures::select! {
            rd = rd_fut => {
                let rd = rd.context("reader")?;
                // When the reader reaches end of file we observe a read of zero
                // bytes.
                if rd == 0 {
                    read_done = true;
                } else {
                    write_pos += rd;
                }
            },
            wr = wr_fut => {
                let wr = wr.context("writer")?;
                assert_ne!(wr, 0);
                read_pos += wr;
            }
        }

        if read_pos == write_pos {
            read_pos = 0;
            write_pos = 0;
        }
    }
}

fn make_loopback_src_addr_with_port(
    domain: fposix_socket::Domain,
    port: u16,
) -> fnet::SocketAddress {
    let ip: std::net::IpAddr = match domain {
        fposix_socket::Domain::Ipv4 => std::net::Ipv4Addr::LOCALHOST.into(),
        fposix_socket::Domain::Ipv6 => std::net::Ipv6Addr::LOCALHOST.into(),
    };
    fnet_ext::SocketAddress(std::net::SocketAddr::from((ip, port))).into()
}

fn gen_available_loopback_addr<F: Fn(&fnet::SocketAddress) -> bool>(
    domain: fposix_socket::Domain,
    free: F,
) -> fnet::SocketAddress {
    let mut rng = rand::thread_rng();
    loop {
        let port = rng.gen_range(EPHEMERAL_PORTS);
        let addr = make_loopback_src_addr_with_port(domain, port);
        if free(&addr) {
            break addr;
        }
    }
}
