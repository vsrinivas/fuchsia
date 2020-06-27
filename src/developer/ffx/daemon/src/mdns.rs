// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use crate::discovery::{TargetFinder, TargetFinderConfig};
use crate::events;
use std::io;

#[cfg(target_os = "linux")]
use {
    self::linux::MdnsTargetFinderLinuxExt, crate::events::TryIntoTargetInfo, crate::target::*,
    std::net::UdpSocket,
};

#[cfg(target_os = "linux")]
#[derive(Debug)]
pub struct MdnsTargetFinder {
    listener_sockets: Vec<UdpSocket>,
    // TODO(awdavies): Might need to periodically check to see if a new iface
    // has come up, like in the event of a TAP interface, for example.
    sender_sockets: Vec<UdpSocket>,
    config: TargetFinderConfig,
}

#[cfg(not(target_os = "linux"))]
#[derive(Debug)]
pub struct MdnsTargetFinder {}

impl TargetFinder for MdnsTargetFinder {
    #[cfg(target_os = "linux")]
    fn new(config: &TargetFinderConfig) -> io::Result<Self> {
        Self::linux_new(config)
    }

    #[cfg(target_os = "linux")]
    fn start(&self, e: events::Queue<events::DaemonEvent>) -> io::Result<()> {
        self.linux_start(e)
    }

    //// Non-linux trait impl

    #[cfg(not(target_os = "linux"))]
    fn new(_config: &TargetFinderConfig) -> io::Result<Self> {
        unimplemented!()
    }

    #[cfg(not(target_os = "linux"))]
    fn start(&self, _e: events::Queue<events::DaemonEvent>) -> io::Result<()> {
        unimplemented!()
    }
}

// TODO(fxb/44855): This needs to be e2e tested.
#[cfg(target_os = "linux")]
mod linux {
    use super::*;
    use std::collections::HashSet;
    use std::fmt::Write;
    use std::net::{IpAddr, Ipv4Addr, Ipv6Addr, SocketAddr, SocketAddrV6, UdpSocket};
    use std::thread;

    use crate::net;
    use ::mdns::protocol as dns;
    use packet::{InnerPacketBuilder, ParseBuffer, Serializer};
    use zerocopy::ByteSlice;

    const MDNS_MCAST_V4: Ipv4Addr = Ipv4Addr::new(224, 0, 0, 251);
    const MDNS_MCAST_V6: Ipv6Addr = Ipv6Addr::new(0xff02, 0, 0, 0, 0, 0, 0, 0x00fb);
    const MDNS_PORT: u16 = 5353;

    #[derive(Debug, Eq, PartialEq)]
    pub enum MdnsConvertError {
        NodenameMissing,
    }

    pub trait MdnsTargetFinderLinuxExt: TargetFinder {
        fn linux_new(c: &TargetFinderConfig) -> io::Result<Self>;

        fn linux_start(&self, e: events::Queue<events::DaemonEvent>) -> io::Result<()>;
    }

    impl MdnsTargetFinderLinuxExt for MdnsTargetFinder {
        fn linux_new(config: &TargetFinderConfig) -> io::Result<Self> {
            let mut listener_sockets: Vec<UdpSocket> = Vec::new();
            listener_sockets.push(make_listen_socket((MDNS_MCAST_V4, MDNS_PORT).into())?);
            listener_sockets.push(make_listen_socket((MDNS_MCAST_V6, MDNS_PORT).into())?);
            let mut sender_sockets: Vec<UdpSocket> = Vec::new();
            for iface in unsafe { net::linux::get_mcast_interfaces()? } {
                for addr in iface.addrs.iter() {
                    let (src, dst): (SocketAddr, SocketAddr) = match addr {
                        IpAddr::V4(a) => {
                            ((*a, MDNS_PORT).into(), (MDNS_MCAST_V4, MDNS_PORT).into())
                        }
                        // This needs to include the interface or else bind() will crash.
                        // Flow label is zero as this is just a UDP datagram.
                        IpAddr::V6(a) => (
                            SocketAddrV6::new(*a, MDNS_PORT, 0, iface.id).into(),
                            (MDNS_MCAST_V6, MDNS_PORT).into(),
                        ),
                    };
                    sender_sockets.push(make_sender_socket(src, dst, config.mdns_ttl)?);
                }
            }

            Ok(Self { listener_sockets, sender_sockets, config: config.clone() })
        }

        fn linux_start(&self, e: events::Queue<events::DaemonEvent>) -> io::Result<()> {
            self.start_listeners(e)?;
            self.start_query_loop()?;

            Ok(())
        }
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

