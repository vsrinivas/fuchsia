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
    anyhow::{Context as _, Result},
    async_std::sync::Mutex,
    async_std::task::JoinHandle,
    async_std::{net::UdpSocket, task},
    fuchsia_async::Timer,
    futures::FutureExt,
    packet::{InnerPacketBuilder, ParseBuffer},
    std::collections::HashMap,
    std::collections::HashSet,
    std::fmt::Write,
    std::net::{IpAddr, Ipv4Addr, Ipv6Addr, SocketAddr},
    std::sync::{Arc, Weak},
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
    // See fxbug.dev/62617#c10 for details. A macOS system can end up in
    // a situation where the default routes for protocols are on
    // non-functional interfaces, and under such conditions the wildcard
    // listen socket binds will fail. We will repeat attempting to bind
    // them, as newly added interfaces later may unstick the issue, if
    // they introduce new routes. These boolean flags are used to
    // suppress the production of a log output in every interface
    // iteration.
    // In order to manually reproduce these conditions on a macOS
    // system, open Network.prefpane, and for each connection in the
    // list select Advanced... > TCP/IP > Configure IPv6 > Link-local
    // only. Click apply, then restart the ffx daemon.
    let mut should_log_v4_listen_error = true;
    let mut should_log_v6_listen_error = true;

    let mut v4_listen_socket: Weak<UdpSocket> = Weak::new();
    let mut v6_listen_socket: Weak<UdpSocket> = Weak::new();

    loop {
        if v4_listen_socket.upgrade().is_none() {
            match make_listen_socket((MDNS_MCAST_V4, MDNS_PORT).into())
                .context("make_listen_socket for IPv4")
            {
                Ok(sock) => {
                    let sock = Arc::new(sock);
                    v4_listen_socket = Arc::downgrade(&sock);
                    task::spawn(recv_loop(sock, e.clone()));
                    should_log_v4_listen_error = true;
                }
                Err(err) => {
                    if should_log_v4_listen_error {
                        log::error!(
                            "unable to bind IPv4 listen socket: {}. Discovery may fail.",
                            err
                        );
                        should_log_v4_listen_error = false;
                    }
                }
            }
        }

        if v6_listen_socket.upgrade().is_none() {
            match make_listen_socket((MDNS_MCAST_V6, MDNS_PORT).into())
                .context("make_listen_socket for IPv6")
            {
                Ok(sock) => {
                    let sock = Arc::new(sock);
                    v6_listen_socket = Arc::downgrade(&sock);
                    task::spawn(recv_loop(sock, e.clone()));
                    should_log_v6_listen_error = true;
                }
                Err(err) => {
                    if should_log_v6_listen_error {
                        log::error!(
                            "unable to bind IPv6 listen socket: {}. Discovery may fail.",
                            err
                        );
                        should_log_v6_listen_error = false;
                    }
                }
            }
        }

        for iface in net::get_mcast_interfaces().unwrap_or(Vec::new()) {
            match iface.id() {
                Ok(id) => {
                    if let Some(sock) = v6_listen_socket.upgrade() {
                        let _ = sock.join_multicast_v6(&MDNS_MCAST_V6, id);
                    }
                }
                Err(err) => {
                    log::warn!("{}", err);
                }
            }

            for addr in iface.addrs.iter() {
                let mut addr = addr.clone();
                addr.set_port(MDNS_PORT);

                // TODO(raggi): remove duplicate joins, log unexpected errors
                if let SocketAddr::V4(addr) = addr {
                    if let Some(sock) = v4_listen_socket.upgrade() {
                        let _ = sock.join_multicast_v4(MDNS_MCAST_V4, *addr.ip());
                    }
                }

                if socket_tasks.lock().await.get(&addr).is_some() {
                    continue;
                }

                let sock = iface
                    .id()
                    .map(|id| match make_sender_socket(id, addr.clone(), ttl) {
                        Ok(sock) => Some(sock),
                        Err(err) => {
                            log::info!("mdns: failed to bind {}: {}", &addr, err);
                            None
                        }
                    })
                    .ok()
                    .flatten();

                if sock.is_some() {
                    socket_tasks.lock().await.insert(
                        addr.clone(),
                        task::spawn(query_recv_loop(
                            Arc::new(sock.unwrap()),
                            e.clone(),
                            query_interval,
                            socket_tasks.clone(),
                        )),
                    );
                }
            }
        }
        Timer::new(discovery_interval).await;
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
            Err(err) => {
                log::info!("listen socket recv error: {}, mdns listener closed", err);
                return;
            }
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
            log::trace!(
                "packet from {} ({}) on {}",
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
            true,
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
    let to_addr: SocketAddr = match sock.local_addr() {
        Ok(SocketAddr::V4(_)) => (MDNS_MCAST_V4, MDNS_PORT).into(),
        Ok(SocketAddr::V6(_)) => (MDNS_MCAST_V6, MDNS_PORT).into(),
        Err(err) => {
            log::info!("resolving local socket addr failed with: {}", err);
            return;
        }
    };

    loop {
        if let Err(err) = sock.send_to(&QUERY_BUF, to_addr).await {
            log::info!("mdns query failed: {}", err);
            return;
        }

        Timer::new(interval).await;
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

    let addr = match sock.local_addr() {
        Ok(addr) => addr,
        Err(err) => {
            log::error!("mdns: failed to shutdown: {:?}", err);
            return;
        }
    };

    log::info!("mdns: started query socket {}", &addr);

    let _ = futures::select!(
        _ = recv => {},
        _ = query => {},
    );

    drop(recv);
    drop(query);

    tasks.lock().await.remove(&addr).map(drop);
    log::info!("mdns: shut down query socket {}", &addr);
}

// TODO(fxbug.dev/44855): This needs to be e2e tested.
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
        Ok(events::TargetInfo {
            nodename,
            addresses: addrs.drain().collect(),
            ..Default::default()
        })
    }
}

