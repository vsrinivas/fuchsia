// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Error, Result},
    async_net::unix::UnixListener,
    ffx_config::sdk::SdkVersion,
    futures_util::future::FutureExt,
    futures_util::io::{AsyncReadExt, AsyncWriteExt},
    std::fs,
};

struct ProcessArguments {
    arguments: Vec<String>,
}

impl ProcessArguments {
    fn new() -> Self {
        Self { arguments: Vec::new() }
    }

    fn add_flag(&mut self, name: &str, value: bool) {
        if value {
            self.arguments.push(name.to_string());
        }
    }

    fn add_value(&mut self, name: &str, value: &str) {
        self.arguments.push(name.to_string());
        self.arguments.push(value.to_string());
    }

    fn add_option(&mut self, name: &str, value: &Option<String>) {
        if let Some(value) = &value {
            self.arguments.push(name.to_string());
            self.arguments.push(value.to_string());
        }
    }

    fn add_values(&mut self, name: &str, value: &Vec<String>) {
        for value in value.iter() {
            self.arguments.push(name.to_string());
            self.arguments.push(value.to_string());
        }
    }
}

const UNIX_SOCKET: &str = "/tmp/debug_agent.socket";

#[ffx_core::ffx_plugin(
    "debug.enabled",
    fidl_fuchsia_debugger::DebugAgentProxy = "core/appmgr:out:fuchsia.debugger.DebugAgent"
)]
pub async fn fidlcat(
    debugger_proxy: fidl_fuchsia_debugger::DebugAgentProxy,
    cmd: ffx_debug_fidlcat_args::FidlcatCommand,
) -> Result<()> {
    let result = execute_debug(debugger_proxy, &cmd).await;
    // Removes the Unix socket file to be able to connect again.
    let _ = fs::remove_file(UNIX_SOCKET);
    result
}

pub async fn execute_debug(
    debugger_proxy: fidl_fuchsia_debugger::DebugAgentProxy,
    cmd: &ffx_debug_fidlcat_args::FidlcatCommand,
) -> Result<(), Error> {
    let sdk = ffx_config::get_sdk().await?;

    let mut arguments = ProcessArguments::new();
    let mut needs_debug_agent = true;

    let command_path = sdk.get_host_tool("fidlcat")?;

    if let Some(from) = &cmd.from {
        if from != "device" {
            needs_debug_agent = false;
            arguments.add_value("--from", from);
        }
    }

    arguments.add_option("--to", &cmd.to);
    arguments.add_option("--format", &cmd.format);
    arguments.add_values("--with", &cmd.with);
    arguments.add_flag("--with-process-info", cmd.with_process_info);
    arguments.add_option("--stack", &cmd.stack);
    arguments.add_values("--syscalls", &cmd.syscalls);
    arguments.add_values("--exclude-syscalls", &cmd.exclude_syscalls);
    arguments.add_values("--messages", &cmd.messages);
    arguments.add_values("--exclude-messages", &cmd.exclude_messages);
    arguments.add_values("--trigger", &cmd.trigger);
    arguments.add_values("--thread", &cmd.thread);
    arguments.add_flag("--dump-messages", cmd.dump_messages);

    if needs_debug_agent {
        // Processes to monitor.
        arguments.add_values("--remote-pid", &cmd.remote_pid);
        arguments.add_values("--remote-name", &cmd.remote_name);
        arguments.add_values("--extra-name", &cmd.extra_name);

        // Jobs to monitor.
        arguments.add_values("--remote-job-id", &cmd.remote_job_id);
        arguments.add_values("--remote-job-name", &cmd.remote_job_name);
    }

    if let SdkVersion::InTree = sdk.get_version() {
        // When ffx is used in tree, uses the JSON IR files listed in all_fidl_json.txt.
        let ir_file = format!("@{}/all_fidl_json.txt", sdk.get_path_prefix().to_str().unwrap());
        arguments.add_value("--fidl-ir-path", &ir_file);
    }

    if !needs_debug_agent {
        // Start fidlcat locally.
        let child = std::process::Command::new(&command_path).args(&arguments.arguments).spawn();
        if let Err(error) = child {
            return Err(anyhow!("Can't launch {:?}: {:?}", command_path, error));
        }
        let mut child = child.unwrap();

        // When the debug agent is not needed, the process (fidlcat) doesn't communicate with the
        // device. In that case, just wait for the process to terminate.
        let _ = child.wait();
        return Ok(());
    }

    // Connect to the debug_agent on the device.
    let (sock_server, sock_client) =
        fidl::Socket::create(fidl::SocketOpts::STREAM).expect("Failed while creating socket");
    debugger_proxy.connect(sock_client).await?;

    let (rx, tx) = fidl::AsyncSocket::from_socket(sock_server)?.split();
    let rx = std::cell::RefCell::new(rx);
    let tx = std::cell::RefCell::new(tx);

    // Create our Unix socket.
    let listener = UnixListener::bind(UNIX_SOCKET)?;

    // Connect to the Unix socket.
    arguments.add_value("--unix-connect", UNIX_SOCKET);

    // Use the symbol server.
    arguments.add_value("--symbol-server", "gs://fuchsia-artifacts-release/debug");

    // Terminate the debug agent when exiting.
    arguments.add_flag("--quit-agent-on-exit", true);

    // Start fidlcat or zxdb locally.
    let child = std::process::Command::new(&command_path).args(&arguments.arguments).spawn();
    if let Err(error) = child {
        return Err(anyhow!("Can't launch {:?}: {:?}", command_path, error));
    }

    // Wait for a connection on the unix socket (connection from zxdb).
    let (socket, _) = listener.accept().await?;

    let (mut zxdb_rx, mut zxdb_tx) = socket.split();
    let mut debug_agent_rx = rx.borrow_mut();
    let mut debug_agent_tx = tx.borrow_mut();

    // Reading from zxdb (using the Unix socket) and writing to the debug agent.
    let zxdb_socket_read = async move {
        let mut buffer = [0; 4096];
        loop {
            let n = zxdb_rx.read(&mut buffer[..]).await?;
            if n == 0 {
                return Ok(()) as Result<(), Error>;
            }
            let mut ofs = 0;
            while ofs != n {
                let wrote = debug_agent_tx.write(&buffer[ofs..n]).await?;
                ofs += wrote;
                if wrote == 0 {
                    return Ok(()) as Result<(), Error>;
                }
            }
        }
    };
    let mut zxdb_socket_read = Box::pin(zxdb_socket_read.fuse());

    // Reading from the debug agent and writing to zxdb (using the Unix socket).
    let zxdb_socket_write = async move {
        let mut buffer = [0; 4096];
        loop {
            let n = debug_agent_rx.read(&mut buffer).await?;
            let mut ofs = 0;
            while ofs != n {
                let wrote = zxdb_tx.write(&buffer[ofs..n]).await?;
                ofs += wrote;
                if wrote == 0 {
                    return Ok(()) as Result<(), Error>;
                }
            }
        }
    };
    let mut zxdb_socket_write = Box::pin(zxdb_socket_write.fuse());

    // ffx zxdb exits when we have an error on the unix socket (either read or write).
    futures::select! {
        read_res = zxdb_socket_read => read_res?,
        write_res = zxdb_socket_write => write_res?,
    };
    return Ok(()) as Result<(), Error>;
}
