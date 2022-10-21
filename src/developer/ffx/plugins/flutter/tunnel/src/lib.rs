// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Result},
    diagnostics_data::Inspect,
    errors::{ffx_error, FfxError},
    ffx_core::ffx_plugin,
    ffx_flutter_tunnel_args::TunnelCommand,
    ffx_flutter_tunnel_ctrlc::wait_for_kill,
    ffx_inspect_common::DiagnosticsBridgeProvider,
    fidl_fuchsia_developer_ffx::{DaemonError, TargetAddrInfo, TargetProxy},
    fidl_fuchsia_developer_remotecontrol::{RemoteControlProxy, RemoteDiagnosticsBridgeProxy},
    fidl_fuchsia_net::{IpAddress, Ipv4Address, Ipv6Address},
    netext::scope_id_to_name,
    std::net::{IpAddr, Ipv4Addr, SocketAddr},
    std::process::Command,
    std::time::Duration,
    timeout::timeout,
};

pub use port_picker::{pick_unused_port, Port};

static DEFAULT_SSH_OPTIONS: &'static [&str] = &[
    "-F",
    "none", // Ignore user and system configuration files.
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

#[ffx_plugin(
    "flutter.tunnel",
    RemoteDiagnosticsBridgeProxy = "core/remote-diagnostics-bridge:expose:fuchsia.developer.remotecontrol.RemoteDiagnosticsBridge"
)]
pub async fn tunnel(
    target_proxy: TargetProxy,
    rcs_proxy: RemoteControlProxy,
    diagnostics_proxy: RemoteDiagnosticsBridgeProxy,
    cmd: TunnelCommand,
) -> Result<()> {
    tunnel_impl(target_proxy, rcs_proxy, diagnostics_proxy, cmd, &mut std::io::stdout()).await
}

pub async fn tunnel_impl<W: std::io::Write>(
    target_proxy: TargetProxy,
    rcs_proxy: RemoteControlProxy,
    diagnostics_proxy: RemoteDiagnosticsBridgeProxy,
    _cmd: TunnelCommand,
    writer: &mut W,
) -> Result<()> {
    let ffx: ffx_command::Ffx = argh::from_env();

    let provider = DiagnosticsBridgeProvider::new(diagnostics_proxy, rcs_proxy);
    let result = provider
        .snapshot_diagnostics_data::<Inspect>(
            &None,
            &[
                String::from("assistant_*_runner.cmx:root:vm_service_port"),
                String::from(
                    "core/session-manager/session\\:session/flutter_*_runner:root:vm_service_port",
                ),
                String::from("flutter_*_runner.cmx:root:vm_service_port"),
                String::from("g3_assistant_*_runner.cmx:root:vm_service_port"),
            ],
        )
        .await
        .map_err(|e| anyhow!(ffx_error!("{}", e)))?;
    let mut vm_service_port: u16 = 0;

    for item in result {
        match item.payload {
            Some(hierarchy) => {
                for property in hierarchy.properties {
                    if property.name() == "vm_service_port" {
                        vm_service_port = property.string().unwrap().parse::<u16>().unwrap();
                        break;
                    }
                }
            }
            None => eprintln!("Inspect data not available."),
        }
    }

    // TODO(fxb/80802): Keep ssh address resolution in sync with get_ssh_address_impl
    // in src/developer/ffx/plugins/target/get-ssh-address until extracted out to shared
    // location.
    let target: Option<String> = ffx_config::get("target.default").await?;
    let res = timeout(Duration::from_secs(1), target_proxy.get_ssh_address()).await.map_err(
        |_timeout_err| FfxError::DaemonError {
            err: DaemonError::Timeout,
            target,
            is_default_target: ffx.target.is_none(),
        },
    )??;
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
    writeln!(writer, "vm_service_port: {}", vm_service_port)?;

    let local_port = pick_unused_port().unwrap();
    let local_socket = SocketAddr::new(IpAddr::V4(Ipv4Addr::new(127, 0, 0, 1)), local_port);
    let dart_vm_socket = SocketAddr::new(ip, vm_service_port);

    let ssh_file_path: String =
        ffx_config::query("ssh.priv").get_file().await.context("getting ssh private key path")?;

    let mut command = Command::new("ssh");
    command.args(DEFAULT_SSH_OPTIONS);
    command.arg("-i").arg(ssh_file_path);
    command.args(vec![
        "-fnNT",
        "-L",
        format!("{}:{}:{}", local_socket.port(), local_socket.ip(), vm_service_port).as_str(),
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

    let _ = wait_for_kill().await;
    writeln!(writer, "SIGINT received. Shutting down.")?;
    Ok(())
}
