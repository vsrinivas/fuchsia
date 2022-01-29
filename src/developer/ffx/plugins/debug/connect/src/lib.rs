// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result, errors::ffx_error, fuchsia_async::unblock, std::ffi::OsString,
    std::process::Command,
};

mod debug_agent;
pub use debug_agent::DebugAgentSocket;

#[ffx_core::ffx_plugin(
    fidl_fuchsia_debugger::DebugAgentProxy = "core/appmgr:out:fuchsia.debugger.DebugAgent"
)]
pub async fn connect(
    debugger_proxy: fidl_fuchsia_debugger::DebugAgentProxy,
    cmd: ffx_debug_connect_args::ConnectCommand,
) -> Result<i32> {
    if let Err(e) = symbol_index::ensure_symbol_index_registered().await {
        eprintln!("ensure_symbol_index_registered failed, error was: {:#?}", e);
    }

    let socket = DebugAgentSocket::create(debugger_proxy)?;

    let zxdb_path = ffx_config::get_sdk().await?.get_host_tool("zxdb")?;
    let mut args: Vec<OsString> = vec![
        "--unix-connect".to_owned().into(),
        socket.unix_socket_path().to_owned().into(),
        "--quit-agent-on-exit".to_owned().into(),
    ];
    args.extend(cmd.zxdb_args.into_iter().map(|s| s.into()));

    let mut zxdb = Command::new(zxdb_path).args(args).spawn()?;

    // Spawn the task that doing the forwarding in the background.
    let _task = fuchsia_async::Task::local(async move {
        let _ = socket.forward_one_connection().await.map_err(|e| {
            eprintln!("Connection to debug_agent broken: {}", e);
        });
    });

    if let Some(exit_code) = unblock(move || zxdb.wait()).await?.code() {
        Ok(exit_code)
    } else {
        Err(ffx_error!("zxdb terminated by signal").into())
    }
}
