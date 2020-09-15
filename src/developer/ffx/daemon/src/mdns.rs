// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::discovery::{TargetFinder, TargetFinderConfig},
    crate::events,
    crate::events::TryIntoTargetInfo,
    crate::net,
    crate::net::IsLocalAddr,
    crate::target::*,
    ::mdns::protocol as dns,
    anyhow::Result,
    async_std::sync::Mutex,
    async_std::task::JoinHandle,
    async_std::{net::UdpSocket, task},
    futures::FutureExt,
    packet::{InnerPacketBuilder, ParseBuffer},
    std::collections::HashMap,
    std::collections::HashSet,
    std::fmt::Write,
    std::net::{IpAddr, Ipv4Addr, Ipv6Addr, SocketAddr, SocketAddrV4, SocketAddrV6},
    std::sync::Arc,
    std::time::Duration,
    zerocopy::ByteSlice,
};

const MDNS_MCAST_V4: Ipv4Addr = Ipv4Addr::new(224, 0, 0, 251);
const MDNS_MCAST_V6: Ipv6Addr = Ipv6Addr::new(0xff02, 0, 0, 0, 0, 0, 0, 0x00fb);
const MDNS_PORT: u16 = 5353;

pub struct MdnsTargetFinder {
    socket_tasks: Arc<Mutex<HashMap<SocketAddr, JoinHandle<()>>>>,
    interface_discovery_task: Option<JoinHandle<()>>,
    config: TargetFinderConfig,
}

// interface_discovery iterates over all multicast interfaces and adds them to
// the socket_tasks if there is not already a task for that interface.
async fn interface_discovery(
    socket_tasks: Arc<Mutex<HashMap<SocketAddr, JoinHandle<()>>>>,
    e: events::Queue<events::DaemonEvent>,
    discovery_interval: Duration,
    query_interval: Duration,
    ttl: u32,
) {
    let v4_listen_socket = Arc::new(make_listen_socket((MDNS_MCAST_V4, MDNS_PORT).into()).unwrap());
    let v6_listen_socket = Arc::new(make_listen_socket((MDNS_MCAST_V6, MDNS_PORT).into()).unwrap());
    {
        let mut tasks = socket_tasks.lock().await;
        tasks.insert(
            (MDNS_MCAST_V4, MDNS_PORT).into(),
            task::spawn(recv_loop(v4_listen_socket.clone(), e.clone())),
        );
        tasks.insert(
            (MDNS_MCAST_V6, MDNS_PORT).into(),
            task::spawn(recv_loop(v6_listen_socket.clone(), e.clone())),
        );
    }

    loop {
        // Block holds the lock on the task map.
        {
            let mut tasks = socket_tasks.lock().await;

            for iface in net::get_mcast_interfaces().unwrap_or(Vec::new()) {
                let maybe_id = iface.id();
                // Note: further below we only map over this result, and don't log repeatedly.
                match maybe_id.as_ref() {
                    Ok(id) => {
                        let _ = v6_listen_socket.join_multicast_v6(&MDNS_MCAST_V6, *id);
                    }
                    Err(err) => {
                        log::warn!("{}", err);
                    }
                }

                for addr in iface.addrs.iter() {
                    // TODO(raggi): remove duplicate joins, log unexpected errors
                    if let SocketAddr::V4(addr) = addr {
                        let _ = v4_listen_socket.join_multicast_v4(MDNS_MCAST_V4, *addr.ip());
                    }

                    if tasks.get(addr).is_some() {
                        continue;
                    }

                    log::debug!("mdns: discovered new interface addr: {}: {}", iface.name, addr);

                    maybe_id.iter().next().map(|id| {
                        let mut saddr = addr.clone();
                        saddr.set_port(MDNS_PORT);
                        let sock = match make_sender_socket(*id, saddr, ttl) {
                            Ok(sock) => sock,
                            Err(err) => {
                                log::info!("mdns: failed to bind {}: {}", addr, err);
                                return;
                            }
                        };

                        tasks.insert(
                            addr.clone(),
                            task::spawn(query_recv_loop(
                                Arc::new(sock),
                                e.clone(),
                                query_interval,
                                socket_tasks.clone(),
                            )),
                        );
                    });
                }
            }
        }
        task::sleep(discovery_interval).await;
    }
}

