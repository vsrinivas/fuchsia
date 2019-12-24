// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl::endpoints::ClientEnd,
    fidl_fuchsia_io::FileMarker,
    fidl_fuchsia_netemul_guest::{CommandListenerEvent, CommandListenerEventStream},
    fuchsia_zircon as zx,
    futures::TryStreamExt,
    std::fs::File,
};

/// Converts a File to a FileMarker ClientEnd.
///
/// # Arguments
///
/// * `file` - An open File to be converted into a ClientEnd<FileMarker>.
///
/// # Example
/// ```
/// let local_file = "/data/local";
/// let client_end = file_to_client(&File::create(local_file)?)?;
/// ```
pub fn file_to_client(file: &File) -> Result<ClientEnd<FileMarker>, Error> {
    let channel = fdio::clone_channel(file)?;
    Ok(ClientEnd::new(channel))
}

/// Ensures that a command executed on a guest starts and stops without error and feeds in any
/// initial stdin.
///
/// # Arguments
///
/// * `stream` - CommandListenerEventStream to be polled for OnStarted and OnTerminated events.
/// * `stdin_socket` - Optional fuchsia_zircon socket to which any provided stdin will be written.
/// * `to_write` - String slice to be written to the `stdin_socket`.
///
/// # Example
///
/// ```
/// let guest_discovery_service = client::connect_to_service::<GuestDiscoveryMarker>()?;
/// let (gis, gis_ch) = fidl::endpoints::create_proxy::<GuestInteractionMarker>()?;
/// let () = guest_discovery_service.get_guest(None, "debian_guest", gis_ch)?;
///
/// let (stdin_0, stdin_1) = zx::Socket::create(zx::SocketOpts::STREAM).unwrap();
///
/// let (client_proxy, server_end) = fidl::endpoints::create_proxy::<CommandListenerMarker>()
///     .context("Failed to create CommandListener ends")?;
///
/// gis.execute_command(
///     command_to_run,
///     &mut env.iter_mut(),
///     Some(stdin_1),
///     None,
///     None,
///     server_end,
/// )?;
///
/// wait_for_command_completion(client_proxy.take_event_stream(), Some(stdin_0), &stdin_input)
///     .await?;
/// ```
pub async fn wait_for_command_completion(
    mut stream: CommandListenerEventStream,
    stdin: Option<(zx::Socket, &str)>,
) -> Result<(), Error> {
    loop {
        let event = stream.try_next().await?;
        match event.unwrap() {
            CommandListenerEvent::OnStarted { status } => {
                zx::ok(status)?;

                if let Some((stdin_socket, to_write)) = stdin.as_ref() {
                    stdin_socket.write(to_write.as_bytes())?;
                }
            }
            CommandListenerEvent::OnTerminated { status, return_code } => {
                zx::ok(status)?;
                assert_eq!(return_code, 0);
                return Ok(());
            }
        }
    }
}
