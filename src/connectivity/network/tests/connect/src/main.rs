// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::convert::TryInto;
use std::os::unix::io::AsRawFd;

use anyhow::Context as _;
use fuchsia_zircon as zx;
use futures::io::{AsyncReadExt, AsyncWriteExt};
use futures::stream::{StreamExt, TryStreamExt};
use net2::TcpStreamExt;

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

    fuchsia_syslog::init_with_tags(&[NAME])?;

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

            println!("Connecting keepalive and retransmit sockets...");
            let keepalive_timeout = fuchsia_async::net::TcpStream::connect(sockaddr)?.await?;
            println!("Connected keepalive.");
            let mut retransmit_timeout = fuchsia_async::net::TcpStream::connect(sockaddr)?.await?;
            println!("Connected retransmit.");

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

            // Start the keepalive machinery.
            let () = keepalive_timeout.std().set_keepalive_ms(Some(0))?;
            // TODO(tamird): use upstream's setters when
            // https://github.com/rust-lang-nursery/net2-rs/issues/90 is fixed.
            //
            // TODO(tamird): use into_iter after
            // https://github.com/rust-lang/rust/issues/25725.
            println!("Setting keepalive options.");
            for name in [libc::TCP_KEEPCNT, libc::TCP_KEEPINTVL].iter().cloned() {
                let value = 1u32;
                // This is safe because `setsockopt` does not retain memory passed to it.
                if unsafe {
                    libc::setsockopt(
                        keepalive_timeout.std().as_raw_fd(),
                        libc::IPPROTO_TCP,
                        name,
                        &value as *const _ as *const libc::c_void,
                        std::mem::size_of_val(&value).try_into().unwrap(),
                    )
                } != 0
                {
                    let () = Err(std::io::Error::last_os_error())?;
                }
            }

            // Start the retransmit machinery.
            let payload = [0xde; 1];
            let n = retransmit_timeout.write(&payload).await?;
            if n != payload.len() {
                let () = Err(anyhow::format_err!("wrote {}/{} bytes", n, payload.len()))?;
            }

            let keepalive_timeout = verify_broken_pipe(keepalive_timeout);
            let retransmit_timeout = verify_broken_pipe(retransmit_timeout);

            // TODO(tamird): revert this to futures::try_join!(...) when the timeouts can be
            // configured.
            let interval = std::time::Duration::from_secs(5);
            let timeout = std::time::Duration::from_secs(600);
            let periodically_emit = fuchsia_async::Interval::new(interval.into())
                .take((timeout.as_secs() / interval.as_secs()).try_into().unwrap())
                .for_each(|()| {
                    futures::future::ready(println!(
                        "still waiting for TCP timeouts! don't terminate me please"
                    ))
                });

            println!("Waiting for all three timeouts...");
            let timeouts =
                futures::future::try_join3(connect_timeout, keepalive_timeout, retransmit_timeout);
            futures::pin_mut!(timeouts);
            match futures::future::select(timeouts, periodically_emit).await {
                futures::future::Either::Left((timeouts, _logger)) => {
                    let ((), (), ()) = timeouts?;
                }
                futures::future::Either::Right(((), _timeouts)) => {
                    let () = Err(anyhow::format_err!("periodic logger completed unexpectedly"))?;
                }
            };

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