fn make_listen_socket(listen_addr: SocketAddr) -> Result<UdpSocket> {
    let socket = match listen_addr {
        SocketAddr::V4(_) => {
            let socket = socket2::Socket::new(
                socket2::Domain::ipv4(),
                socket2::Type::dgram(),
                Some(socket2::Protocol::udp()),
            )
            .context("construct datagram socket")?;
            socket.set_multicast_loop_v4(false).context("set_multicast_loop_v4")?;
            socket.set_reuse_address(true).context("set_reuse_address")?;
            socket.set_reuse_port(true).context("set_reuse_port")?;
            socket
                .bind(
                    &SocketAddr::new(IpAddr::V4(Ipv4Addr::UNSPECIFIED), listen_addr.port()).into(),
                )
                .context("bind")?;
            socket
                .join_multicast_v4(&MDNS_MCAST_V4, &Ipv4Addr::UNSPECIFIED)
                .context("join_multicast_v4")?;
            socket
        }
        SocketAddr::V6(_) => {
            let socket = socket2::Socket::new(
                socket2::Domain::ipv6(),
                socket2::Type::dgram(),
                Some(socket2::Protocol::udp()),
            )
            .context("construct datagram socket")?;
            socket.set_only_v6(true).context("set_only_v6")?;
            socket.set_multicast_loop_v6(false).context("set_multicast_loop_v6")?;
            socket.set_reuse_address(true).context("set_reuse_address")?;
            socket.set_reuse_port(true).context("set_reuse_port")?;
            socket
                .bind(
                    &SocketAddr::new(IpAddr::V6(Ipv6Addr::UNSPECIFIED), listen_addr.port()).into(),
                )
                .context("bind")?;
            socket.join_multicast_v6(&MDNS_MCAST_V6, 0).context("join_multicast_v6")?;
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
            )
            .context("construct datagram socket")?;
            socket.set_ttl(ttl).context("set_ttl")?;
            socket.set_multicast_if_v4(&saddr.ip()).context("set_multicast_if_v4")?;
            socket.set_multicast_ttl_v4(ttl).context("set_multicast_ttl_v4")?;
            socket.set_reuse_address(true).context("set_reuse_address")?;
            socket.set_reuse_port(true).context("set_reuse_port")?;
            socket.bind(&addr.into()).context("bind")?;
            socket
        }
        SocketAddr::V6(ref _saddr) => {
            let socket = socket2::Socket::new(
                socket2::Domain::ipv6(),
                socket2::Type::dgram(),
                Some(socket2::Protocol::udp()),
            )
            .context("construct datagram socket")?;
            socket.set_only_v6(true).context("set_only_v6")?;
            socket.set_multicast_if_v6(interface_id).context("set_multicast_if_v6")?;
            socket.set_unicast_hops_v6(ttl).context("set_unicast_hops_v6")?;
            socket.set_multicast_hops_v6(ttl).context("set_multicast_hops_v6")?;
            socket.set_reuse_address(true).context("set_reuse_address")?;
            socket.set_reuse_port(true).context("set_reuse_port")?;
            socket.bind(&addr.into()).context("bind")?;
            socket
        }
    };
    Ok(socket.into_udp_socket().into())
}
