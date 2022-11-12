// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    async_io::Async,
    errors::{ffx_bail, ffx_error},
    fuchsia_async::unblock,
    futures_util::future::FutureExt,
    futures_util::io::AsyncReadExt,
    signal_hook::{
        consts::signal::{SIGINT, SIGTERM},
        low_level::pipe,
    },
    std::ffi::OsStr,
    std::os::unix::net::UnixStream,
    std::process::Command,
    std::sync::{atomic::AtomicBool, Arc},
};

mod debug_agent;

pub use debug_agent::DebugAgentSocket;

#[ffx_core::ffx_plugin(
    fidl_fuchsia_debugger::DebugAgentProxy = "core/debug_agent:expose:fuchsia.debugger.DebugAgent"
)]
pub async fn connect(
    debugger_proxy: fidl_fuchsia_debugger::DebugAgentProxy,
    cmd: ffx_debug_connect_args::ConnectCommand,
) -> Result<()> {
    let socket = DebugAgentSocket::create(debugger_proxy)?;

    if cmd.agent_only {
        println!("{}", socket.unix_socket_path().display());

        // We have to construct these Async objects ourselves instead of using
        // async_net::UnixStream to force the use of std::os::unix::UnixStream,
        // which implements IntoRawFd - a requirement for the pipe::register
        // calls below.
        let (mut sigterm_receiver, sigterm_sender) = Async::<UnixStream>::pair()?;
        let (mut sigint_receiver, sigint_sender) = Async::<UnixStream>::pair()?;

        // Note: This does not remove the non-blocking nature of Async from the
        // UnixStream objects or file descriptors.
        pipe::register(SIGTERM, sigterm_sender.into_inner()?)?;
        pipe::register(SIGINT, sigint_sender.into_inner()?)?;

        let _forward_task = fuchsia_async::Task::local(async move {
            loop {
                let _ = socket.forward_one_connection().await.map_err(|e| {
                    eprintln!("Connection to debug_agent broken: {}", e);
                });
            }
        });

        let mut sigterm_buf = [0u8; 4];
        let mut sigint_buf = [0u8; 4];

        futures::select! {
            res = sigterm_receiver.read(&mut sigterm_buf).fuse() => res?,
            res = sigint_receiver.read(&mut sigint_buf).fuse() => res?,
        };

        return Ok(());
    }

    if let Err(e) = symbol_index::ensure_symbol_index_registered().await {
        eprintln!("ensure_symbol_index_registered failed, error was: {:#?}", e);
    }

    let sdk = ffx_config::get_sdk().await?;

    let zxdb_path = sdk.get_host_tool("zxdb")?;

    let mut args: Vec<&OsStr> = vec!["--unix-connect".as_ref(), socket.unix_socket_path().as_ref()];

    if cmd.no_auto_attach_limbo {
        args.push("--no-auto-attach-limbo".as_ref());
    }

    args.extend(cmd.zxdb_args.iter().map(|s| AsRef::<OsStr>::as_ref(s)));

    let mut zxdb = match cmd.debugger {
        Some(debugger) => {
            if *sdk.get_version() != ffx_config::sdk::SdkVersion::InTree {
                // OOT doesn't provide symbols for zxdb.
                ffx_bail!("--debugger only works in-tree.");
            }
            let debugger_arg = if debugger == "gdb" {
                "--args"
            } else if debugger == "lldb" {
                "--"
            } else {
                ffx_bail!("--debugger must be gdb or lldb");
            };
            // lldb can find .build-id directory automatically but gdb has some trouble.
            // So we supply the unstripped version for them.
            let zxdb_unstripped_path = zxdb_path.parent().unwrap().join("exe.unstripped/zxdb");
            // Ignore SIGINT because Ctrl-C is used to interrupt zxdb and return to the debugger.
            signal_hook::flag::register(SIGINT, Arc::new(AtomicBool::new(false)))?;
            Command::new(debugger)
                .current_dir(sdk.get_path_prefix())
                .arg(debugger_arg)
                .arg(zxdb_unstripped_path)
                .args(args)
                .spawn()?
        }
        None => Command::new(zxdb_path).args(args).spawn()?,
    };

    // Spawn the task that doing the forwarding in the background.
    let _task = fuchsia_async::Task::local(async move {
        let _ = socket.forward_one_connection().await.map_err(|e| {
            eprintln!("Connection to debug_agent broken: {}", e);
        });
    });

    if let Some(exit_code) = unblock(move || zxdb.wait()).await?.code() {
        if exit_code == 0 {
            Ok(())
        } else {
            Err(ffx_error!("zxdb exited with code {}", exit_code).into())
        }
    } else {
        Err(ffx_error!("zxdb terminated by signal").into())
    }
}