// recv_loop reads packets from sock. If the packet is a Fuchsia mdns packet, a
// corresponding mdns event is published to the queue. All other packets are
// silently discarded.
async fn recv_loop(sock: Arc<UdpSocket>, e: events::Queue<events::DaemonEvent>) {
    loop {
        let mut buf = &mut [0u8; 1500][..];
        let addr = match sock.recv_from(&mut buf).await {
            Ok((sz, addr)) => {
                buf = &mut buf[..sz];
                addr
            }
            _ => continue,
        };

        let msg = match buf.parse::<dns::Message<_>>() {
            Ok(msg) => msg,
            _ => continue,
        };

        // Note: important, otherwise non-local responders could add themselves.
        if !addr.ip().is_local_addr() {
            continue;
        }

        if !is_fuchsia_response(&msg) {
            continue;
        }

        if let Ok(info) = msg.try_into_target_info(addr) {
            log::debug!(
                "received mdns packet from {} ({}) on {}",
                addr,
                info.nodename,
                sock.local_addr().unwrap()
            );
            e.push(events::DaemonEvent::WireTraffic(events::WireTrafficType::Mdns(info)))
                .await
                .unwrap_or_else(|err| log::debug!("mdns discovery was unable to publish: {}", err));
        }
    }
}

lazy_static::lazy_static! {
    static ref QUERY_BUF: &'static [u8] = {
        let question = dns::QuestionBuilder::new(
            dns::DomainBuilder::from_str("_fuchsia._udp.local").unwrap(),
            dns::Type::Ptr,
            dns::Class::In,
            false,
        );

        let mut message = dns::MessageBuilder::new(0, true);
        message.add_question(question);

        let mut buf = vec![0; message.bytes_len()];
        message.serialize(buf.as_mut_slice());
        Box::leak(buf.into_boxed_slice())
    };
}

// query_loop broadcasts an mdns query on sock every interval.
async fn query_loop(sock: Arc<UdpSocket>, interval: Duration) {
    loop {
        if let Err(err) = sock.send(&QUERY_BUF).await {
            log::info!("mdns query failed: {:?}", err);
            return;
        }

        task::sleep(interval).await;
    }
}

// sock is dispatched with a recv_loop, as well as broadcasting an
// mdns query to discover Fuchsia devices every interval.
async fn query_recv_loop(
    sock: Arc<UdpSocket>,
    e: events::Queue<events::DaemonEvent>,
    interval: Duration,
    tasks: Arc<Mutex<HashMap<SocketAddr, JoinHandle<()>>>>,
) {
    let mut recv = recv_loop(sock.clone(), e).boxed().fuse();
    let mut query = query_loop(sock.clone(), interval).boxed().fuse();

    let _ = futures::select!(
        _ = recv => {},
        _ = query => {},
    );

    let addr = match sock.local_addr() {
        Ok(addr) => addr,
        Err(err) => {
            log::error!("mdns: failed to shutdown: {:?}", err);
            return;
        }
    };

    tasks.lock().await.remove(&addr).map(|t| t.cancel());
    log::info!("mdns: shut down query socket {}", addr);
}

// TODO(fxb/44855): This needs to be e2e tested.
impl TargetFinder for MdnsTargetFinder {
    fn new(config: &TargetFinderConfig) -> Result<Self> {
        Ok(Self {
            socket_tasks: Arc::new(Mutex::new(HashMap::new())),
            config: config.clone(),
            interface_discovery_task: None,
        })
    }

    fn start(&mut self, e: events::Queue<events::DaemonEvent>) -> Result<()> {
        self.interface_discovery_task = Some(task::spawn(interface_discovery(
            self.socket_tasks.clone(),
            e.clone(),
            self.config.interface_discovery_interval,
            self.config.broadcast_interval,
            self.config.mdns_ttl,
        )));

        Ok(())
    }
}

#[derive(Debug, Eq, PartialEq)]
pub enum MdnsConvertError {
    NodenameMissing,
}

fn is_fuchsia_response<B: zerocopy::ByteSlice + Clone>(m: &dns::Message<B>) -> bool {
    m.answers.len() >= 1 && m.answers[0].domain == "_fuchsia._udp.local"
}

