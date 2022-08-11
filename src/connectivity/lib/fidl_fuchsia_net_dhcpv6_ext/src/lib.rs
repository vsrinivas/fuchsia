// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(missing_docs)]

//! Extension types and helpers for the fuchsia.net.dhcpv6 FIDL library.

use futures::{future::Either, FutureExt as _};

/// Responses from watch methods on `fuchsia.net.dhcpv6/Client`.
#[derive(Debug)]
pub enum WatchItem {
    /// Return value of `fuchsia.net.dhcpv6/Client.WatchServers`.
    DnsServers(Vec<fidl_fuchsia_net_name::DnsServer_>),
    /// Return value of `fuchsia.net.dhcpv6/Client.WatchAddress`.
    Address {
        /// The address bits and prefix.
        addr: fidl_fuchsia_net::Subnet,
        /// Address parameters.
        parameters: fidl_fuchsia_net_interfaces_admin::AddressParameters,
        /// Server end of a `fuchsia.net.interfaces.admin/AddressStateProvider`
        /// protocol channel.
        address_state_provider_server_end: fidl::endpoints::ServerEnd<
            fidl_fuchsia_net_interfaces_admin::AddressStateProviderMarker,
        >,
    },
}

impl WatchItem {
    /// Constructs a new [`WatchItem::Address`].
    pub fn new_address(
        addr: fidl_fuchsia_net::Subnet,
        parameters: fidl_fuchsia_net_interfaces_admin::AddressParameters,
        address_state_provider_server_end: fidl::endpoints::ServerEnd<
            fidl_fuchsia_net_interfaces_admin::AddressStateProviderMarker,
        >,
    ) -> Self {
        Self::Address { addr, parameters, address_state_provider_server_end }
    }
}

/// Turns a [`fidl_fuchsia_net_dhcpv6::ClientProxy`] into a stream of items
/// yielded by calling all hanging get methods on the protocol.
///
/// [`fidl_fuchsia_net_dhcpv6::ClientProxy::watch_servers`] and
/// [`fidl_fuchsia_net_dhcpv6::ClientProxy::watch_address`] must never be
/// called on the protocol channel `client_proxy` belongs to once this function
/// returns until the stream ends or returns an error, as only one pending call
/// is allowed at a time.
pub fn into_watch_stream(
    client_proxy: fidl_fuchsia_net_dhcpv6::ClientProxy,
) -> impl futures::Stream<Item = Result<WatchItem, fidl::Error>> + Unpin {
    let watch_servers_fut = client_proxy.watch_servers();
    let watch_address_fut = client_proxy.watch_address();
    futures::stream::try_unfold(
        (client_proxy, watch_servers_fut, watch_address_fut),
        |(client_proxy, watch_servers_fut, watch_address_fut)| {
            futures::future::select(watch_servers_fut, watch_address_fut).map(|either| {
                match either {
                    Either::Left((servers_res, watch_address_fut)) => servers_res.map(|servers| {
                        let watch_servers_fut = client_proxy.watch_servers();
                        Some((
                            WatchItem::DnsServers(servers),
                            (client_proxy, watch_servers_fut, watch_address_fut),
                        ))
                    }),
                    Either::Right((address_res, watch_servers_fut)) => {
                        address_res.map(|(addr, parameters, address_state_provider_server_end)| {
                            let watch_address_fut = client_proxy.watch_address();
                            Some((
                                WatchItem::new_address(
                                    addr,
                                    parameters,
                                    address_state_provider_server_end,
                                ),
                                (client_proxy, watch_servers_fut, watch_address_fut),
                            ))
                        })
                    }
                }
                .or_else(|e| if e.is_closed() { Ok(None) } else { Err(e) })
            })
        },
    )
}

#[cfg(test)]
mod tests {
    use super::{into_watch_stream, WatchItem};

    use assert_matches::assert_matches;
    use futures::{StreamExt as _, TryStreamExt as _};
    use test_case::test_case;

    #[derive(Debug, Clone, Copy)]
    enum WatchType {
        DnsServers,
        Address,
    }

    const SUBNET: fidl_fuchsia_net::Subnet = net_declare::fidl_subnet!("abcd::1/128");

