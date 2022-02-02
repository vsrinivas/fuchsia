// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    addr::TargetAddr,
    anyhow::{anyhow, bail, Context as _, Result},
    async_io::Async,
    async_net::UdpSocket,
    ffx_daemon_core::events,
    ffx_daemon_events::{DaemonEvent, TargetInfo, TryIntoTargetInfo, WireTrafficType},
    fuchsia_async::{Task, Timer},
    netext::{get_mcast_interfaces, IsLocalAddr},
    netsvc_proto::netboot::{
        NetbootPacket, NetbootPacketBuilder, Opcode, ADVERT_PORT, SERVER_PORT,
    },
    packet::{Buf, FragmentedBuffer, InnerPacketBuilder, ParseBuffer, Serializer},
    std::collections::HashSet,
    std::net::{IpAddr, Ipv6Addr, SocketAddr},
    std::num::NonZeroU16,
    std::sync::{Arc, Weak},
    std::time::Duration,
    zerocopy::ByteSlice,
};

const ZEDBOOT_MCAST_V6: Ipv6Addr = Ipv6Addr::new(0xff02, 0, 0, 0, 0, 0, 0, 1);
const ZEDBOOT_REDISCOVERY_INTERFACE_INTERVAL: Duration = Duration::from_secs(5);

pub async fn zedboot_discovery(e: events::Queue<DaemonEvent>) -> Result<Task<()>> {
    let port = port().await?;
    Ok(Task::local(interface_discovery(port, e, ZEDBOOT_REDISCOVERY_INTERFACE_INTERVAL)))
}

async fn port() -> Result<NonZeroU16> {
    ffx_config::get("discovery.zedboot.advert_port")
        .await
        .map(|port| {
            NonZeroU16::new(port).ok_or_else(|| anyhow::anyhow!("advert port must be nonzero"))
        })
        .unwrap_or(Ok(ADVERT_PORT))
}

