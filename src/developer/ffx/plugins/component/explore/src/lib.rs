// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Error, Result},
    atty::Stream,
    errors::{ffx_bail, ffx_error},
    ffx_component::{query::get_cml_moniker_from_query, rcs::connect_to_realm_explorer},
    ffx_component_explore_args::ExploreComponentCommand,
    ffx_core::ffx_plugin,
    fidl_fuchsia_dash::{DashNamespaceLayout, LauncherError, LauncherEvent, LauncherProxy},
    fidl_fuchsia_developer_remotecontrol as rc, fidl_fuchsia_io as fio,
    futures::prelude::*,
    std::io::{Read, StdoutLock, Write},
    termion::raw::{IntoRawMode, RawTerminal},
};

enum Terminal<'a> {
    Raw(RawTerminal<StdoutLock<'a>>),
    Std,
}

impl std::io::Write for Terminal<'_> {
    fn flush(&mut self) -> Result<(), std::io::Error> {
        match self {
            Terminal::Raw(r) => r.flush(),
            Terminal::Std => std::io::stdout().flush(),
        }
    }
    fn write(&mut self, buf: &[u8]) -> Result<usize, std::io::Error> {
        match self {
            Terminal::Raw(r) => r.write(buf),
            Terminal::Std => std::io::stdout().write(buf),
        }
    }
}

impl Terminal<'_> {
    // Create a terminal either in raw or standard mode. Use raw for interactivity, indicated by the
    // given command being None. Return an error if the terminal can't be created for the required
    // combination of interactivity and output pipes: Output pipes are allowed only when not
    // in interactive mode.
    fn new(cmd: &Option<String>) -> Result<Self> {
        let piping = !atty::is(Stream::Stdout);
        if piping && cmd.is_none() {
            ffx_bail!("ffx component explore does not support pipes in interactive mode");
        }

        if cmd.is_none() && !piping {
            // Put the host terminal into raw mode, so input characters are not echoed, streams are
            // not buffered and newlines are not changed.
            let term_out = std::io::stdout()
                .lock()
                .into_raw_mode()
                .map_err(|e| ffx_error!("could not set raw mode on terminal: {}", e))?;
            Ok(Terminal::Raw(term_out))
        } else {
            Ok(Terminal::Std)
        }
    }
}

// TODO(https://fxbug.dev/102835): This plugin needs E2E tests.
#[ffx_plugin(LauncherProxy = "core/debug-dash-launcher:expose:fuchsia.dash.Launcher")]
pub async fn explore(
    rcs: rc::RemoteControlProxy,
    launcher_proxy: LauncherProxy,
    cmd: ExploreComponentCommand,
) -> Result<()> {
    let realm_explorer = connect_to_realm_explorer(&rcs).await?;
    let moniker = get_cml_moniker_from_query(&cmd.query, &realm_explorer).await?;

    println!("Moniker: {}", moniker);

    // LifecycleController accepts RelativeMonikers only.
    let relative_moniker = format!(".{}", moniker.to_string());
    let tools_url = cmd.tools.as_deref();

    // Launch dash with the given moniker and stdio handles.
    let (pty, pty_server) = fidl::Socket::create(fidl::SocketOpts::STREAM)?;
    let mut terminal = Terminal::new(&cmd.command)?;

    let ns_layout = cmd.ns_layout.map(|l| l.0).unwrap_or(DashNamespaceLayout::NestAllInstanceDirs);

    launcher_proxy
        .launch_with_socket(&relative_moniker, pty_server, tools_url, cmd.command.as_deref(), ns_layout)
        .await
        .map_err(|e| ffx_error!("fidl error launching dash: {}", e))?
        .map_err(|e| match e {
            LauncherError::InstanceNotFound => ffx_error!("No instance was found matching the moniker '{}'. Use `ffx component list` to find the correct moniker to use here.", moniker),
            LauncherError::InstanceNotResolved => ffx_error!("The specified instance is not resolved. Use `ffx component resolve {}` and retry this command", moniker),
            e => ffx_error!("Unexpected error launching dash: {:?}", e),
        })?;

    let pty = fuchsia_async::Socket::from_socket(pty)?;
    let (mut read_from_pty, mut write_to_pty) = pty.split();

    // Set up a thread for forwarding stdin. Reading from stdin is a blocking operation which
    // will halt the executor if it were to run on the same thread.
    std::thread::spawn(move || {
        let mut executor = fuchsia_async::LocalExecutor::new()?;
        executor.run_singlethreaded(async move {
            let mut term_in = std::io::stdin().lock();
            let mut buf = [0u8; fio::MAX_BUF as usize];
            loop {
                let bytes_read = term_in.read(&mut buf)?;
                if bytes_read == 0 {
                    return Ok::<(), Error>(());
                }
                write_to_pty.write_all(&buf[..bytes_read]).await?;
                write_to_pty.flush().await?;
            }
        })?;
        Ok::<(), Error>(())
    });

    // In a loop, wait for the TTY to be readable and print out the bytes.
    loop {
        let mut buf = [0u8; fio::MAX_BUF as usize];
        let bytes_read = read_from_pty.read(&mut buf).await?;
        if bytes_read == 0 {
            // There are no more bytes to read. This means that the socket has been closed. This is
            // probably because the dash process has terminated.
            break;
        }
        terminal.write_all(&buf[..bytes_read])?;
        terminal.flush()?;
    }

    if matches!(terminal, Terminal::Raw(_)) {
        drop(terminal);
        eprintln!("Connection to terminal closed");
    }

    // Report process errors and return the exit status.
    let mut event_stream = launcher_proxy.take_event_stream();
    match event_stream.next().await {
        Some(Ok(LauncherEvent::OnTerminated { return_code })) => {
            std::process::exit(return_code);
        }
        Some(Err(e)) => Err(anyhow!("OnTerminated event error: {:?}", e)),
        None => Err(anyhow!("didn't receive an expected OnTerminated event")),
    }
}
