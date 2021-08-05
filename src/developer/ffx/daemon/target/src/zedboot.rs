// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    addr::TargetAddr,
    anyhow::{anyhow, bail, Context as _, Result},
    async_io::Async,
    async_net::UdpSocket,
    byteorder::{ByteOrder, LittleEndian},
    ffx_daemon_core::events,
    ffx_daemon_events::{DaemonEvent, TargetInfo, TryIntoTargetInfo, WireTrafficType},
    fuchsia_async::{Task, Timer},
    netext::{get_mcast_interfaces, IsLocalAddr},
    std::collections::HashSet,
    std::net::{IpAddr, Ipv6Addr, SocketAddr},
    std::sync::{Arc, Weak},
    std::time::Duration,
    zerocopy::{AsBytes, ByteSlice, FromBytes, LayoutVerified, Unaligned, U32},
};

const ZEDBOOT_MCAST_V6: Ipv6Addr = Ipv6Addr::new(0xff02, 0, 0, 0, 0, 0, 0, 1);
const ZEDBOOT_CMD_SERVER_PORT: u16 = 33330;
const ZEDBOOT_ADVERT_PORT: u16 = 33331;
const ZEDBOOT_MAGIC: u32 = 0xAA774217;
const ZEDBOOT_REDISCOVERY_INTERFACE_INTERVAL: Duration = Duration::from_secs(5);

// Commands
const ZEDBOOT_REBOOT: u32 = 12;
const ZEDBOOT_SHELL_CMD: u32 = 6;

const ZEDBOOT_REBOOT_BOOTLOADER_CMD: &str = "dm reboot-bootloader\0";
const ZEDBOOT_REBOOT_BOOTLOADER_CMD_LEN: usize = 37;
const ZEDBOOT_REBOOT_RECOVERY_CMD: &str = "dm reboot-recovery\0";
const ZEDBOOT_REBOOT_RECOVERY_CMD_LEN: usize = 35;

#[derive(FromBytes, AsBytes, Unaligned)]
#[repr(C)]
struct ZedbootHeader {
    magic: U32<LittleEndian>,
    cookie: U32<LittleEndian>,
    cmd: U32<LittleEndian>,
    arg: U32<LittleEndian>,
}

impl ZedbootHeader {
    fn new(cookie: u32, cmd: u32, arg: u32) -> Self {
        Self {
            magic: U32::<LittleEndian>::from(ZEDBOOT_MAGIC),
            cookie: U32::<LittleEndian>::from(cookie),
            cmd: U32::<LittleEndian>::from(cmd),
            arg: U32::<LittleEndian>::from(arg),
        }
    }
}

struct ZedbootPacket<B: ByteSlice> {
    header: LayoutVerified<B, ZedbootHeader>,
    body: B,
}

impl<B: ByteSlice> ZedbootPacket<B> {
    fn parse(bytes: B) -> Option<ZedbootPacket<B>> {
        let (header, body) = LayoutVerified::new_from_prefix(bytes)?;
        Some(Self { header, body })
    }

    fn magic(&self) -> u32 {
        self.header.magic.get()
    }
}

pub fn zedboot_discovery(e: events::Queue<DaemonEvent>) -> Result<Task<()>> {
    Ok(Task::local(interface_discovery(e, ZEDBOOT_REDISCOVERY_INTERFACE_INTERVAL)))
}

async fn port() -> u16 {
    ffx_config::get("discovery.zedboot.advert_port").await.unwrap_or(ZEDBOOT_ADVERT_PORT)
}

// interface_discovery iterates over all multicast interfaces
pub async fn interface_discovery(e: events::Queue<DaemonEvent>, discovery_interval: Duration) {
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
        if v6_listen_socket.upgrade().is_none() {
            match make_listen_socket((ZEDBOOT_MCAST_V6, port().await).into())
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
        Timer::new(discovery_interval).await;
    }
}

// recv_loop reads packets from sock. If the packet is a Fuchsia zedboot packet, a
// corresponding zedboot event is published to the queue. All other packets are
// silently discarded.
async fn recv_loop(sock: Arc<UdpSocket>, e: events::Queue<DaemonEvent>) {
    loop {
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

        let msg = match ZedbootPacket::parse(buf) {
            Some(msg) => msg,
            _ => continue,
        };

        if msg.magic() != ZEDBOOT_MAGIC {
            continue;
        }

        // Note: important, otherwise non-local responders could add themselves.
        if !addr.ip().is_local_addr() {
            continue;
        }

        if let Ok(info) = msg.try_into_target_info(addr) {
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
    }
}

#[derive(Debug, Eq, PartialEq)]
pub enum ZedbootConvertError {
    NodenameMissing,
}

impl<B: ByteSlice> TryIntoTargetInfo for ZedbootPacket<B> {
    type Error = ZedbootConvertError;

    fn try_into_target_info(self, src: SocketAddr) -> Result<TargetInfo, Self::Error> {
        let mut nodename = None;
        let msg = String::from_utf8(self.body.to_vec())
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

pub async fn reboot(to_addr: TargetAddr) -> Result<()> {
    let zed = ZedbootHeader::new(1, ZEDBOOT_REBOOT, 0);
    let mut to_sock: SocketAddr = to_addr.into();
    to_sock.set_port(ZEDBOOT_CMD_SERVER_PORT);
    log::info!("Sending Zedboot reboot to {}", to_sock);
    let sock = make_sender_socket(to_sock).await?;
    sock.send(zed.as_bytes()).await.map_err(|e| anyhow!("Sending error: {}", e)).map(|_| ())
}

pub async fn reboot_to_bootloader(to_addr: TargetAddr) -> Result<()> {
    let mut buf = [0u8; ZEDBOOT_REBOOT_BOOTLOADER_CMD_LEN];
    LittleEndian::write_u32(&mut buf[..4], ZEDBOOT_MAGIC);
    LittleEndian::write_u32(&mut buf[4..8], 1);
    LittleEndian::write_u32(&mut buf[8..12], ZEDBOOT_SHELL_CMD);
    LittleEndian::write_u32(&mut buf[12..16], 0);
    buf[16..].copy_from_slice(&ZEDBOOT_REBOOT_BOOTLOADER_CMD.as_bytes()[..]);
    let mut to_sock: SocketAddr = to_addr.into();
    to_sock.set_port(ZEDBOOT_CMD_SERVER_PORT);
    log::info!("Sending Zedboot reboot to {}", to_sock);
    let sock = make_sender_socket(to_sock).await?;
    sock.send(&buf).await.map_err(|e| anyhow!("Sending error: {}", e)).map(|_| ())
}

pub async fn reboot_to_recovery(to_addr: TargetAddr) -> Result<()> {
    let mut buf = [0u8; ZEDBOOT_REBOOT_RECOVERY_CMD_LEN];
    LittleEndian::write_u32(&mut buf[..4], ZEDBOOT_MAGIC);
    LittleEndian::write_u32(&mut buf[4..8], 1);
    LittleEndian::write_u32(&mut buf[8..12], ZEDBOOT_SHELL_CMD);
    LittleEndian::write_u32(&mut buf[12..16], 0);
    buf[16..].copy_from_slice(&ZEDBOOT_REBOOT_RECOVERY_CMD.as_bytes()[..]);
    let mut to_sock: SocketAddr = to_addr.into();
    to_sock.set_port(ZEDBOOT_CMD_SERVER_PORT);
    log::info!("Sending Zedboot reboot to {}", to_sock);
    let sock = make_sender_socket(to_sock).await?;
    sock.send(&buf).await.map_err(|e| anyhow!("Sending error: {}", e)).map(|_| ())
}