// interface_discovery iterates over all multicast interfaces
pub async fn interface_discovery(
    port: NonZeroU16,
    e: events::Queue<DaemonEvent>,
    discovery_interval: Duration,
) {
    log::debug!("Starting Zedboot discovery");
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
    let mut should_log_v6_listen_error = true;
    let mut v6_listen_socket: Weak<UdpSocket> = Weak::new();
    loop {
        // fxb/90219 - disabled by default
        let is_enabled: bool = ffx_config::get("discovery.zedboot.enabled").await.unwrap_or(false);
        if is_enabled {
            if v6_listen_socket.upgrade().is_none() {
                match make_listen_socket((ZEDBOOT_MCAST_V6, port.get()).into())
                    .context("make_listen_socket for IPv6")
                {
                    Ok(sock) => {
                        let sock = Arc::new(sock);
                        v6_listen_socket = Arc::downgrade(&sock);
                        Task::local(recv_loop(sock.clone(), e.clone())).detach();
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

            for iface in get_mcast_interfaces().unwrap_or(Vec::new()) {
                match iface.id() {
                    Ok(id) => {
                        if let Some(sock) = v6_listen_socket.upgrade() {
                            let _ = sock.join_multicast_v6(&ZEDBOOT_MCAST_V6, id);
                        }
                    }
                    Err(err) => {
                        log::warn!("{}", err);
                    }
                }
            }
        }
        Timer::new(discovery_interval).await;
    }
}

// recv_loop reads packets from sock. If the packet is a Fuchsia zedboot packet, a
// corresponding zedboot event is published to the queue. All other packets are
// silently discarded.
async fn recv_loop(sock: Arc<UdpSocket>, e: events::Queue<DaemonEvent>) {
    loop {
        // fxb/90219 - disabled by default
        let is_enabled: bool = ffx_config::get("discovery.zedboot.enabled").await.unwrap_or(false);
        if !is_enabled {
            return;
        }

        let mut buf = &mut [0u8; 1500][..];
        let addr = match sock.recv_from(&mut buf).await {
            Ok((sz, addr)) => {
                buf = &mut buf[..sz];
                addr
            }
            Err(err) => {
                log::info!("listen socket recv error: {}, zedboot listener closed", err);
                return;
            }
        };

        let msg = match buf.parse::<NetbootPacket<_>>() {
            Ok(msg) => msg,
            Err(e) => {
                log::error!("failed to parse netboot packet {:?}", e);
                continue;
            }
        };

        // Note: important, otherwise non-local responders could add themselves.
        if !addr.ip().is_local_addr() {
            continue;
        }

        match ZedbootPacket(msg).try_into_target_info(addr) {
            Ok(info) => {
                log::trace!(
                    "zedboot packet from {} ({}) on {}",
                    addr,
                    info.nodename.clone().unwrap_or("<unknown>".to_string()),
                    sock.local_addr().unwrap()
                );
                e.push(DaemonEvent::WireTraffic(WireTrafficType::Zedboot(info))).unwrap_or_else(
                    |err| log::debug!("zedboot discovery was unable to publish: {}", err),
                );
            }
            Err(e) => log::error!("failed to extract zedboot target info {:?}", e),
        }
    }
}

#[derive(Debug, Eq, PartialEq)]
pub enum ZedbootConvertError {
    NodenameMissing,
}

/// A newtype for NetbootPacket so we can implement traits for it respecting the
/// orphan rules.
struct ZedbootPacket<B: ByteSlice>(NetbootPacket<B>);

impl<B: ByteSlice> TryIntoTargetInfo for ZedbootPacket<B> {
    type Error = ZedbootConvertError;

    fn try_into_target_info(self, src: SocketAddr) -> Result<TargetInfo, Self::Error> {
        let Self(packet) = self;
        let mut nodename = None;
        let msg = std::str::from_utf8(packet.payload().as_ref())
            .map_err(|_| ZedbootConvertError::NodenameMissing)?;
        for data in msg.split(';') {
            let entry: Vec<&str> = data.split('=').collect();
            if entry.len() == 2 {
                if entry[0] == "nodename" {
                    nodename.replace(entry[1].trim_matches(char::from(0)));
                    break;
                }
            }
        }
        let nodename = nodename.ok_or(ZedbootConvertError::NodenameMissing)?;
        let mut addrs: HashSet<TargetAddr> = [src.into()].iter().cloned().collect();
        Ok(TargetInfo {
            nodename: Some(nodename.to_string()),
            addresses: addrs.drain().collect(),
            ..Default::default()
        })
    }
}

fn make_listen_socket(listen_addr: SocketAddr) -> Result<UdpSocket> {
    let socket: std::net::UdpSocket = match listen_addr {
        SocketAddr::V4(_) => bail!("Zedboot only supports IPv6"),
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
            socket
        }
    }
    .into();
    Ok(Async::new(socket)?.into())
}

async fn make_sender_socket(addr: SocketAddr) -> Result<UdpSocket> {
    let socket: std::net::UdpSocket = {
        let socket = socket2::Socket::new(
            socket2::Domain::IPV6,
            socket2::Type::DGRAM,
            Some(socket2::Protocol::UDP),
        )
        .context("construct datagram socket")?;
        socket.set_only_v6(true).context("set_only_v6")?;
        socket.set_reuse_address(true).context("set_reuse_address")?;
        socket.set_reuse_port(true).context("set_reuse_port")?;
        socket
    }
    .into();
    let result: UdpSocket = Async::new(socket)?.into();
    result.connect(addr).await.context("connect to remote address")?;
    Ok(result)
}

async fn send(opcode: Opcode, body: &str, to_addr: TargetAddr) -> Result<()> {
    const BUFFER_SIZE: usize = 512;
    const COOKIE: u32 = 1;
    const ARG: u32 = 0;
    let msg = (body.as_bytes())
        .into_serializer_with(Buf::new([0u8; BUFFER_SIZE], ..))
        .serialize_no_alloc(NetbootPacketBuilder::new(opcode.into(), COOKIE, ARG))
        .expect("failed to serialize");
    let mut to_sock: SocketAddr = to_addr.into();
    to_sock.set_port(SERVER_PORT.get());
    log::info!("Sending {:?} {} to {}", opcode, body, to_sock);
    let sock = make_sender_socket(to_sock).await?;
    sock.send(msg.as_ref()).await.map_err(|e| anyhow!("Sending error: {}", e)).and_then(|sent| {
        if sent == msg.len() {
            Ok(())
        } else {
            Err(anyhow::anyhow!("partial send {} of {} bytes", sent, msg.len()))
        }
    })
}

pub async fn reboot(to_addr: TargetAddr) -> Result<()> {
    send(Opcode::Reboot, "", to_addr).await
}

pub async fn reboot_to_bootloader(to_addr: TargetAddr) -> Result<()> {
    send(Opcode::ShellCmd, "dm reboot-bootloader\0", to_addr).await
}

pub async fn reboot_to_recovery(to_addr: TargetAddr) -> Result<()> {
    send(Opcode::ShellCmd, "dm reboot-recovery\0", to_addr).await
}
