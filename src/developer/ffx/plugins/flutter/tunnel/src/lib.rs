// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Result},
    errors::FfxError,
    ffx_core::ffx_plugin,
    ffx_flutter_tunnel_args::TunnelCommand,
    fidl_fuchsia_developer_bridge::{DaemonProxy, TargetAddrInfo},
    fidl_fuchsia_net::{IpAddress, Ipv4Address, Ipv6Address},
    netext::scope_id_to_name,
    std::net::{IpAddr, Ipv4Addr, SocketAddr},
    std::process::Command,
    std::sync::atomic::{AtomicBool, Ordering},
    std::sync::Arc,
    std::time::Duration,
};

pub use ffx_emulator::portpicker::{pick_unused_port, Port};

static DEFAULT_SSH_OPTIONS: &'static [&str] = &[
    "-o",
    "CheckHostIP=no",
    "-o",
    "StrictHostKeyChecking=no",
    "-o",
    "UserKnownHostsFile=/dev/null",
    "-o",
    "ServerAliveInterval=1",
    "-o",
    "ServerAliveCountMax=10",
    "-o",
    "LogLevel=ERROR",
];

#[ffx_plugin("flutter.tunnel")]
pub async fn tunnel(daemon_proxy: DaemonProxy, cmd: TunnelCommand) -> Result<()> {
    tunnel_impl(daemon_proxy, cmd, &mut std::io::stdout()).await
}

pub async fn tunnel_impl<W: std::io::Write>(
    daemon_proxy: DaemonProxy,
    cmd: TunnelCommand,
    writer: &mut W,
) -> Result<()> {
    writeln!(writer, "vm_service_port: {}", cmd.vm_service_port)?;

    let ffx: ffx_lib_args::Ffx = argh::from_env();

    // Timeout value is 1.0 second for now.
    let timeout = Duration::from_secs(1);
    let target: Option<String> = ffx_config::get("target.default").await?;
    let res =
        daemon_proxy.get_ssh_address(target.as_deref(), timeout.as_nanos() as i64).await?.map_err(
            |e| FfxError::DaemonError { err: e, target, is_default_target: ffx.target.is_none() },
        )?;

    let (ip, scope, port) = match res {
        TargetAddrInfo::Ip(info) => {
            let ip = match info.ip {
                IpAddress::Ipv6(Ipv6Address { addr }) => IpAddr::from(addr),
                IpAddress::Ipv4(Ipv4Address { addr }) => IpAddr::from(addr),
            };
            (ip, info.scope_id, 0)
        }
        TargetAddrInfo::IpPort(info) => {
            let ip = match info.ip {
                IpAddress::Ipv6(Ipv6Address { addr }) => IpAddr::from(addr),
                IpAddress::Ipv4(Ipv4Address { addr }) => IpAddr::from(addr),
            };
            (ip, info.scope_id, info.port)
        }
    };
    writeln!(
        writer,
        "Target -> ip: {}, interface: %{}, port: {})",
        ip,
        scope_id_to_name(scope),
        port
    )?;

    let local_port = pick_unused_port().unwrap();
    let local_socket = SocketAddr::new(IpAddr::V4(Ipv4Addr::new(127, 0, 0, 1)), local_port);
    let dart_vm_socket = SocketAddr::new(ip, cmd.vm_service_port);

    let ssh_file_path: String =
        ffx_config::file("ssh.priv").await.context("getting ssh private key path")?;

    let mut command = Command::new("ssh");
    command.args(DEFAULT_SSH_OPTIONS);
    command.arg("-i").arg(ssh_file_path);
    command.args(vec![
        "-fnNT",
        "-L",
        format!("{}:{}:{}", local_socket.port(), local_socket.ip(), cmd.vm_service_port).as_str(),
        format!("{}%{}", dart_vm_socket.ip(), scope_id_to_name(scope)).as_str(),
    ]);
    let mut sp = command.spawn().expect("Cannot spawn child to execute ssh.");
    let _ = sp.wait();
    writeln!(
        writer,
        "Dart VM is listening on: http://{}:{}",
        local_socket.ip(),
        local_socket.port()
    )?;

    let term = Arc::new(AtomicBool::new(false));
    signal_hook::flag::register(signal_hook::consts::SIGINT, Arc::clone(&term))?;
    writeln!(writer, "Press Ctrl-C to kill ssh connection . . .")?;
    while !term.load(Ordering::Relaxed) {
        let _ = {};
    }
    Ok(())
}
