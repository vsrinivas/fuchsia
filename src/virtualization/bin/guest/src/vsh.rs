// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod pty;
mod termina;
mod util;

use {
    anyhow::{anyhow, Context, Result},
    fidl_fuchsia_hardware_pty as fpty,
    fidl_fuchsia_virtualization::{
        GuestManagerProxy, GuestMarker, GuestStatus, HostVsockEndpointMarker,
    },
    fuchsia_async::{self as fasync, Duration, Timer},
    fuchsia_zircon::{self as zx, HandleBased},
    futures::{future::Fuse, pin_mut, select, AsyncReadExt, AsyncWriteExt, FutureExt},
    tracing,
    vsh_rust_proto::vm_tools::vsh,
};

// Some guest message helpers

/// In order to reduce allocations this helper takes in data as a Vec and returns it for further
/// reuse.
async fn stdin_message(socket: &mut fasync::Socket, data: Vec<u8>) -> Result<Vec<u8>> {
    if data.len() > util::MAX_DATA_SIZE {
        anyhow::bail!("Given data too large for DataMessage: {} bytes", data.len());
    }
    let msg = vsh::GuestMessage {
        msg: Some(vsh::guest_message::Msg::DataMessage(vsh::DataMessage {
            stream: vsh::StdioStream::StdinStream as i32,
            data,
        })),
    };
    util::send_message(socket, &msg).await?;

    // Return vec for reuse
    Ok(match msg.msg.unwrap() {
        vsh::guest_message::Msg::DataMessage(vsh::DataMessage { data, .. }) => data,
        _ => unreachable!(),
    })
}

async fn window_resize_message(socket: &mut fasync::Socket, rows: i32, cols: i32) -> Result<()> {
    util::send_message(
        socket,
        &vsh::GuestMessage {
            msg: Some(vsh::guest_message::Msg::ResizeMessage(vsh::WindowResizeMessage {
                rows,
                cols,
            })),
        },
    )
    .await
}

// Initiate communication with guest vshd and initialize the local pty
async fn init_shell(socket: &mut fasync::Socket, args: Vec<String>) -> Result<Option<pty::RawPty>> {
    let mut conn_req = vsh::SetupConnectionRequest::default();
    // Target can be "/vm_shell" or the empty string for the VM.
    // Specifying container name directly here is not supported.
    conn_req.target = String::new();
    // User can be defaulted with empty string. This is chronos for vmshell and root otherwise
    conn_req.user = String::new();
    // Blank command for login shell. (other uses deprecated, use argv directly instead)
    conn_req.command = String::new();
    conn_req.argv = args;
    conn_req.env = std::collections::HashMap::from([
        ("LXD_DIR".to_string(), "/mnt/stateful/lxd".to_string()),
        ("LXD_CONF".to_string(), "/mnt/stateful/lxd_conf".to_string()),
        ("LXD_UNPRIVILEGED_ONLY".to_string(), "true".to_string()),
    ]);
    if let Ok(term) = std::env::var("TERM") {
        conn_req.env.insert("TERM".to_string(), term);
    }

    util::send_message(socket, &conn_req).await?;
    let conn_resp: vsh::SetupConnectionResponse = util::recv_message(socket).await?;

    if conn_resp.status() != vsh::ConnectionStatus::Ready {
        anyhow::bail!(
            "Server was unable to set up connection properly: {:?}",
            conn_resp.description
        );
    }

    let maybe_raw_pty = pty::RawPty::new().await.map_err(|e| tracing::warn!("{e}")).ok().flatten();

    let (cols, rows) = match maybe_raw_pty.as_ref() {
        Some(raw_pty) => (raw_pty.cols(), raw_pty.rows()),
        None => (80, 24),
    };

    window_resize_message(socket, rows, cols).await?;

    Ok(maybe_raw_pty)
}

// The main I/O handlers

