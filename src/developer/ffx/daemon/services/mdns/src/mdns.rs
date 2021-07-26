use std::os::unix::prelude::AsRawFd;

// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::MdnsServiceInner,
    anyhow::{Context as _, Result},
    async_io::Async,
    async_lock::Mutex,
    async_net::UdpSocket,
    fidl_fuchsia_developer_bridge as bridge,
    fidl_fuchsia_net::{IpAddress, Ipv4Address, Ipv6Address},
    fuchsia_async::{Task, Timer},
    futures::FutureExt,
    mdns::protocol as dns,
    netext::{get_mcast_interfaces, IsLocalAddr},
    packet::{InnerPacketBuilder, ParseBuffer},
    std::collections::HashMap,
    std::collections::HashSet,
    std::fmt::Write,
    std::net::{IpAddr, Ipv4Addr, Ipv6Addr, SocketAddr},
    std::rc::{Rc, Weak},
    std::time::Duration,
    zerocopy::ByteSlice,
};

const MDNS_MCAST_V4: Ipv4Addr = Ipv4Addr::new(224, 0, 0, 251);
const MDNS_MCAST_V6: Ipv6Addr = Ipv6Addr::new(0xff02, 0, 0, 0, 0, 0, 0, 0x00fb);

#[cfg(not(test))]
const MDNS_PORT: u16 = 5353;
#[cfg(test)]
const MDNS_PORT: u16 = 0;

pub(crate) struct DiscoveryConfig {
    pub socket_tasks: Rc<Mutex<HashMap<IpAddr, Task<()>>>>,
    pub mdns_service: Weak<MdnsServiceInner>,
    pub discovery_interval: Duration,
    pub query_interval: Duration,
    pub ttl: u32,
}

async fn propagate_bind_event(sock: &UdpSocket, svc: &Weak<MdnsServiceInner>) -> u16 {
    let port = match sock.local_addr().unwrap() {
        SocketAddr::V4(s) => s.port(),
        SocketAddr::V6(s) => s.port(),
    };
    if let Some(svc) = svc.upgrade() {
        svc.publish_event(bridge::MdnsEventType::SocketBound(bridge::MdnsBindEvent {
            port: Some(port),
            ..bridge::MdnsBindEvent::EMPTY
        }))
        .await;
    }
    port
}

// discovery_loop iterates over all multicast interfaces and adds them to
// the socket_tasks if there is not already a task for that interface.
pub(crate) async fn discovery_loop(config: DiscoveryConfig) {
    let DiscoveryConfig { socket_tasks, mdns_service, discovery_interval, query_interval, ttl } =
        config;
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
                    // TODO(awdavies): Networking tests appear to fail when
                    // using IPv6. Only propagates the port binding event for
                    // IPv4.
                    let _ = propagate_bind_event(&sock, &mdns_service).await;
                    let sock = Rc::new(sock);
                    v4_listen_socket = Rc::downgrade(&sock);
                    Task::local(recv_loop(sock, mdns_service.clone())).detach();
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
                    let sock = Rc::new(sock);
                    v6_listen_socket = Rc::downgrade(&sock);
                    Task::local(recv_loop(sock, mdns_service.clone())).detach();
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

        // As some operating systems will not error sendmsg/recvmsg for UDP
        // sockets bound to addresses that no longer exist, they must be removed
        // by ensuring that they still exist, otherwise we may be sending out
        // unanswerable queries.
        let mut to_delete = HashSet::<IpAddr>::new();
        for ip in socket_tasks.lock().await.keys() {
            to_delete.insert(ip.clone());
        }

        for iface in get_mcast_interfaces().unwrap_or(Vec::new()) {
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
                to_delete.remove(&addr.ip());

                let mut addr = addr.clone();
                addr.set_port(0);

                // TODO(raggi): remove duplicate joins, log unexpected errors
                if let SocketAddr::V4(addr) = addr {
                    if let Some(sock) = v4_listen_socket.upgrade() {
                        let _ = sock.join_multicast_v4(MDNS_MCAST_V4, *addr.ip());
                    }
                }

                if socket_tasks.lock().await.get(&addr.ip()).is_some() {
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
                        addr.ip().clone(),
                        Task::local(query_recv_loop(
                            Rc::new(sock.unwrap()),
                            mdns_service.clone(),
                            query_interval,
                            socket_tasks.clone(),
                        )),
                    );
                }
            }
        }

        // Drop tasks for IP addresses no longer found on the system.
        {
            let mut tasks = socket_tasks.lock().await;
            for ip in to_delete {
                if let Some(handle) = tasks.remove(&ip) {
                    handle.cancel().await;
                }
            }
        }

        Timer::new(discovery_interval).await;
    }
}

