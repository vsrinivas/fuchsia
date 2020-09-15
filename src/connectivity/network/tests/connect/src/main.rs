// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Context as _;
use fuchsia_zircon as zx;
use futures::future::FutureExt as _;
use futures::io::{AsyncReadExt as _, AsyncWriteExt as _};
use futures::stream::{StreamExt as _, TryStreamExt as _};
use tcp_stream_ext::TcpStreamExt as _;

#[derive(argh::FromArgs)]
/// Adverse condition `connect` test.
struct TopLevel {
    #[argh(subcommand)]
    sub_command: SubCommand,
}

#[derive(argh::FromArgs)]
/// Connect to the remote address.
#[argh(subcommand, name = "client")]
struct Client {
    #[argh(option)]
    /// the remote address to connect to
    remote: std::net::Ipv4Addr,
}

#[derive(argh::FromArgs)]
/// Listen for incoming connections.
#[argh(subcommand, name = "server")]
struct Server {}

#[derive(argh::FromArgs)]
#[argh(subcommand)]
enum SubCommand {
    Client(Client),
    Server(Server),
}

const NAME: &str = "connect_test";

fn bus_subscribe(
    sync_manager: &fidl_fuchsia_netemul_sync::SyncManagerProxy,
    client_name: &str,
) -> Result<fidl_fuchsia_netemul_sync::BusProxy, fidl::Error> {
    let (client, server) = fidl::endpoints::create_proxy::<fidl_fuchsia_netemul_sync::BusMarker>()?;
    let () = sync_manager.bus_subscribe(NAME, client_name, server)?;
    Ok(client)
}

async fn measure<T>(
    fut: impl std::future::Future<Output = Result<(), T>>,
) -> Result<std::time::Duration, T> {
    let start = std::time::Instant::now();
    let () = fut.await?;
    Ok(start.elapsed())
}

async fn verify_broken_pipe(
    mut stream: fuchsia_async::net::TcpStream,
) -> Result<(), anyhow::Error> {
    let mut buf = [0xad; 1];
    let n = stream.read(&mut buf).await?;
    if n != 0 {
        let () = Err(anyhow::format_err!("read {}/{} bytes", n, 0))?;
    }
    match stream.write(&buf).await {
        Ok(n) => Err(anyhow::format_err!("unexpectedly wrote {} bytes", n)),
        Err(io_error) => {
            if io_error.kind() == std::io::ErrorKind::BrokenPipe {
                Ok(())
            } else {
                Err(anyhow::format_err!("unexpected error {}", io_error))
            }
        }
    }
}