// Receive messages from the guest vshd, and output to stdout
async fn console_out(
    stdout: &mut fasync::net::EventedFd<std::fs::File>,
    stderr: &mut fasync::net::EventedFd<std::fs::File>,
    mut socket: fasync::Socket,
) -> Result<i32> {
    loop {
        let host_message: vsh::HostMessage = util::recv_message(&mut socket).await?;
        let msg = host_message.msg.context("HostMessage must contain a msg")?;
        match msg {
            vsh::host_message::Msg::DataMessage(data_message) => match data_message.stream() {
                vsh::StdioStream::StdoutStream => stdout.write_all(&data_message.data).await?,
                vsh::StdioStream::StderrStream => stderr.write_all(&data_message.data).await?,
                s => anyhow::bail!("Received unexpected stream in console_out: {:?}", s),
            },
            vsh::host_message::Msg::StatusMessage(status_message) => {
                match vsh::ConnectionStatus::from_i32(status_message.status) {
                    Some(vsh::ConnectionStatus::Ready) => {}
                    Some(vsh::ConnectionStatus::Exited) => return Ok(status_message.code),
                    _ => {
                        anyhow::bail!(
                            "Guest sent connection status: {}, and description: {}",
                            status_message.status,
                            status_message.description
                        )
                    }
                }
            }
        }
    }
}

// Receive input from stdin, and forward them as messages to guest vshd
async fn console_in(
    stdin: &mut fasync::net::EventedFd<std::fs::File>,
    mut socket: fasync::Socket,
) -> Result<()> {
    // Try to get a handle to the pty::Device and signaling eventpair. If this is None then stdin
    // isn't a pty and we don't need to handle resize events.
    let pty_and_eventpair = pty::get_pty(stdin).await?;

    // When a new event can be read using ReadEvent, SIGNAL_EVENT is signaled on the eventpair.
    // Convert SIGNAL_EVENT from a DeviceSignal bitflag to a zx::Signal.
    let signal_event = zx::Signals::from_bits(fpty::SIGNAL_EVENT.bits())
        .expect("SIGNAL_EVENT should be a zx::Signal!");

    let event_handler = Fuse::terminated();
    pin_mut!(event_handler);

    if let Some((_, eventpair)) = pty_and_eventpair.as_ref() {
        event_handler.set(fasync::OnSignals::new(eventpair, signal_event).fuse());
    }

    let mut buf = vec![0; util::MAX_DATA_SIZE];
    let mut read_fut = stdin.read(&mut buf).fuse();
    loop {
        select! {
            result = read_fut => {
                let bytes_read = result.context("stdin read failure")?;
                // To reduce allocations in the critical path we reuse the same Vec, updating its
                // size as required.
                buf.truncate(bytes_read);
                buf = stdin_message(&mut socket, buf)
                    .await
                    .context("Failed to send message over vsock")?;
                buf.resize(util::MAX_DATA_SIZE, 0u8);
                read_fut = stdin.read(&mut buf).fuse();
            },
            result = event_handler => {
                let _signals = result.map_err(|status| {
                    anyhow!("Waiting on pty event errored with status {status}")
                })?;

                // Unwrap since if we are in this handler we expect the event to exist.
                let (pty, eventpair) =
                    pty_and_eventpair.as_ref().expect("Pty should exist if we are in this branch");

                let events = pty.read_events().await.context("ReadEvents call failed").and_then(
                    |(status, events)| {
                        zx::Status::ok(status).context("ReadEvents status not OK")?;
                        Ok(events)
                    },
                )?;

                if (events & fpty::EVENT_WINDOW_SIZE) != 0 {
                    let win_size = pty::get_window_size(pty).await?;
                    window_resize_message(
                        &mut socket,
                        win_size.height.try_into()?,
                        win_size.width.try_into()?,
                    )
                    .await
                    .context("Failed to send window resize message to the guest")?
                }

                if (events & !fpty::EVENT_WINDOW_SIZE) != 0 {
                    // Since we are in raw mode neither EVENT_INTERRUPT nor EVENT_SUSPEND
                    // should assert. Furthermore EVENT_HANGUP should not assert either
                    // since we are the active client. So either something suspicious is
                    // happening, or a new event was added that we may wish to consider
                    // handling.
                    tracing::warn!("Unexpected Pty event received: {events:#x}");
                }

                event_handler.set(fasync::OnSignals::new(eventpair, signal_event).fuse());
            },
        }
    }
}

