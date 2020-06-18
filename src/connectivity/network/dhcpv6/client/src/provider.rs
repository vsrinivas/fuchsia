// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Result},
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_net_dhcpv6::{
        ClientMarker, ClientProviderRequest, ClientProviderRequestStream, NewClientParams,
    },
    futures::{Future, StreamExt as _, TryStreamExt as _},
};

/// Handles client provider requests from the input stream.
pub(crate) async fn run_client_provider<Fut, F>(
    stream: ClientProviderRequestStream,
    start_client: F,
) -> Result<()>
where
    Fut: Future<Output = Result<()>>,
    F: Fn(NewClientParams, ServerEnd<ClientMarker>) -> Fut,
{
    stream
        .map(|res| res.context("reading client provider request from stream"))
        .try_for_each_concurrent(None, |request| async {
            match request {
                ClientProviderRequest::NewClient { params, request, control_handle: _ } => {
                    start_client(params, request).await
                }
            }
        })
        .await
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        anyhow::{anyhow, Error},
        fidl::endpoints::create_endpoints,
        fidl_fuchsia_net_dhcpv6::{ClientProviderMarker, OperationalModels},
        fuchsia_async as fasync,
        futures::{join, try_join},
        net_declare::fidl_socket_addr_v6,
    };

    async fn start_client(
        _param: NewClientParams,
        _request: ServerEnd<ClientMarker>,
    ) -> Result<()> {
        Ok(())
    }

    async fn start_err_client(
        _param: NewClientParams,
        _request: ServerEnd<ClientMarker>,
    ) -> Result<()> {
        Err(anyhow!("fake test error"))
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_client_provider() {
        let (client_end, server_end) =
            create_endpoints::<ClientProviderMarker>().expect("failed to create test fidl channel");
        let client_provider_proxy =
            client_end.into_proxy().expect("failed to create test client proxy");
        let client_provider_stream =
            server_end.into_stream().expect("failed to create test request stream");

        let test_fut = async {
            for interface_id in 0..10 {
                let (_client_end, server_end) =
                    create_endpoints::<ClientMarker>().expect("failed to create test fidl channel");
                client_provider_proxy
                    .new_client(
                        NewClientParams {
                            interface_id: Some(interface_id),
                            address: Some(fidl_socket_addr_v6!([fe01::1:2]:546)),
                            models: Some(OperationalModels { stateless: None }),
                        },
                        server_end,
                    )
                    .expect("failed to request new client");
            }
            drop(client_provider_proxy);
            Ok(())
        };
        let provider_fut = run_client_provider(client_provider_stream, start_client);

        try_join!(test_fut, provider_fut).expect("client provider failed unexpectly");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_client_provider_error_propagation() {
        let (client_end, server_end) =
            create_endpoints::<ClientProviderMarker>().expect("failed to create test fidl channel");
        let client_provider_proxy =
            client_end.into_proxy().expect("failed to create test client proxy");
        let client_provider_stream =
            server_end.into_stream().expect("failed to create test request stream");

        let test_fut = async {
            let (_client_end, server_end) = create_endpoints::<ClientMarker>()?;
            client_provider_proxy.new_client(
                NewClientParams {
                    interface_id: Some(1),
                    address: Some(fidl_socket_addr_v6!([fe01::1:2]:546)),
                    models: Some(OperationalModels { stateless: None }),
                },
                server_end,
            )?;
            drop(client_provider_proxy);
            Ok::<_, Error>(())
        };
        let provider_fut = run_client_provider(client_provider_stream, start_err_client);

        let (test_fut_res, provider_fut_res) = join!(test_fut, provider_fut);
        test_fut_res.expect("test future should succeed");
        assert_eq!(
            provider_fut_res
                .expect_err("provider should propagate error from DHCPv6 client")
                .to_string(),
            "fake test error"
        );
    }
}
