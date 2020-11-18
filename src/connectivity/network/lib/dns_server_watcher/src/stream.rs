// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! DNS Server watcher stream.

use fidl_fuchsia_net_name::{DnsServerWatcherProxy, DnsServer_};

use async_utils::stream::WithTag as _;
use futures::{future::TryFutureExt as _, stream::Stream};

/// The possible sources of DNS server updates.
#[derive(Debug, Copy, Clone, PartialEq, Eq, Hash)]
pub enum DnsServersUpdateSource {
    Default,
    Netstack,
    Dhcpv6 { interface_id: u64 },
}

/// Returns a `Stream` of [`DnsServerWatcherEvent`]s from watching the server configuration
/// provided by `proxy`.
pub fn new_dns_server_stream(
    source: DnsServersUpdateSource,
    proxy: DnsServerWatcherProxy,
) -> impl Stream<Item = (DnsServersUpdateSource, Result<Vec<DnsServer_>, fidl::Error>)> {
    futures::stream::try_unfold(proxy, move |proxy| {
        proxy.watch_servers().map_ok(move |s| Some((s, proxy)))
    })
    .tagged(source)
}

#[cfg(test)]
mod tests {
    use std::collections::VecDeque;
    use std::sync::Arc;

    use fidl_fuchsia_net_name::{
        DnsServerWatcherMarker, DnsServerWatcherRequest, DnsServerWatcherRequestStream,
        DnsServerWatcherWatchServersResponder,
    };

    use fuchsia_async as fasync;
    use futures::lock::Mutex;
    use futures::{FutureExt, StreamExt, TryStreamExt};

    use super::*;
    use crate::test_util::constants::*;

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
            let mut stream = new_dns_server_stream(DnsServersUpdateSource::Netstack, proxy);
            assert!(stream.next().now_or_never().is_none());
            assert!(stream.next().now_or_never().is_none());
            {
                let mut w = watcher.lock().await;
                w.push_config(vec![NDP_SERVER]);
                w.push_config(vec![STATIC_SERVER]);
            }
            let (source, res) = stream.next().await.expect("stream ended unexpectedly");
            assert_eq!(source, DnsServersUpdateSource::Netstack);
            assert_eq!(vec![NDP_SERVER], res.expect("FIDL error occurred"));

            let (source, res) = stream.next().await.expect("stream ended unexpectedly");
            assert_eq!(source, DnsServersUpdateSource::Netstack);
            assert_eq!(vec![STATIC_SERVER], res.expect("FIDL error occurred"));

            // Abort the serving future so join will end.
            abort_handle.abort();
            stream
        })
        .await;
        let _aborted = serve_result.expect_err("Future must've been aborted");
        let (source, res) = stream.next().await.expect("Stream must yield a final value");
        assert_eq!(source, DnsServersUpdateSource::Netstack);
        let _fidl_error: fidl::Error = res.expect_err("Stream must yield an error");
        assert!(stream.next().await.is_none(), "Stream must end after error");
    }
}