fn make_target<B: ByteSlice + Clone>(
    src: SocketAddr,
    msg: dns::Message<B>,
) -> Option<(bridge::Target, u32)> {
    let mut nodename = String::new();
    let mut ttl = 0u32;
    let src = bridge::TargetAddrInfo::Ip(bridge::TargetIp {
        ip: match &src {
            SocketAddr::V6(s) => IpAddress::Ipv6(Ipv6Address { addr: s.ip().octets().into() }),
            SocketAddr::V4(s) => IpAddress::Ipv4(Ipv4Address { addr: s.ip().octets().into() }),
        },
        scope_id: if let SocketAddr::V6(s) = &src { s.scope_id() } else { 0 },
    });
    let is_fastboot = is_fastboot_response(&msg);
    for record in msg.additional.iter() {
        if record.rtype != dns::Type::A && record.rtype != dns::Type::Aaaa {
            continue;
        }
        if nodename.len() == 0 {
            write!(nodename, "{}", record.domain).unwrap();
            nodename = nodename.trim_end_matches(".local").into();
        }
        if ttl == 0 {
            ttl = record.ttl;
        }
        // The records here also have the IP addresses of
        // the machine, however these could be different if behind a NAT
        // (as with QEMU). Later it might be useful to store them in the
        // Target struct.
    }
    if nodename.len() == 0 || ttl == 0 {
        return None;
    }
    Some((
        bridge::Target {
            nodename: Some(nodename),
            addresses: Some(vec![src]),
            target_state: if is_fastboot { Some(bridge::TargetState::Fastboot) } else { None },
            ..bridge::Target::EMPTY
        },
        ttl,
    ))
}

// recv_loop reads packets from sock. If the packet is a Fuchsia mdns packet, a
// corresponding mdns event is published to the queue. All other packets are
// silently discarded.
async fn recv_loop(sock: Rc<UdpSocket>, mdns_service: Weak<MdnsServiceInner>) {
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
            Err(e) => {
                log::warn!(
                    "unable to parse message received on {} from {}: {:?}",
                    sock.local_addr()
                        .map(|s| format!("{}", s))
                        .unwrap_or(format!("fd:{}", sock.as_raw_fd())),
                    addr,
                    e
                );
                continue;
            }
        };

        // Note: important, otherwise non-local responders could add themselves.
        if !addr.ip().is_local_addr() {
            continue;
        }

        if !is_fuchsia_response(&msg) && !is_fastboot_response(&msg) {
            continue;
        }

        if let Some(mdns_service) = mdns_service.upgrade() {
            if let Some((t, ttl)) = make_target(addr, msg) {
                log::trace!(
                    "packet from {} ({}) on {}",
                    addr,
                    t.nodename.as_ref().unwrap_or(&"<unknown>".to_string()),
                    sock.local_addr().unwrap()
                );
                mdns_service.handle_target(t, ttl).await;
            }
        } else {
            return;
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
async fn query_loop(sock: Rc<UdpSocket>, interval: Duration) {
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
            log::info!(
                "mdns query failed from {}: {}",
                sock.local_addr().map(|a| a.to_string()).unwrap_or("unknown".to_string()),
                err
            );
            return;
        }

        Timer::new(interval).await;
    }
}

// sock is dispatched with a recv_loop, as well as broadcasting an
// mdns query to discover Fuchsia devices every interval.
async fn query_recv_loop(
    sock: Rc<UdpSocket>,
    mdns_service: Weak<MdnsServiceInner>,
    interval: Duration,
    tasks: Rc<Mutex<HashMap<IpAddr, Task<()>>>>,
) {
    let mut recv = recv_loop(sock.clone(), mdns_service).boxed_local().fuse();
    let mut query = query_loop(sock.clone(), interval).boxed_local().fuse();

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

    tasks.lock().await.remove(&addr.ip()).map(drop);
    log::info!("mdns: shut down query socket {}", &addr);
}

fn is_fuchsia_response<B: zerocopy::ByteSlice + Clone>(m: &dns::Message<B>) -> bool {
    m.answers.len() >= 1 && m.answers[0].domain == "_fuchsia._udp.local"
}

fn is_fastboot_response<B: zerocopy::ByteSlice + Clone>(m: &dns::Message<B>) -> bool {
    m.answers.len() >= 1 && m.answers[0].domain == "_fastboot._udp.local"
}