    /// Run a fake server which reads requests from `request_stream` and
    /// makes responses in the order as given in `response_types`.
    async fn run_fake_server(
        request_stream: &mut fidl_fuchsia_net_dhcpv6::ClientRequestStream,
        response_types: &[WatchType],
    ) {
        let (_, _, _): (
            &mut fidl_fuchsia_net_dhcpv6::ClientRequestStream,
            Option<fidl_fuchsia_net_dhcpv6::ClientWatchServersResponder>,
            Option<fidl_fuchsia_net_dhcpv6::ClientWatchAddressResponder>,
        ) = futures::stream::iter(response_types)
            .fold(
                (request_stream, None, None),
                |(request_stream, mut dns_servers_responder, mut address_responder),
                 watch_type_to_unblock| async move {
                    while dns_servers_responder.is_none() || address_responder.is_none() {
                        match request_stream
                            .try_next()
                            .await
                            .expect("FIDL error")
                            .expect("request stream ended")
                        {
                            fidl_fuchsia_net_dhcpv6::ClientRequest::WatchServers { responder } => {
                                assert_matches!(dns_servers_responder.replace(responder), None);
                            }
                            fidl_fuchsia_net_dhcpv6::ClientRequest::WatchAddress { responder } => {
                                assert_matches!(address_responder.replace(responder), None);
                            }
                            fidl_fuchsia_net_dhcpv6::ClientRequest::Shutdown { responder: _ } => {
                                panic!("Shutdown method should not be called");
                            }
                        }
                    }
                    match watch_type_to_unblock {
                        WatchType::Address => {
                            let (_, server_end) = fidl::endpoints::create_endpoints::<
                                fidl_fuchsia_net_interfaces_admin::AddressStateProviderMarker,
                            >()
                            .expect("create endpoints");
                            let mut subnet = SUBNET;
                            address_responder
                                .take()
                                .expect("must have address responder")
                                .send(
                                    &mut subnet,
                                    fidl_fuchsia_net_interfaces_admin::AddressParameters::EMPTY,
                                    server_end,
                                )
                                .expect("FIDL error");
                        }
                        WatchType::DnsServers => {
                            dns_servers_responder
                                .take()
                                .expect("must have DNS servers responder")
                                .send(&mut std::iter::empty())
                                .expect("FIDL error");
                        }
                    };
                    (request_stream, dns_servers_responder, address_responder)
                },
            )
            .await;
    }

    /// Tests that polling the watcher stream causes all hanging get methods
    /// to be called, and the items yielded by the stream are in the order
    /// as the fake server is instructed to unblock the calls.
    #[test_case(&[WatchType::DnsServers, WatchType::DnsServers]; "dns_servers")]
    #[test_case(&[WatchType::Address, WatchType::Address]; "address")]
    #[test_case(&[WatchType::DnsServers, WatchType::Address]; "dns_servers_then_address")]
    #[test_case(&[WatchType::Address, WatchType::DnsServers]; "address_then_dns_servers")]
    #[fuchsia_async::run_singlethreaded(test)]
    async fn watch_stream(watch_types: &[WatchType]) {
        let (client_proxy, mut request_stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_fuchsia_net_dhcpv6::ClientMarker>()
                .expect("create fuchsia.net.dhcpv6/Client proxy and stream");
        let mut watch_stream = into_watch_stream(client_proxy);
        let client_fut = watch_stream.by_ref().take(watch_types.len()).try_collect::<Vec<_>>();
        let (r, ()) = futures::future::join(
            client_fut,
            run_fake_server(request_stream.by_ref(), watch_types),
        )
        .await;
        let items = r.expect("watch stream error");
        assert_eq!(items.len(), watch_types.len());
        for (item, watch_type) in items.into_iter().zip(watch_types) {
            match watch_type {
                WatchType::Address => {
                    assert_matches!(
                        item,
                        WatchItem::Address {
                            addr,
                            parameters: fidl_fuchsia_net_interfaces_admin::AddressParameters {
                                initial_properties: None,
                                temporary: None,
                                ..
                            },
                            address_state_provider_server_end: _,
                        } if addr == SUBNET
                    );
                }
                WatchType::DnsServers => {
                    assert_matches!(
                        item,
                        WatchItem::DnsServers(dns_servers) if dns_servers.is_empty()
                    );
                }
            }
        }

        drop(request_stream);
        assert_matches!(
            watch_stream.try_collect::<Vec<_>>().await.expect("watch stream error").as_slice(),
            &[]
        );
    }
}
