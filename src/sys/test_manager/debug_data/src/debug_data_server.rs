// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::message::{DebugDataRequestMessage, VmoMessage};
use anyhow::Error;
use fidl::endpoints::{ControlHandle, Responder};
use fidl_fuchsia_debugdata as fdebug;
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use futures::{
    channel::mpsc,
    stream::{Stream, StreamExt, TryStreamExt},
    SinkExt, TryFutureExt,
};
use log::warn;

/// Serve the |fuchsia.debugdata.DebugData| protocol for each connection received over
/// |request_stream|. VMOs ready to be processed are output via |vmo_sender|.
pub async fn serve_debug_data_requests<S: Stream<Item = DebugDataRequestMessage>>(
    request_stream: S,
    vmo_sender: mpsc::Sender<VmoMessage>,
) {
    request_stream
        .for_each_concurrent(None, move |request|
                // failure serving one connection shouldn't terminate all of them. 
                serve_debug_data(request, vmo_sender.clone())
                    .unwrap_or_else(|e| warn!("Error serving debug data: {:?}", e)))
        .await
}

async fn serve_debug_data(
    request_message: DebugDataRequestMessage,
    vmo_sender: mpsc::Sender<VmoMessage>,
) -> Result<(), Error> {
    let DebugDataRequestMessage { test_url, request } = request_message;
    let request_stream = request.into_stream()?;

    request_stream
        .map_err(Into::<Error>::into)
        .try_for_each_concurrent(None, move |request| {
            handle_debug_data_request(request, test_url.clone(), vmo_sender.clone())
        })
        .await
}