/// Initiate a connection with a termina guest's shell through vshd. If `args` are provided they are
/// treated as the argv of the program to run, otherwise a login shell is provided. If termina is
/// not already started we will try to start it.
///
/// Returns the exit code of the remote process, or an error indicating the failure reason.
pub async fn handle_vsh(
    stdin: &mut fasync::net::EventedFd<std::fs::File>,
    stdout: &mut fasync::net::EventedFd<std::fs::File>,
    stderr: &mut fasync::net::EventedFd<std::fs::File>,
    termina_manager: GuestManagerProxy,
    port: Option<u32>,
    is_container: bool,
    mut args: Vec<String>,
) -> Result<i32> {
    let port = port.unwrap_or(util::VSH_PORT);

    let guest_info = termina_manager.get_info().await?;
    match guest_info.guest_status {
        Some(GuestStatus::Starting) | Some(GuestStatus::Running) => {}
        _ => loop {
            if let Err(e) = termina::launch(stdout.as_mut()).await {
                println!("Starting the Linux container has failed because of: {:?}", e);
                println!("Retry? (Y/n)");
                let mut answer = [0];
                stdin.read_exact(&mut answer).await?;
                match answer[0] {
                    b'y' | b'Y' | b'\n' => continue,
                    _ => anyhow::bail!(e),
                }
            }
            break;
        },
    }

    let (guest, guest_server_end) = fidl::endpoints::create_proxy::<GuestMarker>()?;
    termina_manager
        .connect(guest_server_end)
        .await
        .context("GuestManager.ConnectToGuest call failed")?
        .map_err(|e| anyhow!("GuestManager.ConnectToGuest returned an error: {e:?}"))?;

    let (vsock_endpoint, vsock_server_end) =
        fidl::endpoints::create_proxy::<HostVsockEndpointMarker>()?;
    guest
        .get_host_vsock_endpoint(vsock_server_end)
        .await?
        .map_err(|e| anyhow!("Unable to get vsock endpoint: {:?}", e))?;

    let raw_socket = vsock_endpoint.connect(port).await?.map_err(zx::Status::from_raw)?;

    // No practical difference between |socket_in| and |socket_out| but semantically separate them
    let mut socket_in =
        fasync::Socket::from_socket(raw_socket.duplicate_handle(zx::Rights::SAME_RIGHTS)?)?;
    let socket_out = fasync::Socket::from_socket(raw_socket)?;

    let is_login_shell = args.is_empty();

    if is_container {
        args = if is_login_shell {
            ["lxc", "exec", "penguin", "--", "login", "-f", "machina"]
                .into_iter()
                .map(ToString::to_string)
                .collect()
        } else {
            ["lxc", "exec", "penguin", "--"]
                .into_iter()
                .map(ToString::to_string)
                .chain(args.into_iter())
                .collect()
        }
    }

    let _reset_pty_on_drop =
        init_shell(&mut socket_in, args).await.context(anyhow!("Failed to initialize pty"))?;

    // Inject penguin helper function when connecting to the default login shell of the VM.
    if !is_container && is_login_shell {
        // Inserting a sleep here gives the prompt a chance to render before we insert keystrokes
        Timer::new(Duration::from_millis(100)).await;
        if let Err(e) = stdin_message(
            &mut socket_in,
            b"function penguin() { lxc exec penguin -- login -f machina ; } \n\n".to_vec(),
        )
        .await
        {
            tracing::warn!("Failed to inject helper with error: {:?}", e);
        }
    }

    let stdin_handler = console_in(stdin, socket_in).fuse();
    let stdout_handler = console_out(stdout, stderr, socket_out).fuse();
    pin_mut!(stdin_handler, stdout_handler);
    loop {
        select! {
            result = stdin_handler => {
                // It's expected this will be an error if for example the guest side is finished
                // and we are no longer able to send it messages over vsock.
                if let Err(e) = result.context("stdin handler finished with an error") {
                    tracing::debug!("{e:?}");
                }
            }
            result = stdout_handler => {
                match result {
                    Ok(exit_code) => {
                        tracing::debug!("Guest process ended with exit code: {exit_code}");
                        return Ok(exit_code);
                    }
                    Err(e) => anyhow::bail!("stdout handler finished with error: {e:?}")
                }
            }
        }
    }
}