fn make_listen_socket(listen_addr: SocketAddr) -> Result<UdpSocket> {
    let socket: std::net::UdpSocket = match listen_addr {
        SocketAddr::V4(_) => {
            let socket = socket2::Socket::new(
                socket2::Domain::IPV4,
                socket2::Type::DGRAM,
                Some(socket2::Protocol::UDP),
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
                socket2::Domain::IPV6,
                socket2::Type::DGRAM,
                Some(socket2::Protocol::UDP),
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
            // For some reason this often fails to bind on Mac, so avoid it and
            // use the interface binding loop to get multicast group joining to
            // work.
            #[cfg(not(target_os = "macos"))]
            socket.join_multicast_v6(&MDNS_MCAST_V6, 0).context("join_multicast_v6")?;
            socket
        }
    }
    .into();
    Ok(Async::new(socket)?.into())
}

fn make_sender_socket(interface_id: u32, addr: SocketAddr, ttl: u32) -> Result<UdpSocket> {
    let socket: std::net::UdpSocket = match addr {
        SocketAddr::V4(ref saddr) => {
            let socket = socket2::Socket::new(
                socket2::Domain::IPV4,
                socket2::Type::DGRAM,
                Some(socket2::Protocol::UDP),
            )
            .context("construct datagram socket")?;
            socket.set_ttl(ttl).context("set_ttl")?;
            socket.set_multicast_if_v4(&saddr.ip()).context("set_multicast_if_v4")?;
            socket.set_multicast_ttl_v4(ttl).context("set_multicast_ttl_v4")?;
            socket.bind(&addr.into()).context("bind")?;
            socket
        }
        SocketAddr::V6(ref _saddr) => {
            let socket = socket2::Socket::new(
                socket2::Domain::IPV6,
                socket2::Type::DGRAM,
                Some(socket2::Protocol::UDP),
            )
            .context("construct datagram socket")?;
            socket.set_only_v6(true).context("set_only_v6")?;
            socket.set_multicast_if_v6(interface_id).context("set_multicast_if_v6")?;
            socket.set_unicast_hops_v6(ttl).context("set_unicast_hops_v6")?;
            socket.set_multicast_hops_v6(ttl).context("set_multicast_hops_v6")?;
            socket.bind(&addr.into()).context("bind")?;
            socket
        }
    }
    .into();
    Ok(Async::new(socket)?.into())
}

#[cfg(test)]
mod tests {

    use super::*;
    use ::mdns::protocol::{Class, DomainBuilder, Message, MessageBuilder, RecordBuilder, Type};
    use packet::{InnerPacketBuilder, ParseBuffer, Serializer};

    #[test]
    fn test_make_target() {
        let nodename = DomainBuilder::from_str("foo._fuchsia._udp.local").unwrap();
        let record = RecordBuilder::new(nodename, Type::A, Class::Any, true, 4500, &[8, 8, 8, 8]);
        let mut message = MessageBuilder::new(0, true);
        message.add_additional(record);
        let mut msg_bytes = message
            .into_serializer()
            .serialize_vec_outer()
            .unwrap_or_else(|_| panic!("failed to serialize"));
        let parsed = msg_bytes.parse::<Message<_>>().expect("failed to parse");
        let addr: SocketAddr = (MDNS_MCAST_V4, 12).into();
        let (t, ttl) = make_target(addr.clone(), parsed).unwrap();
        assert_eq!(ttl, 4500);
        assert_eq!(
            t.addresses.as_ref().unwrap()[0],
            bridge::TargetAddrInfo::Ip(bridge::TargetIp {
                ip: IpAddress::Ipv4(Ipv4Address { addr: MDNS_MCAST_V4.octets().into() }),
                scope_id: 0
            })
        );
        assert_eq!(t.nodename.unwrap(), "foo._fuchsia._udp");
    }

    #[test]
    fn test_make_target_no_valid_record() {
        let nodename = DomainBuilder::from_str("foo._fuchsia._udp.local").unwrap();
        let record = RecordBuilder::new(
            nodename,
            Type::Ptr,
            Class::Any,
            true,
            4500,
            &[0x03, 'f' as u8, 'o' as u8, 'o' as u8, 0],
        );
        let mut message = MessageBuilder::new(0, true);
        message.add_additional(record);
        let mut msg_bytes = message
            .into_serializer()
            .serialize_vec_outer()
            .unwrap_or_else(|_| panic!("failed to serialize"));
        let parsed = msg_bytes.parse::<Message<_>>().expect("failed to parse");
        let addr: SocketAddr = (MDNS_MCAST_V4, 12).into();
        assert!(make_target(addr.clone(), parsed).is_none());
    }
}
