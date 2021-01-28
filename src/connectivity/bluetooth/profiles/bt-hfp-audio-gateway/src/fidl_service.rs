// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_bluetooth_hfp::{HfpRequest, HfpRequestStream},
    futures::TryStreamExt,
    log::info,
};

use crate::call_manager::CallManagerServiceProvider;

/// All FIDL services that are exposed by this component's ServiceFs.
pub enum Services {
    Hfp(HfpRequestStream),
}

/// Handle a new incoming Hfp protocol client connection. This async function completes when
/// a client connection is closed.
pub async fn handle_hfp_client_connection(
    stream: HfpRequestStream,
    call_manager: CallManagerServiceProvider,
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
    call_manager: CallManagerServiceProvider,
) -> Result<(), fidl::Error> {
    while let Some(request) = stream.try_next().await? {
        let HfpRequest::Register { manager, .. } = request;
        info!("registering call manager");
        let stream = manager.into_stream()?;
        call_manager.register(stream).await;
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::call_manager::CallManager,
        fidl::endpoints::{RequestStream, ServerEnd},
        fidl_fuchsia_bluetooth_bredr as bredr,
        fidl_fuchsia_bluetooth_hfp::*,
        fuchsia_async as fasync, fuchsia_zircon as zx,
        matches::assert_matches,
    };

    #[fasync::run_until_stalled(test)]
    async fn successful_no_op_hfp_connection() {
        let mut manager = CallManager::new();
        let service = manager.service_provider().unwrap();
        let (proxy, stream) = fidl::endpoints::create_proxy_and_stream::<HfpMarker>().unwrap();

        // close the stream by dropping the proxy
        drop(proxy);

        let result = handle_hfp_client_connection_result(stream, service).await;
        assert!(result.is_ok());
    }

    #[fasync::run_until_stalled(test)]
    async fn error_on_hfp_connection() {
        let mut manager = CallManager::new();
        let service = manager.service_provider().unwrap();

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

        let result = handle_hfp_client_connection_result(stream, service).await;
        assert_matches!(result, Err(fidl::Error::UnknownOrdinal { .. }));
    }

    #[fasync::run_until_stalled(test)]
    async fn successful_call_manager_registration() {
        let mut manager = CallManager::new();
        let service = manager.service_provider().unwrap();
        let (proxy, stream) = fidl::endpoints::create_proxy_and_stream::<HfpMarker>().unwrap();

        let (_call_manager_proxy, call_manager_server_end) =
            fidl::endpoints::create_proxy::<CallManagerMarker>().unwrap();

        // Register a call manager.
        proxy.register(call_manager_server_end).expect("request to be sent");

        // Close the stream by dropping the proxy.
        drop(proxy);

        let result = handle_hfp_client_connection_result(stream, service).await;

        assert!(result.is_ok());
    }

    #[fasync::run_until_stalled(test)]
    async fn error_on_bad_registration_parameter() {
        let mut manager = CallManager::new();
        let service = manager.service_provider().unwrap();
        let (proxy, stream) = fidl::endpoints::create_proxy_and_stream::<HfpMarker>().unwrap();

        // Create an Event that we will cast to a Channel and send to the hfp server.
        // This invalid handle type is expected to cause the hfp server to return an error.
        let event = zx::Event::create().unwrap();
        let invalid_channel: zx::Channel = zx::Channel::from(zx::Handle::from(event));
        let invalid_server_end = ServerEnd::new(invalid_channel);

        proxy.register(invalid_server_end).expect("request to be sent");

        let result = handle_hfp_client_connection_result(stream, service).await;

        assert_matches!(result, Err(fidl::Error::IncorrectHandleSubtype { .. }));
    }
}
