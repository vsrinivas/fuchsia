// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    fidl_fuchsia_developer_remotecontrol::{RemoteControlMarker, RemoteControlProxy},
    fidl_fuchsia_overnet::MeshControllerProxyInterface,
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
    futures::{future::try_join, prelude::*},
    std::io::{Read, Write},
};

async fn copy_stdin_to_socket(
    mut tx_socket: futures::io::WriteHalf<fidl::AsyncSocket>,
) -> Result<(), Error> {
    let (mut tx_stdin, mut rx_stdin) = futures::channel::mpsc::channel::<Vec<u8>>(2);
    std::thread::Builder::new()
        .spawn(move || -> Result<(), Error> {
            let mut buf = [0u8; 1024];
            let mut stdin = std::io::stdin();
            loop {
                let n = stdin.read(&mut buf)?;
                if n == 0 {
                    return Ok(());
                }
                let buf = &buf[..n];
                futures::executor::block_on(tx_stdin.send(buf.to_vec()))?;
            }
        })
        .context("Spawning blocking thread")?;
    while let Some(buf) = rx_stdin.next().await {
        tx_socket.write(buf.as_slice()).await?;
    }
    Ok(())
}

async fn copy_socket_to_stdout(
    mut rx_socket: futures::io::ReadHalf<fidl::AsyncSocket>,
) -> Result<(), Error> {
    let (mut tx_stdout, mut rx_stdout) = futures::channel::mpsc::channel::<Vec<u8>>(2);
    std::thread::Builder::new()
        .spawn(move || -> Result<(), Error> {
            let mut stdout = std::io::stdout();
            while let Some(buf) = futures::executor::block_on(rx_stdout.next()) {
                let mut buf = buf.as_slice();
                loop {
                    let n = stdout.write(buf)?;
                    if n == buf.len() {
                        stdout.flush()?;
                        break;
                    }
                    buf = &buf[n..];
                }
            }
            Ok(())
        })
        .context("Spawning blocking thread")?;
    let mut buf = [0u8; 1024];
    loop {
        let n = rx_socket.read(&mut buf).await?;
        tx_stdout.send((&buf[..n]).to_vec()).await?;
    }
}

async fn send_request(proxy: RemoteControlProxy) -> Result<(), Error> {
    // We just need to make a request to the RCS - it doesn't really matter
    // what we choose here so long as there are no side effects.
    let _ = proxy.identify_host().await?;
    Ok(())
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let rcs_proxy = connect_to_service::<RemoteControlMarker>()?;
    send_request(rcs_proxy).await?;
    let (local_socket, remote_socket) = fidl::Socket::create(fidl::SocketOpts::STREAM)?;
    let local_socket = fidl::AsyncSocket::from_socket(local_socket)?;
    let (rx_socket, tx_socket) = futures::AsyncReadExt::split(local_socket);
    hoist::connect_as_mesh_controller()?.attach_socket_link(
        remote_socket,
        fidl_fuchsia_overnet_protocol::SocketLinkOptions::empty(),
    )?;
    try_join(copy_socket_to_stdout(rx_socket), copy_stdin_to_socket(tx_socket)).await?;

    Ok(())
}

#[cfg(test)]
mod test {
    use {
        crate::send_request,
        anyhow::Error,
        fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_developer_remotecontrol::{
            IdentifyHostResponse, RemoteControlMarker, RemoteControlProxy, RemoteControlRequest,
        },
        fuchsia_async as fasync,
        futures::prelude::*,
    };

    fn setup_fake_rcs(handle_stream: bool) -> RemoteControlProxy {
        let (proxy, mut stream) = create_proxy_and_stream::<RemoteControlMarker>().unwrap();

        if !handle_stream {
            return proxy;
        }

        fasync::Task::spawn(async move {
            while let Ok(req) = stream.try_next().await {
                match req {
                    Some(RemoteControlRequest::IdentifyHost { responder }) => {
                        let _ = responder
                            .send(&mut Ok(IdentifyHostResponse {
                                nodename: Some("".to_string()),
                                addresses: Some(vec![]),
                            }))
                            .unwrap();
                    }
                    _ => assert!(false),
                }
            }
        })
        .detach();

        proxy
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_handles_successful_response() -> Result<(), Error> {
        let rcs_proxy = setup_fake_rcs(true);
        assert!(send_request(rcs_proxy).await.is_ok());
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_handles_failed_response() -> Result<(), Error> {
        let rcs_proxy = setup_fake_rcs(false);
        assert!(send_request(rcs_proxy).await.is_err());
        Ok(())
    }
}
