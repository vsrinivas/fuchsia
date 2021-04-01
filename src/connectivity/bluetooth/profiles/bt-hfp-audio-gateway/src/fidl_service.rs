// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    fidl_fuchsia_bluetooth_hfp::{CallManagerProxy, HfpRequest, HfpRequestStream},
    fuchsia_component::server::{ServiceFs, ServiceObj},
    futures::{channel::mpsc::Sender, SinkExt, StreamExt, TryStreamExt},
    log::info,
};

/// The maximum number of fidl service client connections that will be serviced concurrently.
const MAX_CONCURRENT_CONNECTIONS: usize = 10;

/// All FIDL services that are exposed by this component's ServiceFs.
pub enum Services {
    Hfp(HfpRequestStream),
}

/// Handle a new incoming Hfp protocol client connection. This async function completes when
/// a client connection is closed.
async fn handle_hfp_client_connection(
    stream: HfpRequestStream,
    call_manager: Sender<CallManagerProxy>,
) {
    log::info!("new hfp connection");
    if let Err(e) = handle_hfp_client_connection_result(stream, call_manager).await {
        // An error processing client provided parameters is not a fatal error.
        // Log and return.
        info!("hfp FIDL client error: {}", e);
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
        info!("registering call manager");
        let proxy = manager.into_proxy().context("hfp FIDL client error")?;
        call_manager.send(proxy).await.context("component main loop halted")?;
    }
    Ok(())
}

pub async fn run_services(
    mut fs: ServiceFs<ServiceObj<'_, Services>>,
    call_manager: Sender<CallManagerProxy>,
) -> Result<(), Error> {
    fs.dir("svc").add_fidl_service(Services::Hfp);
    fs.take_and_serve_directory_handle().context("Failed to serve ServiceFs directory")?;
    fs.for_each_concurrent(MAX_CONCURRENT_CONNECTIONS, move |Services::Hfp(stream)| {
        handle_hfp_client_connection(stream, call_manager.clone())
    })
    .await;
    Ok(())
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl::endpoints::{ClientEnd, RequestStream},
        fidl_fuchsia_bluetooth_bredr as bredr,
        fidl_fuchsia_bluetooth_hfp::*,
        fuchsia_async as fasync, fuchsia_zircon as zx,
        futures::channel::mpsc,
        matches::assert_matches,
    };

    #[fasync::run_until_stalled(test)]
    async fn successful_no_op_hfp_connection() {
        let (sender, _receiver) = mpsc::channel(0);
        let (proxy, stream) = fidl::endpoints::create_proxy_and_stream::<HfpMarker>().unwrap();

        // close the stream by dropping the proxy
        drop(proxy);

        let result = handle_hfp_client_connection_result(stream, sender).await;
        assert!(result.is_ok());
    }

    #[fasync::run_until_stalled(test)]
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

    #[fasync::run_until_stalled(test)]
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

    #[fasync::run_until_stalled(test)]
    async fn error_on_bad_registration_parameter() {
        let (sender, _receiver) = mpsc::channel(0);
        let (proxy, stream) = fidl::endpoints::create_proxy_and_stream::<HfpMarker>().unwrap();

        // Create an Event that we will cast to a Channel and send to the hfp server.
        // This invalid handle type is expected to cause the hfp server to return an error.
        let event = zx::Event::create().unwrap();
        let invalid_channel: zx::Channel = zx::Channel::from(zx::Handle::from(event));
        let invalid_client_end = ClientEnd::new(invalid_channel);

        proxy.register(invalid_client_end).expect("request to be sent");

        let result = handle_hfp_client_connection_result(stream, sender).await;

        assert_matches!(result, Err(_));
    }
}
