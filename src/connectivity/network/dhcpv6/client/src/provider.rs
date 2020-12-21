// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_net_dhcpv6::{
        ClientMarker, ClientProviderRequest, ClientProviderRequestStream, NewClientParams,
    },
    futures::{Future, StreamExt as _},
};

/// Handles client provider requests from the input stream.
pub(crate) async fn run_client_provider<Fut, F>(
    stream: ClientProviderRequestStream,
    serve_client: F,
) where
    Fut: Future<Output = Result<()>>,
    F: Fn(NewClientParams, ServerEnd<ClientMarker>) -> Fut,
{
    stream
        .for_each_concurrent(None, |request| async {
            match request {
                Ok(ClientProviderRequest::NewClient { params, request, control_handle: _ }) => {
                    // `NewClientParams` does not implement `Clone`. It is also non-trivial to pass
                    // a reference of `params` to `serve_client` because that would require adding
                    // lifetimes in quite a few places.
                    let params_str = format!("{:?}", params);
                    let () =
                        serve_client(params, request).await.unwrap_or_else(|e: anyhow::Error| {
                            log::error!("error running client with params {}: {}", params_str, e)
                        });
                }
                Err(e) => log::warn!("client provider request FIDL error: {}", e),
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
        futures::join,
        matches::assert_matches,
        net_declare::fidl_socket_addr_v6,
    };

    async fn serve_client(
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

    async fn test_client_provider<Fut, F>(serve_client: F)
    where
        Fut: Future<Output = Result<()>>,
        F: Fn(NewClientParams, ServerEnd<ClientMarker>) -> Fut,
    {
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
                            address: Some(fidl_socket_addr_v6!("[fe01::1:2]:546")),
                            models: Some(OperationalModels {
                                stateless: None,
                                ..OperationalModels::EMPTY
                            }),
                            ..NewClientParams::EMPTY
                        },
                        server_end,
                    )
                    .expect("failed to request new client");
            }
            drop(client_provider_proxy);
            Ok(())
        };
        let provider_fut = run_client_provider(client_provider_stream, serve_client);

        let (test_res, ()): (Result<_, Error>, ()) = join!(test_fut, provider_fut);
        assert_matches!(test_res, Ok(()));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_client_provider_serve_client_success() {
        let () = test_client_provider(serve_client).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_client_provider_should_keep_running_on_client_err() {
        let () = test_client_provider(start_err_client).await;
    }
}
