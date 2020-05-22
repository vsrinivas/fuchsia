// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! DNS Server watcher.

use fidl_fuchsia_net_name::{DnsServerWatcherProxy, DnsServer_};
use fuchsia_async as fasync;
use futures::{
    channel::mpsc,
    future::TryFutureExt,
    sink::SinkExt,
    stream::{Stream, StreamExt, TryStreamExt},
};

/// An updated DNS server event from some source.
#[derive(Debug)]
pub struct DnsServerWatcherEvent {
    /// The source of the DNS server update.
    pub(super) source: DnsServerWatcherSource,

    /// The updated list of DNS servers.
    pub(super) servers: Vec<DnsServer_>,
}

/// The possible sources of DNS server updates.
#[derive(Debug, Copy, Clone)]
pub(super) enum DnsServerWatcherSource {
    Netstack,
}

/// A watcher for DNS server updates.
pub(super) struct DnsServerWatcher {
    source: DnsServerWatcherSource,
    client: DnsServerWatcherProxy,
}

impl DnsServerWatcher {
    /// Returns a new `DnsServerWatcher` that watches for events on `client` which was obtained
    /// from `source`.
    ///
    /// All events this watcher sends to the eventloop will be marked with the provided source
    /// so the eventloop knows where the DNS server event came from.
    pub(super) fn new(
        source: DnsServerWatcherSource,
        client: DnsServerWatcherProxy,
    ) -> DnsServerWatcher {
        DnsServerWatcher { source, client }
    }

    /// Spawns a new future that sends new DNS server events to the eventloop.
    pub(super) fn spawn(self, event_chan: mpsc::UnboundedSender<crate::event::Event>) {
        let Self { source, client } = self;

        info!("Starting DNS Server watcher for {:?}", source);

        fasync::spawn_local(async move {
            new_dns_server_stream(client)
                .map_ok(|servers| DnsServerWatcherEvent { source, servers }.into())
                .map_err(anyhow::Error::from)
                .forward(event_chan.clone().sink_map_err(anyhow::Error::from))
                .await
                .unwrap_or_else(|e| error!("error watching for DNS server updates: {:?}", e))
        });
    }
}

/// Creates a stream of [`DnsServer_`] from watching the server
/// configuration provided by `proxy`.
fn new_dns_server_stream(
    proxy: DnsServerWatcherProxy,
) -> impl Stream<Item = Result<Vec<DnsServer_>, fidl::Error>> {
    futures::stream::try_unfold(proxy, |proxy| {
        proxy.watch_servers().map_ok(move |item| Some((item, proxy)))
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::test_util::dns::*;

    use fidl_fuchsia_net_name::{
        DnsServerWatcherMarker, DnsServerWatcherRequest, DnsServerWatcherRequestStream,
        DnsServerWatcherWatchServersResponder,
    };

    use fuchsia_async as fasync;
    use futures::lock::Mutex;
    use futures::{FutureExt, StreamExt, TryStreamExt};
    use std::collections::VecDeque;
    use std::sync::Arc;

    struct MockDnsServerWatcher {
        configs: VecDeque<Vec<DnsServer_>>,
        pending_request: Option<DnsServerWatcherWatchServersResponder>,
    }

    impl MockDnsServerWatcher {
        fn new() -> Self {
            Self { configs: VecDeque::new(), pending_request: None }
        }

        fn push_config(&mut self, config: Vec<DnsServer_>) {
            match self.pending_request.take() {
                Some(req) => {
                    let () =
                        req.send(&mut config.into_iter()).expect("Failed to fulfill FIDL request");
                }
                None => self.configs.push_back(config),
            }
        }

        async fn serve(
            watcher: Arc<Mutex<Self>>,
            rs: DnsServerWatcherRequestStream,
        ) -> Result<(), fidl::Error> {
            rs.try_for_each(move |r| {
                let watcher = watcher.clone();
                async move {
                    match r {
                        DnsServerWatcherRequest::WatchServers { responder } => {
                            let mut w = watcher.lock().await;
                            if w.pending_request.is_some() {
                                panic!("No more than 1 pending requests allowed");
                            }

                            if let Some(config) = w.configs.pop_front() {
                                responder
                                    .send(&mut config.into_iter())
                                    .expect("Failed to fulfill FIDL request");
                            } else {
                                w.pending_request = Some(responder)
                            }
                        }
                    }
                    Ok(())
                }
            })
            .await
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_dns_server_stream() {
        let watcher = Arc::new(Mutex::new(MockDnsServerWatcher::new()));
        let (proxy, rs) =
            fidl::endpoints::create_proxy_and_stream::<DnsServerWatcherMarker>().unwrap();
        let (serve_fut, abort_handle) =
            futures::future::abortable(MockDnsServerWatcher::serve(watcher.clone(), rs));

        let (serve_result, mut stream) = futures::future::join(serve_fut, async move {
            let mut stream = new_dns_server_stream(proxy);
            assert!(stream.next().now_or_never().is_none());
            assert!(stream.next().now_or_never().is_none());
            {
                let mut w = watcher.lock().await;
                w.push_config(vec![DHCPV6_SERVER]);
                w.push_config(vec![STATIC_SERVER]);
            }
            let nxt = stream
                .next()
                .await
                .expect("Stream ended unexpectedly")
                .expect("FIDL error occurred");
            assert_eq!(nxt, vec![DHCPV6_SERVER]);
            let nxt = stream
                .next()
                .await
                .expect("Stream ended unexpectedly")
                .expect("FIDL error occurred");
            assert_eq!(nxt, vec![STATIC_SERVER]);

            // Abort the serving future so join will end.
            abort_handle.abort();
            stream
        })
        .await;
        let _aborted = serve_result.expect_err("Future must've been aborted");
        let _fidl_error: fidl::Error = stream
            .next()
            .await
            .expect("Stream must yield a final value")
            .expect_err("Stream must yield an error");
        assert!(stream.next().await.is_none(), "Stream must end after error");
    }
}