    impl MdnsTargetFinder {
        fn start_listeners(&self, e: events::Queue<events::DaemonEvent>) -> io::Result<()> {
            // Listen on all sockets in the event that unicast packets are returned.
            for l in self.sender_sockets.iter().chain(self.listener_sockets.iter()) {
                let sock = l.try_clone()?;
                let e = e.clone();
                thread::spawn(move || {
                    let mut buf = [0; 1500];
                    loop {
                        let (amt, src) = sock.recv_from(&mut buf).unwrap();
                        let mut buf = &mut buf[..amt];
                        match buf.parse::<dns::Message<_>>() {
                            Ok(m) => {
                                if is_fuchsia_response(&m) {
                                    match m.try_into_target_info(src) {
                                        Ok(info) => futures::executor::block_on(e.push(
                                            events::DaemonEvent::WireTraffic(
                                                events::WireTrafficType::Mdns(info),
                                            ),
                                        ))
                                        .unwrap(),
                                        _ => (),
                                    }
                                }
                            }
                            _ => (),
                        }
                    }
                });
            }

            Ok(())
        }

        fn start_query_loop(&self) -> io::Result<()> {
            for s in self.sender_sockets.iter() {
                let sock = s.try_clone()?;
                let config = self.config.clone();
                thread::spawn(move || {
                    // MCast question.
                    let question = dns::QuestionBuilder::new(
                        dns::DomainBuilder::from_str("_fuchsia._udp.local").unwrap(),
                        dns::Type::Ptr,
                        dns::Class::In,
                        false,
                    );
                    let mut message = dns::MessageBuilder::new(0, true);
                    message.add_question(question);
                    let message_bytes = message
                        .into_serializer()
                        .serialize_vec_outer()
                        .unwrap_or_else(|_| panic!("Failed to serialize msg"))
                        .unwrap_b();
                    loop {
                        sock.send(&message_bytes.as_ref()).unwrap();
                        std::thread::sleep(config.broadcast_interval);
                    }
                });
            }

            Ok(())
        }
    }

    fn make_listen_socket(s: SocketAddr) -> io::Result<UdpSocket> {
        Ok(match s {
            SocketAddr::V4(addr) => {
                let socket = socket2::Socket::new(
                    socket2::Domain::ipv4(),
                    socket2::Type::dgram(),
                    Some(socket2::Protocol::udp()),
                )?;
                let () = socket.set_reuse_address(true)?;
                let () = socket.set_reuse_port(true)?;
                let () = socket
                    .bind(&SocketAddr::new(IpAddr::V4(Ipv4Addr::UNSPECIFIED), s.port()).into())?;
                let () = socket.set_multicast_loop_v4(false)?;
                let () = socket.join_multicast_v4(&addr.ip(), &Ipv4Addr::UNSPECIFIED)?;

                socket
            }
            SocketAddr::V6(addr) => {
                let socket = socket2::Socket::new(
                    socket2::Domain::ipv6(),
                    socket2::Type::dgram(),
                    Some(socket2::Protocol::udp()),
                )?;
                let () = socket.set_only_v6(true)?;
                let () = socket.set_reuse_address(true)?;
                let () = socket.set_reuse_port(true)?;
                let () = socket
                    .bind(&SocketAddr::new(IpAddr::V6(Ipv6Addr::UNSPECIFIED), s.port()).into())?;
                let () = socket.set_multicast_loop_v6(false)?;
                // Presuming that this is a multicast address, we need to join
                // on every interface.
                for iface in unsafe { net::linux::get_mcast_interfaces()? } {
                    // If the iface doesn't have a link local IPv6 address,
                    // this will panic.
                    if iface.addrs.iter().any(|addr| addr.is_ipv6()) {
                        let () = socket.join_multicast_v6(&addr.ip(), iface.id)?;
                    }
                }

                socket
            }
        }
        .into_udp_socket())
    }

    fn make_sender_socket(s: SocketAddr, d: SocketAddr, ttl: u8) -> io::Result<UdpSocket> {
        let socket = match s {
            SocketAddr::V4(saddr) => {
                let socket = socket2::Socket::new(
                    socket2::Domain::ipv4(),
                    socket2::Type::dgram(),
                    Some(socket2::Protocol::udp()),
                )?;
                let () = socket.set_reuse_address(true)?;
                let () = socket.set_reuse_port(true)?;
                let () = socket.bind(&saddr.into())?;
                let () = socket.set_multicast_ttl_v4(ttl.into())?;
                socket
            }
            SocketAddr::V6(saddr) => {
                let socket = socket2::Socket::new(
                    socket2::Domain::ipv6(),
                    socket2::Type::dgram(),
                    Some(socket2::Protocol::udp()),
                )?;
                let () = socket.set_reuse_address(true)?;
                let () = socket.set_reuse_port(true)?;
                let () = socket.bind(&saddr.into())?;
                let () = socket.set_multicast_hops_v6(ttl.into())?;
                socket
            }
        };
        let () = socket.connect(&d.into())?;
        Ok(socket.into_udp_socket())
    }
}
