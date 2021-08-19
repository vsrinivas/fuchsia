// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    fidl_fuchsia_bluetooth_hfp::{CallManagerProxy, HfpRequest, HfpRequestStream},
    fidl_fuchsia_bluetooth_hfp_test::{HfpTestRequest, HfpTestRequestStream},
    fuchsia_component::server::{ServiceFs, ServiceObj},
    futures::{channel::mpsc::Sender, FutureExt, SinkExt, StreamExt, TryStreamExt},
    tracing::info,
};

/// The maximum number of fidl service client connections that will be serviced concurrently.
const MAX_CONCURRENT_CONNECTIONS: usize = 10;

/// All FIDL services that are exposed by this component's ServiceFs.
pub enum Services {
    Hfp(HfpRequestStream),
    HfpTest(HfpTestRequestStream),
}

/// Handle a new incoming Hfp protocol client connection. This async function completes when
/// a client connection is closed.
async fn handle_hfp_client_connection(
    stream: HfpRequestStream,
    call_manager: Sender<CallManagerProxy>,
) {
    info!("New HFP client connection");
    if let Err(e) = handle_hfp_client_connection_result(stream, call_manager).await {
        // An error processing client provided parameters is not a fatal error.
        // Log and return.
        info!("HFP FIDL client error: {}", e);
    }
}

/// Handle all requests made over `stream`.
///
/// Returns an Err if the stream closes with an error or the client sends an invalid request.
async fn handle_hfp_client_connection_result(
    mut stream: HfpRequestStream,
    mut call_manager: Sender<CallManagerProxy>,
) -> Result<(), Error> {
    while let Some(request) = stream.try_next().await.context("hfp FIDL client error")? {
        let HfpRequest::Register { manager, .. } = request;
        info!("Registering call manager");
        let proxy = manager.into_proxy().context("hfp FIDL client error")?;
        call_manager.send(proxy).await.context("component main loop halted")?;
    }
    Ok(())
}

async fn handle_hfp_test_client_connection(
    mut stream: HfpTestRequestStream,
    mut test_requests: Sender<HfpTestRequest>,
) {
    while let Some(request) = stream.next().await {
        match request {
            Ok(request) => {
                if let Err(e) = test_requests.send(request).await {
                    info!("Error handling test request: {}", e);
                    break;
                }
            }
            Err(e) => info!("Error in test connection: {}", e),
        }
    }
}

pub async fn run_services(
    mut fs: ServiceFs<ServiceObj<'_, Services>>,
    call_manager: Sender<CallManagerProxy>,
    test_requests: Sender<HfpTestRequest>,
) -> Result<(), Error> {
    fs.dir("svc").add_fidl_service(Services::Hfp).add_fidl_service(Services::HfpTest);
    fs.take_and_serve_directory_handle().context("Failed to serve ServiceFs directory")?;
    fs.for_each_concurrent(MAX_CONCURRENT_CONNECTIONS, move |connection| match connection {
        Services::Hfp(stream) => handle_hfp_client_connection(stream, call_manager.clone()).boxed(),
        Services::HfpTest(stream) => {
            handle_hfp_test_client_connection(stream, test_requests.clone()).boxed()
        }
    })
    .await;
    Ok(())
}

#[cfg(test)]
mod tests {
    use {
        super::*, fidl::endpoints::RequestStream, fidl_fuchsia_bluetooth_bredr as bredr,
        fidl_fuchsia_bluetooth_hfp::*, futures::channel::mpsc, matches::assert_matches,
    };

    #[fuchsia::test(allow_stalls = false)]
    async fn successful_no_op_hfp_connection() {
        let (sender, _receiver) = mpsc::channel(0);
        let (proxy, stream) = fidl::endpoints::create_proxy_and_stream::<HfpMarker>().unwrap();

        // close the stream by dropping the proxy
        drop(proxy);

        let result = handle_hfp_client_connection_result(stream, sender).await;
        assert!(result.is_ok());
    }

    #[fuchsia::test(allow_stalls = false)]
    async fn error_on_hfp_connection() {
        let (sender, _receiver) = mpsc::channel(0);

        // Create a stream of an unexpected protocol type.
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<bredr::ProfileMarker>().unwrap();
        // Pretend that stream is the expected protocol type.
        let stream = stream.cast_stream::<HfpRequestStream>();
        // Send a request using the wrong protocol to the server end.
        let _ = proxy
            .connect(
                &mut fidl_fuchsia_bluetooth::PeerId { value: 1 },
                &mut bredr::ConnectParameters::L2cap(bredr::L2capParameters::EMPTY),
            )
            .check()
            .expect("request to be sent");

        let result = handle_hfp_client_connection_result(stream, sender).await;
        assert_matches!(result, Err(_));
    }

    #[fuchsia::test(allow_stalls = false)]
    async fn successful_call_manager_registration() {
        let (sender, _receiver) = mpsc::channel(1);
        let (proxy, stream) = fidl::endpoints::create_proxy_and_stream::<HfpMarker>().unwrap();

        let (call_manager_client_end, _call_manager_server_end) =
            fidl::endpoints::create_endpoints::<CallManagerMarker>().unwrap();

        // Register a call manager.
        proxy.register(call_manager_client_end).expect("request to be sent");

        // Close the stream by dropping the proxy.
        drop(proxy);

        let result = handle_hfp_client_connection_result(stream, sender).await;

        assert!(result.is_ok());
    }
}
