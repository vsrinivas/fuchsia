// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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
) -> Result<(), failure::Error> {
    let mut buf = [0xad; 1];
    let n = stream.read(&mut buf).await?;
    if n != 0 {
        return Err(failure::format_err!("read {}/{} bytes", n, 0))?;
    }
    match stream.write(&buf).await {
        Ok(n) => Err(failure::format_err!("unexpectedly wrote {} bytes", n)),
        Err(io_error) => {
            if io_error.kind() == std::io::ErrorKind::BrokenPipe {
                Ok(())
            } else {
                Err(failure::format_err!("unexpected error {}", io_error))
            }
        }
    }
}

#[fuchsia_async::run_singlethreaded]
async fn main() -> Result<(), failure::Error> {
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
            let bus = bus_subscribe(&sync_manager, CLIENT_NAME)?;
            let stream = bus.take_event_stream().try_filter_map(|event| {
                async move {
                    Ok(match event {
                        fidl_fuchsia_netemul_sync::BusEvent::OnClientAttached { client } => {
                            match client.as_str() {
                                SERVER_NAME => Some(()),
                                _client => None,
                            }
                        }
                        fidl_fuchsia_netemul_sync::BusEvent::OnBusData { data: _ }
                        | fidl_fuchsia_netemul_sync::BusEvent::OnClientDetached { client: _ } => {
                            None
                        }
                    })
                }
            });
            futures::pin_mut!(stream);
            let () = stream
                .next()
                .await
                .ok_or(failure::err_msg("stream ended before server attached"))??;

            let sockaddr = std::net::SocketAddr::from((remote, PORT));

            let keepalive_timeout = fuchsia_async::net::TcpStream::connect(sockaddr)?.await?;
            let mut retransmit_timeout = fuchsia_async::net::TcpStream::connect(sockaddr)?.await?;

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
                    .ok_or(failure::err_msg("failed to get network"))?
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

            let connect_timeout = fuchsia_async::net::TcpStream::connect(sockaddr)?;
            let connect_timeout = async {
                match connect_timeout.await {
                    Ok(_stream) => Err(failure::err_msg("unexpectedly connected")),
                    Err(io_error) => {
                        if io_error.kind() == std::io::ErrorKind::TimedOut {
                            Ok(())
                        } else {
                            Err(failure::format_err!("unexpected error {}", io_error))
                        }
                    }
                }
            };

            // Start the keepalive machinery.
            let () = keepalive_timeout.std().set_keepalive_ms(Some(0))?;

            // Start the retransmit machinery.
            let () = retransmit_timeout.std().set_send_buffer_size(1)?;
            let size = retransmit_timeout.std().send_buffer_size()?;
            let mut payload = Vec::with_capacity(size);
            let () = payload.resize(size, 0xde);
            let n = retransmit_timeout.write(&payload).await?;
            if n != payload.len() {
                return Err(failure::format_err!("wrote {}/{} bytes", n, payload.len()))?;
            }

            // TCP_KEEP{CNT,INVTL} aren't supported, so this can't complete in a reasonable amount
            // of time. See https://github.com/rust-lang-nursery/net2-rs/issues/90.
            //
            // At the time of writing, the defaults are TCP_KEEPCNT=9, TCP_KEEPINVTL=75s.
            //
            // TODO(tamird): when the above is fixed, include this in the try_join! below. This
            // will require keepalive_timeout to be made `mut`.
            let _ = verify_broken_pipe(keepalive_timeout);

            let retransmit_timeout = verify_broken_pipe(retransmit_timeout);

            let ((), ()) = futures::try_join!(connect_timeout, retransmit_timeout)?;
            Ok(())
        }
        SubCommand::Server(Server {}) => {
            let _listener = std::net::TcpListener::bind(&std::net::SocketAddr::from((
                std::net::Ipv4Addr::UNSPECIFIED,
                PORT,
            )))?;

            let bus = bus_subscribe(&sync_manager, SERVER_NAME)?;
            let stream = bus.take_event_stream().try_filter_map(|event| {
                async move {
                    Ok(match event {
                        fidl_fuchsia_netemul_sync::BusEvent::OnClientDetached { client } => {
                            match client.as_str() {
                                CLIENT_NAME => Some(()),
                                _client => None,
                            }
                        }
                        fidl_fuchsia_netemul_sync::BusEvent::OnBusData { data: _ }
                        | fidl_fuchsia_netemul_sync::BusEvent::OnClientAttached { client: _ } => {
                            None
                        }
                    })
                }
            });
            futures::pin_mut!(stream);
            let () = stream
                .next()
                .await
                .ok_or(failure::err_msg("stream ended before client detached"))??;
            Ok(())
        }
    }
}
