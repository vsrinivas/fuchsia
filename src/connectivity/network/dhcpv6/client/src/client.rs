// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context as _, Result},
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_net as fnet,
    fidl_fuchsia_net_dhcpv6::{
        ClientMarker, ClientRequest, ClientWatchServersResponder, NewClientParams,
        OperationalModels, DEFAULT_CLIENT_PORT,
    },
    fuchsia_zircon as zx,
    futures::{StreamExt as _, TryStreamExt as _},
    std::net::Ipv6Addr,
};

/// A DHCPv6 client.
#[derive(Default)]
pub(crate) struct Dhcpv6Client {
    dns_responder: Option<ClientWatchServersResponder>,
}

/// Returns `true` if the input address is a link-local address (`fe80::/64`).
///
/// TODO(https://github.com/rust-lang/rust/issues/27709): use is_unicast_link_local_strict() in
/// stable rust when it's available.
fn is_unicast_link_local_strict(addr: &fnet::Ipv6Address) -> bool {
    addr.addr[..8] == [0xfe, 0x80, 0, 0, 0, 0, 0, 0]
}

/// Starts a client based on `params`.
///
/// `request_stream` will be serviced by the client.
pub(crate) async fn start_client(
    params: NewClientParams,
    request: ServerEnd<ClientMarker>,
) -> Result<()> {
    if let NewClientParams {
        interface_id: Some(interface_id),
        address: Some(address),
        models: Some(_models),
    } = params
    {
        if Ipv6Addr::from(address.address.addr).is_multicast()
            || (is_unicast_link_local_strict(&address.address)
                && address.zone_index != interface_id)
        {
            return request
                .close_with_epitaph(zx::Status::INVALID_ARGS)
                .context("closing request channel with epitaph");
        }

        // TODO(jayzhuang): handle socket recv and timer.
        request
            .into_stream()
            .context("getting new client request stream from channel")?
            .map(|res| res.context("reading client request from stream"))
            .try_fold(Dhcpv6Client::default(), |mut client, request| {
                async move {
                    match request {
                        ClientRequest::WatchServers { responder } => match client.dns_responder {
                            Some(_) => {
                                // Drop the previous responder to close the channel.
                                client.dns_responder = None;
                                return Err(anyhow!(
                                    "got watch request while the previous one is pending"
                                ));
                            }
                            None => client.dns_responder = Some(responder),
                        },
                    }
                    Ok(client)
                }
            })
            .await
            .map(|_: Dhcpv6Client| ())
    } else {
        // All param fields are required.
        request
            .close_with_epitaph(zx::Status::INVALID_ARGS)
            .context("closing request channel with epitaph")
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl::endpoints::create_endpoints,
        fuchsia_async as fasync,
        futures::join,
        matches::assert_matches,
        net_declare::{fidl_ip_v6, fidl_socket_addr_v6},
        std::task::Poll,
    };

    #[fasync::run_singlethreaded(test)]
    async fn test_client_should_return_error_on_double_watch() {
        let (client_end, server_end) =
            create_endpoints::<ClientMarker>().expect("failed to create test fidl channel");
        let client_proxy = client_end.into_proxy().expect("failed to create test client proxy");

        let (caller1_res, caller2_res, client_res) = join!(
            client_proxy.watch_servers(),
            client_proxy.watch_servers(),
            start_client(
                NewClientParams {
                    interface_id: Some(1),
                    address: Some(fidl_socket_addr_v6!([fe01::1:2]:546)),
                    models: Some(OperationalModels { stateless: None }),
                },
                server_end,
            )
        );

        assert_matches!(
            caller1_res,
            Err(fidl::Error::ClientChannelClosed(zx::Status::PEER_CLOSED))
        );
        assert_matches!(
            caller2_res,
            Err(fidl::Error::ClientChannelClosed(zx::Status::PEER_CLOSED))
        );
        assert!(client_res
            .expect_err("client should fail with double watch error")
            .to_string()
            .contains("got watch request while the previous one is pending"));
    }

    #[test]
    fn test_client_starts_with_valid_args() {
        let mut exec = fasync::Executor::new().expect("failed to create test executor");

        let (client_end, server_end) =
            create_endpoints::<ClientMarker>().expect("failed to create test fidl channel");
        let client_proxy = client_end.into_proxy().expect("failed to create test client proxy");

        let test_fut = async {
            join!(
                client_proxy.watch_servers(),
                start_client(
                    NewClientParams {
                        interface_id: Some(1),
                        address: Some(fidl_socket_addr_v6!([fe01::1:2]:546)),
                        models: Some(OperationalModels { stateless: None }),
                    },
                    server_end
                )
            )
        };
        futures::pin_mut!(test_fut);
        assert_matches!(exec.run_until_stalled(&mut test_fut), Poll::Pending);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_client_fails_to_start_with_invalid_args() {
        for params in vec![
            // Missing required field.
            NewClientParams {
                interface_id: Some(1),
                address: None,
                models: Some(OperationalModels { stateless: None }),
            },
            // Interface ID and zone index mismatch on link-local address.
            NewClientParams {
                interface_id: Some(2),
                address: Some(fnet::Ipv6SocketAddress {
                    address: fidl_ip_v6!(fe80::1),
                    port: DEFAULT_CLIENT_PORT,
                    zone_index: 1,
                }),
                models: Some(OperationalModels { stateless: None }),
            },
            // Multicast address is invalid.
            NewClientParams {
                interface_id: Some(1),
                address: Some(fnet::Ipv6SocketAddress {
                    address: fidl_ip_v6!(ff01::1),
                    port: DEFAULT_CLIENT_PORT,
                    zone_index: 1,
                }),
                models: Some(OperationalModels { stateless: None }),
            },
        ] {
            let (client_end, server_end) =
                create_endpoints::<ClientMarker>().expect("failed to create test fidl channel");
            let () =
                start_client(params, server_end).await.expect("start server failed unexpectedly");
            // Calling any function on the client proxy should fail due to channel closed with
            // `INVALID_ARGS`.
            assert_matches!(
                client_end.into_proxy().expect("failed to create test proxy").watch_servers().await,
                Err(fidl::Error::ClientChannelClosed(zx::Status::INVALID_ARGS))
            );
        }
    }

    #[test]
    fn test_is_unicast_link_local_strict() {
        assert_eq!(is_unicast_link_local_strict(&fidl_ip_v6!(fe80::)), true);
        assert_eq!(is_unicast_link_local_strict(&fidl_ip_v6!(fe80::1)), true);
        assert_eq!(is_unicast_link_local_strict(&fidl_ip_v6!(fe80::ffff:1:2:3)), true);
        assert_eq!(is_unicast_link_local_strict(&fidl_ip_v6!(fe80::1:0:0:0:0)), false);
        assert_eq!(is_unicast_link_local_strict(&fidl_ip_v6!(fe81::)), false);
    }
}
