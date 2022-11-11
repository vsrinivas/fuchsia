// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl::endpoints::Proxy;
use fidl_fuchsia_bluetooth_fastpair::ProviderWatcherProxy;
use fuchsia_bluetooth::types::PeerId;
use futures::future::{self, Future, FutureExt};
use tracing::{debug, warn};

use crate::types::Error;

/// Manages the state of a connection an upstream `fuchsia.bluetooth.fastpair.Provider` client that
/// has requested to enable the Provider service.
pub struct FastPairConnectionManager {
    /// The connection to the upstream client. The Provider service is considered enabled if this
    /// connection is set and open.
    watcher: Option<ProviderWatcherProxy>,
}

impl FastPairConnectionManager {
    pub fn new() -> Self {
        Self { watcher: None }
    }

    pub fn reset(&mut self) {
        if let Some(_upstream) = self.watcher.take() {
            debug!("Reset upstream Fast Pair connection");
        }
    }

    #[cfg(test)]
    pub fn new_with_upstream(watcher: ProviderWatcherProxy) -> Self {
        Self { watcher: Some(watcher) }
    }

    pub fn is_enabled(&self) -> bool {
        self.watcher.as_ref().map_or(false, |h| !h.is_closed())
    }

    pub fn set(&mut self, watcher: ProviderWatcherProxy) -> Result<(), Error> {
        if self.is_enabled() {
            return Err(Error::AlreadyEnabled);
        }

        self.watcher = Some(watcher);
        Ok(())
    }

    pub fn notify_pairing_complete(&self, id: PeerId) {
        if let Some(watcher) = &self.watcher {
            // TODO(aniramakri): This is an async request which expects an empty response.
            // Should we care?
            if let Err(e) = watcher.on_pairing_complete(&mut id.into()).check() {
                warn!("Couldn't notify upstream client of pairing complete: {:?}", e);
            }
        }
    }

    /// If set, returns a Future that resolves when the `ProviderWatcher` channel is closed.
    /// Otherwise, returns a Future that never resolves.
    pub fn on_upstream_client_closed(&self) -> impl Future<Output = ()> + 'static {
        match &self.watcher {
            Some(watcher) => watcher.on_closed().extend_lifetime().map(|_| ()).left_future(),
            None => future::pending().right_future(),
        }
    }
}

#[cfg(test)]
pub(crate) mod tests {
    use super::*;

    use assert_matches::assert_matches;
    use fidl::client::QueryResponseFut;
    use fidl::endpoints::{create_proxy_and_stream, create_request_stream};
    use fidl_fuchsia_bluetooth_fastpair::{
        ProviderMarker, ProviderWatcherMarker, ProviderWatcherRequestStream,
    };
    use futures::stream::StreamExt;

    use crate::provider::ServiceRequest;

    /// An upstream client that requests to enable Fast Pair functionality.
    pub(crate) struct MockUpstreamClient {
        connection: ProviderWatcherRequestStream,
    }

    impl MockUpstreamClient {
        pub fn new() -> (Self, ProviderWatcherProxy) {
            let (c, s) = create_proxy_and_stream::<ProviderWatcherMarker>().unwrap();
            (MockUpstreamClient { connection: s }, c)
        }

        pub async fn expect_on_pairing_complete(&mut self, expected_id: PeerId) {
            let notification = self.connection.select_next_some().await.expect("fidl response");
            let (received_id, responder) = notification.into_on_pairing_complete().unwrap();
            assert_eq!(expected_id, received_id.into());
            let _ = responder.send();
        }

        pub async fn make_enable_request(
        ) -> (QueryResponseFut<Result<(), i32>>, ServiceRequest, Self) {
            let (c, mut s) = create_proxy_and_stream::<ProviderMarker>().unwrap();
            let (watcher_client, watcher_server) =
                create_request_stream::<ProviderWatcherMarker>().unwrap();
            let mock_client = Self { connection: watcher_server };
            let fut = c.enable(watcher_client);
            let (watcher, responder) =
                s.select_next_some().await.expect("fidl request").into_enable().unwrap();

            (
                fut,
                ServiceRequest::EnableFastPair {
                    watcher: watcher.into_proxy().unwrap(),
                    responder,
                },
                mock_client,
            )
        }
    }

    #[fuchsia::test]
    async fn fast_pair_connection_to_client() {
        let mut connection = FastPairConnectionManager::new();
        assert!(!connection.is_enabled());
        // With no upstream client, the closed future should never resolve.
        let closed_fut = connection.on_upstream_client_closed();
        assert_matches!(closed_fut.now_or_never(), None);

        let (mut mock_client, c) = MockUpstreamClient::new();
        assert_matches!(connection.set(c), Ok(_));
        assert!(connection.is_enabled());
        // Upstream client is set and active, closed future should not resolve.
        let closed_fut = connection.on_upstream_client_closed();
        assert_matches!(closed_fut.now_or_never(), None);

        // Trying to set another client while the existing one is active is an Error.
        let (_mock_client1, c1) = MockUpstreamClient::new();
        assert_matches!(connection.set(c1), Err(Error::AlreadyEnabled));

        // Can notify upstream that pairing completed.
        let id = PeerId(123);
        connection.notify_pairing_complete(id);
        mock_client.expect_on_pairing_complete(id).await;
        assert!(connection.is_enabled());

        // Upstream wants to disable Fast Pair - closed fut should resolve. Service is no longer
        // enabled.
        drop(mock_client);
        let closed_fut = connection.on_upstream_client_closed();
        let () = closed_fut.await;
        assert!(!connection.is_enabled());

        // Trying to set another client is OK now that the previous one finished.
        let (_mock_client2, c2) = MockUpstreamClient::new();
        assert_matches!(connection.set(c2), Ok(_));
        assert!(connection.is_enabled());
    }
}