async fn handle_debug_data_request(
    request: fdebug::DebugDataRequest,
    test_url: String,
    mut vmo_sender: mpsc::Sender<VmoMessage>,
) -> Result<(), Error> {
    match request {
        fdebug::DebugDataRequest::LoadConfig { responder, .. } => {
            let _ = responder.control_handle().shutdown_with_epitaph(zx::Status::NOT_SUPPORTED);
            Ok(())
        }
        fdebug::DebugDataRequest::Publish { data_sink, data, vmo_token, .. } => {
            // Wait for the token channel to close before sending the VMO for processing.
            // This allows the client to continue modifying the VMO after it has sent it.
            // See |fuchsia.debugdata.DebugData| protocol for details.
            fasync::OnSignals::new(&vmo_token, zx::Signals::CHANNEL_PEER_CLOSED).await?;
            vmo_sender.send(VmoMessage { test_url, data_sink, vmo: data }).await?;
            Ok(())
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use assert_matches::assert_matches;
    use fidl::{endpoints::create_proxy, AsHandleRef};
    use futures::FutureExt;

    fn create_proxy_and_message(
        test_url: &str,
    ) -> (fdebug::DebugDataProxy, DebugDataRequestMessage) {
        let (proxy, request) = create_proxy::<fdebug::DebugDataMarker>().unwrap();
        (proxy, DebugDataRequestMessage { test_url: test_url.to_string(), request })
    }

    #[fuchsia::test]
    async fn single_debugdata_connection() {
        let (proxy, message) = create_proxy_and_message("test-url");
        let (vmo_send, vmo_recv) = mpsc::channel(5);
        let serve_fut = serve_debug_data(message, vmo_send);
        let test_fut = async move {
            let (vmo_token, vmo_token_server) =
                create_proxy::<fdebug::DebugDataVmoTokenMarker>().unwrap();
            let vmo = zx::Vmo::create(1024).unwrap();
            let vmo_koid = vmo.get_koid().unwrap();
            proxy.publish("data-sink", vmo, vmo_token_server).expect("publish vmo");
            drop(vmo_token);
            drop(proxy);
            let vmo_messages: Vec<_> = vmo_recv.collect().await;
            assert_eq!(vmo_messages.len(), 1);
            assert_eq!(vmo_messages[0].test_url, "test-url");
            assert_eq!(vmo_messages[0].data_sink, "data-sink");
            assert_eq!(vmo_messages[0].vmo.get_koid().unwrap(), vmo_koid);
        };
        let (result, ()) = futures::future::join(serve_fut, test_fut).await;
        result.expect("serve failed");
    }

    #[fuchsia::test]
    fn single_debugdata_connection_send_vmo_when_ready() {
        let mut executor = fasync::TestExecutor::new().unwrap();

        let (proxy, message) = create_proxy_and_message("test-url");
        let (vmo_send, mut vmo_recv) = mpsc::channel(5);
        let mut serve_fut = serve_debug_data(message, vmo_send).boxed();

        let (vmo_token, vmo_token_server) =
            create_proxy::<fdebug::DebugDataVmoTokenMarker>().unwrap();
        let vmo = zx::Vmo::create(1024).unwrap();
        let vmo_koid = vmo.get_koid().unwrap();

        // VMO should not be sent for processing until the vmo_token channel is closed.
        proxy.publish("data-sink", vmo, vmo_token_server).expect("publish vmo");
        assert!(executor.run_until_stalled(&mut serve_fut).is_pending());
        assert!(vmo_recv.try_next().is_err());

        // After closing vmo_token, the VMO is sent for processing.
        drop(vmo_token);
        assert!(executor.run_until_stalled(&mut serve_fut).is_pending());
        let vmo_message = vmo_recv.try_next().expect("receive vmo").unwrap();
        assert_eq!(vmo_message.test_url, "test-url");
        assert_eq!(vmo_message.data_sink, "data-sink");
        assert_eq!(vmo_message.vmo.get_koid().unwrap(), vmo_koid);

        drop(proxy);
        match executor.run_until_stalled(&mut serve_fut) {
            futures::task::Poll::Ready(Ok(())) => (),
            other => panic!("Expected server to complete succesffully but got {:?}", other),
        }
    }

    #[fuchsia::test]
    async fn close_connection_on_load_config() {
        let (proxy, message) = create_proxy_and_message("test-url");
        let (vmo_send, _) = mpsc::channel(5);
        let serve_fut = serve_debug_data(message, vmo_send);
        let test_fut = async move {
            assert_matches!(
                proxy.load_config("config").await.unwrap_err(),
                fidl::Error::ClientChannelClosed { status: zx::Status::NOT_SUPPORTED, .. }
            );
        };
        let (result, ()) = futures::future::join(serve_fut, test_fut).await;
        result.expect("serve failed");
    }

    #[fuchsia::test]
    async fn handle_concurrent_debugdata_connections() {
        const CONCURRENT_CLIENTS: usize = 10;
        let (proxies, messages): (Vec<_>, Vec<_>) = (0..CONCURRENT_CLIENTS)
            .map(|i| create_proxy_and_message(&format!("test-url-{:?}", i)))
            .unzip();

        let (vmo_send, mut vmo_recv) = mpsc::channel(5);
        let serve_fut = serve_debug_data_requests(futures::stream::iter(messages), vmo_send);
        let test_fut = async move {
            // Send one VMO for each proxy.
            let first_vmo_tokens: Vec<_> = proxies
                .iter()
                .map(|proxy| {
                    let (vmo_token, vmo_token_server) =
                        create_proxy::<fdebug::DebugDataVmoTokenMarker>().unwrap();
                    let vmo = zx::Vmo::create(1024).unwrap();
                    proxy.publish("data-sink-1", vmo, vmo_token_server).expect("publish vmo");
                    vmo_token
                })
                .collect();
            // Send a second VMO for each proxy.
            let second_vmo_tokens: Vec<_> = proxies
                .iter()
                .map(|proxy| {
                    let (vmo_token, vmo_token_server) =
                        create_proxy::<fdebug::DebugDataVmoTokenMarker>().unwrap();
                    let vmo = zx::Vmo::create(1024).unwrap();
                    proxy.publish("data-sink-2", vmo, vmo_token_server).expect("publish vmo");
                    vmo_token
                })
                .collect();
            // After dropping the second VMOs, the messages for those VMOS only should be ready.
            drop(second_vmo_tokens);
            let ready_vmos: Vec<_> = vmo_recv.by_ref().take(CONCURRENT_CLIENTS).collect().await;
            assert!(ready_vmos.iter().all(|message| message.data_sink == "data-sink-2"));
            assert!(vmo_recv.try_next().is_err());
            // After dropping the first VMOs, the remaining messages are ready.
            drop(first_vmo_tokens);
            let ready_vmos: Vec<_> = vmo_recv.by_ref().take(CONCURRENT_CLIENTS).collect().await;
            assert!(ready_vmos.iter().all(|message| message.data_sink == "data-sink-1"));
            assert!(vmo_recv.try_next().is_err());
            // dropping the proxies terminates the stream.
            drop(proxies);
            assert!(vmo_recv.next().await.is_none());
        };
        let ((), ()) = futures::future::join(serve_fut, test_fut).await;
    }

    #[fuchsia::test]
    async fn subsequent_requests_handled_when_vmo_not_ready() {
        // this test verifies that when a VMO is not immediately ready to be processed,
        // this does not stop subsequent requests from being processed
        let (proxy, message) = create_proxy_and_message("test-url-2");
        let (vmo_send, mut vmo_recv) = mpsc::channel(5);
        let serve_fut = serve_debug_data(message, vmo_send);
        let test_fut = async move {
            let (vmo_token_1, vmo_token_server_1) =
                create_proxy::<fdebug::DebugDataVmoTokenMarker>().unwrap();
            let vmo_1 = zx::Vmo::create(1024).unwrap();
            let vmo_1_koid = vmo_1.get_koid().unwrap();

            proxy.publish("data-sink-1", vmo_1, vmo_token_server_1).expect("publish vmo");
            // VMO should not be sent for processing until the vmo_token_1 channel is closed.
            // Hold it open, and in the meantime, send another vmo
            let (vmo_token_2, vmo_token_server_2) =
                create_proxy::<fdebug::DebugDataVmoTokenMarker>().unwrap();
            let vmo_2 = zx::Vmo::create(1024).unwrap();
            let vmo_2_koid = vmo_2.get_koid().unwrap();
            proxy.publish("data-sink-2", vmo_2, vmo_token_server_2).expect("publish vmo");
            // second VMO should become immediately available for processing once token is dropped
            drop(vmo_token_2);
            let next_message = vmo_recv.next().await.unwrap();
            assert_eq!(next_message.data_sink, "data-sink-2");
            assert_eq!(next_message.vmo.get_koid().unwrap(), vmo_2_koid);

            // if we never drop the token for the first VMO, it should not be sent.
            assert!(vmo_recv.next().now_or_never().is_none());
            // after dropping the first token the message becomes available.
            drop(vmo_token_1);
            let next_message = vmo_recv.next().await.unwrap();
            assert_eq!(next_message.data_sink, "data-sink-1");
            assert_eq!(next_message.vmo.get_koid().unwrap(), vmo_1_koid);
        };
        let ((), result) = futures::future::join(test_fut, serve_fut).await;
        result.expect("serve should not fail");
    }
}
