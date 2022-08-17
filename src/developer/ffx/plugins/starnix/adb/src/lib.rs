// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Result},
    async_net::{TcpListener, TcpStream},
    ffx_core::ffx_plugin,
    ffx_starnix_adb_args::AdbStarnixCommand,
    fidl_fuchsia_starnix_developer::ManagerProxy,
    fuchsia_async as fasync,
    futures::io::AsyncReadExt,
    futures::stream::StreamExt,
    signal_hook::{consts::signal::SIGINT, iterator::Signals},
};

const ADB_DEFAULT_PORT: u32 = 5555;

async fn serve_adb_connection(mut stream: TcpStream, bridge_socket: fidl::Socket) -> Result<()> {
    let mut bridge = fidl::AsyncSocket::from_socket(bridge_socket)?;
    let (breader, mut bwriter) = (&mut bridge).split();
    let (sreader, mut swriter) = (&mut stream).split();

    let copy_futures = futures::future::try_join(
        futures::io::copy(breader, &mut swriter),
        futures::io::copy(sreader, &mut bwriter),
    );

    copy_futures.await?;

    Ok(())
}

#[ffx_plugin(
    "starnix_enabled",
    ManagerProxy = "core/starnix_manager:expose:fuchsia.starnix.developer.Manager"
)]
pub async fn adb_starnix(manager_proxy: ManagerProxy, command: AdbStarnixCommand) -> Result<()> {
    println!("adb_starnix - listening");

    let mut signals = Signals::new(&[SIGINT]).unwrap();
    let handle = signals.handle();
    let thread = std::thread::spawn(move || {
        for signal in signals.forever() {
            match signal {
                SIGINT => {
                    eprintln!("Caught interrupt. Shutting down starnix adb bridge...");
                    std::process::exit(0);
                }
                _ => unreachable!(),
            }
        }
    });

    let address = "127.0.0.1:5556";
    let listener = TcpListener::bind(address).await.expect("cannot bind to adb address");
    println!("The adb bridge is listening on {}", address);
    println!("To connect: adb connect {}", address);
    while let Some(stream) = listener.incoming().next().await {
        let stream = stream?;
        let (sbridge, cbridge) =
            fidl::Socket::create(fidl::SocketOpts::STREAM).context("failed to create socket")?;

        manager_proxy
            .vsock_connect(&command.galaxy, ADB_DEFAULT_PORT, sbridge)
            .map_err(|e| anyhow!("Error connecting to adbd: {:?}", e))?;

        fasync::Task::spawn(async move {
            serve_adb_connection(stream, cbridge)
                .await
                .unwrap_or_else(|e| println!("serve_adb_connection returned with {:?}", e));
        })
        .detach();
    }

    handle.close();
    thread.join().expect("signal thread to shutdown without panic");
    Ok(())
}

#[cfg(test)]
mod test {
    use super::*;
    use futures::AsyncWriteExt;

    async fn run_connection(listener: TcpListener, socket: fidl::Socket) {
        if let Some(stream) = listener.incoming().next().await {
            let stream = stream.unwrap();
            serve_adb_connection(stream, socket).await.unwrap();
        } else {
            panic!("did not get a connection");
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_adb_relay() {
        let any_local_address = "127.0.0.1:0";
        let listener = TcpListener::bind(any_local_address).await.unwrap();
        let local_address = listener.local_addr().unwrap();

        let port = local_address.port();

        let (sbridge, cbridge) = fidl::Socket::create(fidl::SocketOpts::STREAM).unwrap();

        fasync::Task::spawn(async move {
            run_connection(listener, sbridge).await;
        })
        .detach();

        let connect_address = format!("127.0.0.1:{}", port);
        let mut stream = TcpStream::connect(connect_address).await.unwrap();

        let test_data_1: Vec<u8> = vec![1, 2, 3, 4, 5];
        stream.write_all(&test_data_1).await.unwrap();

        let mut buf = [0u8; 64];
        let mut async_socket = fidl::AsyncSocket::from_socket(cbridge).unwrap();
        let bytes_read = async_socket.read(&mut buf).await.unwrap();
        assert_eq!(test_data_1.len(), bytes_read);
        for (a, b) in test_data_1.iter().zip(buf[..bytes_read].iter()) {
            assert_eq!(a, b);
        }

        let test_data_2: Vec<u8> = vec![6, 7, 8, 9, 10, 11];
        let bytes_written = async_socket.write(&test_data_2).await.unwrap();
        assert_eq!(bytes_written, test_data_2.len());

        let mut buf = [0u8; 64];
        let bytes_read = stream.read(&mut buf).await.unwrap();
        assert_eq!(bytes_written, bytes_written);

        for (a, b) in test_data_2.iter().zip(buf[..bytes_read].iter()) {
            assert_eq!(a, b);
        }
    }
}