#[fuchsia_async::run_singlethreaded]
async fn main() -> Result<(), anyhow::Error> {
    const CLIENT_NAME: &str = "client";
    const SERVER_NAME: &str = "server";
    const PORT: u16 = 80;

    let () = fuchsia_syslog::init().context("cannot init logger")?;

    let sync_manager = fuchsia_component::client::connect_to_service::<
        fidl_fuchsia_netemul_sync::SyncManagerMarker,
    >()?;

    let TopLevel { sub_command } = argh::from_env();

    match sub_command {
        SubCommand::Client(Client { remote }) => {
            println!("Running client.");
            let bus = bus_subscribe(&sync_manager, CLIENT_NAME)?;
            // Wait for the server to be attached to the event bus, meaning it's
            // already bound and listening.
            let _ = bus
                .wait_for_clients(
                    &mut Some(SERVER_NAME).into_iter(),
                    zx::Time::INFINITE.into_nanos(),
                )
                .await
                .context("Failed to observe server joining the bus")?;

            let sockaddr = std::net::SocketAddr::from((remote, PORT));

            println!("Connecting sockets...");

            let keepalive_timeout = fuchsia_async::net::TcpStream::connect(sockaddr)?.await?;
            println!("Connected keepalive.");
            let keepalive_usertimeout = fuchsia_async::net::TcpStream::connect(sockaddr)?.await?;
            println!("Connected keepalive_user.");
            let mut retransmit_timeout = fuchsia_async::net::TcpStream::connect(sockaddr)?.await?;
            println!("Connected retransmit.");
            let mut retransmit_usertimeout =
                fuchsia_async::net::TcpStream::connect(sockaddr)?.await?;
            println!("Connected retransmit_user.");

            println!("Connected all sockets.");

            // Now that we have our connections, partition the network.
            {
                let network_context = fuchsia_component::client::connect_to_service::<
                    fidl_fuchsia_netemul_network::NetworkContextMarker,
                >()?;

                let network_manager = {
                    let (client, server) = fidl::endpoints::create_proxy::<
                        fidl_fuchsia_netemul_network::NetworkManagerMarker,
                    >()?;
                    let () = network_context.get_network_manager(server)?;
                    client
                };

                let network = network_manager
                    .get_network("net")
                    .await?
                    .ok_or(anyhow::format_err!("failed to get network"))?
                    .into_proxy()?;
                let status = network
                    .set_config(fidl_fuchsia_netemul_network::NetworkConfig {
                        latency: None,
                        packet_loss: Some(fidl_fuchsia_netemul_network::LossConfig::RandomRate(
                            100,
                        )),
                        reorder: None,
                    })
                    .await?;
                let () = fuchsia_zircon::ok(status)?;
            }
            println!("Network partitioned.");

            let connect_timeout = fuchsia_async::net::TcpStream::connect(sockaddr)?;
            let connect_timeout = async {
                match connect_timeout.await {
                    Ok(_stream) => Err(anyhow::format_err!("unexpectedly connected")),
                    Err(io_error) => {
                        if io_error.kind() == std::io::ErrorKind::TimedOut {
                            Ok(())
                        } else {
                            Err(anyhow::format_err!("unexpected error {}", io_error))
                        }
                    }
                }
            };

            let usertimeout = std::time::Duration::from_secs(1);
            for socket in [&keepalive_timeout, &keepalive_usertimeout].iter() {
                socket.std().set_user_timeout(usertimeout)?;
            }

            // Start the keepalive machinery.
            //
            // set_keepalive sets TCP_KEEPIDLE, which requires a minimum of 1 second.
            let keepalive_idle = std::time::Duration::from_secs(1);
            for socket in [&keepalive_timeout, &keepalive_usertimeout].iter() {
                let () = socket.std().set_keepalive(Some(keepalive_idle))?;
            }
            let keepalive_count: i32 = 1;
            keepalive_timeout.std().set_keepalive_count(keepalive_count)?;
            let keepalive_interval = std::time::Duration::from_secs(1);
            keepalive_timeout.std().set_keepalive_interval(keepalive_interval)?;

            // Start the retransmit machinery.
            for socket in [&mut retransmit_timeout, &mut retransmit_usertimeout].iter_mut() {
                let () = socket.write_all(&[0xde]).await?;
            }

            let connect_timeout = measure(connect_timeout).fuse();
            futures::pin_mut!(connect_timeout);
            let keepalive_timeout = measure(verify_broken_pipe(keepalive_timeout)).fuse();
            futures::pin_mut!(keepalive_timeout);
            let keepalive_usertimeout = measure(verify_broken_pipe(keepalive_usertimeout)).fuse();
            futures::pin_mut!(keepalive_usertimeout);
            let retransmit_timeout = measure(verify_broken_pipe(retransmit_timeout)).fuse();
            futures::pin_mut!(retransmit_timeout);
            let retransmit_usertimeout = measure(verify_broken_pipe(retransmit_usertimeout)).fuse();
            futures::pin_mut!(retransmit_usertimeout);

            macro_rules! try_print_elapsed {
                ($val:expr) => {
                    let v = $val.context(stringify!($val))?;
                    println!("{} timed out after {:?}", stringify!($val), v);
                };
            }

            println!("Waiting for all timeouts...");
            loop {
                futures::select! {
                  connect = connect_timeout => {
                    try_print_elapsed!(connect);
                  },
                  keepalive = keepalive_timeout => {
                    try_print_elapsed!(keepalive);
                  },
                  keepalive_user = keepalive_usertimeout => {
                    try_print_elapsed!(keepalive_user);
                  },
                  // TODO(fxb/52278): Enable retransmit timeout test,
                  // after we are able to tune the TCP stack to reduce
                  // this time. Currently it is too long for the test.
                  //
                  // retransmit = retransmit_timeout => {
                  //   try_print_elapsed!(retransmit);
                  // },
                  // retransmit_user = retransmit_usertimeout => {
                  //   try_print_elapsed!(retransmit_user);
                  // },
                  complete => break,
                }
            }
            println!("All timeouts complete.");

            Ok(())
        }
        SubCommand::Server(Server {}) => {
            println!("Starting server...");
            let _listener = std::net::TcpListener::bind(&std::net::SocketAddr::from((
                std::net::Ipv4Addr::UNSPECIFIED,
                PORT,
            )))?;
            println!("Server bound.");
            let bus = bus_subscribe(&sync_manager, SERVER_NAME)?;
            let stream = bus.take_event_stream().try_filter_map(|event| async move {
                Ok(match event {
                    fidl_fuchsia_netemul_sync::BusEvent::OnClientDetached { client } => {
                        match client.as_str() {
                            CLIENT_NAME => Some(()),
                            _client => None,
                        }
                    }
                    fidl_fuchsia_netemul_sync::BusEvent::OnBusData { data: _ }
                    | fidl_fuchsia_netemul_sync::BusEvent::OnClientAttached { client: _ } => None,
                })
            });
            println!("Waiting for client to detach.");
            futures::pin_mut!(stream);
            let () = stream
                .next()
                .await
                .ok_or(anyhow::format_err!("stream ended before client detached"))??;
            Ok(())
        }
    }
}