impl<B: ByteSlice + Clone> events::TryIntoTargetInfo for dns::Message<B> {
    type Error = MdnsConvertError;

    fn try_into_target_info(self, src: SocketAddr) -> Result<events::TargetInfo, Self::Error> {
        let mut nodename = String::new();
        let mut addrs: HashSet<TargetAddr> = [src.into()].iter().cloned().collect();
        for record in self.additional.iter() {
            if record.rtype != dns::Type::A && record.rtype != dns::Type::Aaaa {
                continue;
            }
            if nodename.len() == 0 {
                write!(nodename, "{}", record.domain).unwrap();
                nodename = nodename.trim_end_matches(".local").into();
            }
            // The records here also have the IP addresses of
            // the machine, however these could be different if behind a NAT
            // (as with QEMU). Later it might be useful to store them in the
            // Target struct.
        }
        if nodename.len() == 0 {
            return Err(MdnsConvertError::NodenameMissing);
        }
        Ok(events::TargetInfo { nodename, addresses: addrs.drain().collect() })
    }
}

fn make_listen_socket(listen_addr: SocketAddr) -> Result<UdpSocket> {
    let socket = match listen_addr {
        SocketAddr::V4(_) => {
            let socket = socket2::Socket::new(
                socket2::Domain::ipv4(),
                socket2::Type::dgram(),
                Some(socket2::Protocol::udp()),
            )?;
            socket.set_multicast_loop_v4(false)?;
            socket.set_reuse_address(true)?;
            socket.set_reuse_port(true)?;
            socket.bind(
                &SocketAddr::new(IpAddr::V4(Ipv4Addr::UNSPECIFIED), listen_addr.port()).into(),
            )?;
            socket.join_multicast_v4(&MDNS_MCAST_V4, &Ipv4Addr::UNSPECIFIED)?;
            socket
        }
        SocketAddr::V6(_) => {
            let socket = socket2::Socket::new(
                socket2::Domain::ipv6(),
                socket2::Type::dgram(),
                Some(socket2::Protocol::udp()),
            )?;
            socket.set_only_v6(true)?;
            socket.set_multicast_loop_v6(false)?;
            socket.set_reuse_address(true)?;
            socket.set_reuse_port(true)?;
            socket.bind(
                &SocketAddr::new(IpAddr::V6(Ipv6Addr::UNSPECIFIED), listen_addr.port()).into(),
            )?;
            socket.join_multicast_v6(&MDNS_MCAST_V6, 0)?;
            socket
        }
    };
    Ok(socket.into_udp_socket().into())
}

fn make_sender_socket(interface_id: u32, addr: SocketAddr, ttl: u32) -> Result<UdpSocket> {
    let socket = match addr {
        SocketAddr::V4(ref saddr) => {
            let socket = socket2::Socket::new(
                socket2::Domain::ipv4(),
                socket2::Type::dgram(),
                Some(socket2::Protocol::udp()),
            )?;
            socket.set_ttl(ttl)?;
            socket.set_multicast_if_v4(&saddr.ip())?;
            socket.set_multicast_ttl_v4(ttl)?;
            socket.set_reuse_address(true)?;
            socket.set_reuse_port(true)?;
            socket.bind(&addr.into())?;
            socket.connect(&SocketAddrV4::new(MDNS_MCAST_V4, MDNS_PORT).into())?;
            socket
        }
        SocketAddr::V6(ref saddr) => {
            let socket = socket2::Socket::new(
                socket2::Domain::ipv6(),
                socket2::Type::dgram(),
                Some(socket2::Protocol::udp()),
            )?;
            socket.set_only_v6(true)?;
            socket.set_multicast_if_v6(interface_id)?;
            socket.set_unicast_hops_v6(ttl)?;
            socket.set_multicast_hops_v6(ttl)?;
            socket.set_reuse_address(true)?;
            socket.set_reuse_port(true)?;
            socket.bind(&addr.into())?;
            socket.connect(
                &SocketAddrV6::new(MDNS_MCAST_V6, MDNS_PORT, 0, saddr.scope_id()).into(),
            )?;
            socket
        }
    };
    Ok(socket.into_udp_socket().into())
}
