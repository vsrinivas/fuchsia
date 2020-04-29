// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_net_name as name;
use futures::{Stream, TryFutureExt};

/// Creates a stream of [`name::DnsServer_`] from watching the server
/// configuration provided by `proxy`.
pub fn new_dns_server_stream(
    proxy: name::DnsServerWatcherProxy,
) -> impl Stream<Item = Result<Vec<name::DnsServer_>, fidl::Error>> {
    futures::stream::try_unfold(proxy, |proxy| {
        proxy.watch_servers().map_ok(move |item| Some((item, proxy)))
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::test_util::*;

    use fuchsia_async as fasync;
    use futures::lock::Mutex;
    use futures::{FutureExt, StreamExt, TryStreamExt};
    use std::collections::VecDeque;
    use std::sync::Arc;

    struct MockDnsServerWatcher {
        configs: VecDeque<Vec<name::DnsServer_>>,
        pending_request: Option<name::DnsServerWatcherWatchServersResponder>,
    }

    impl MockDnsServerWatcher {
        fn new() -> Self {
            Self { configs: VecDeque::new(), pending_request: None }
        }

        fn push_config(&mut self, config: Vec<name::DnsServer_>) {
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
            rs: name::DnsServerWatcherRequestStream,
        ) -> Result<(), fidl::Error> {
            rs.try_for_each(move |r| {
                let watcher = watcher.clone();
                async move {
                    match r {
                        name::DnsServerWatcherRequest::WatchServers { responder } => {
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
            fidl::endpoints::create_proxy_and_stream::<name::DnsServerWatcherMarker>().unwrap();
        let (serve_fut, abort_handle) =
            futures::future::abortable(MockDnsServerWatcher::serve(watcher.clone(), rs));

        let (serve_result, mut stream) = futures::future::join(serve_fut, async move {
            let mut stream = new_dns_server_stream(proxy);
            assert!(stream.next().now_or_never().is_none());
            assert!(stream.next().now_or_never().is_none());
            {
                let mut w = watcher.lock().await;
                w.push_config(vec![to_discovered_server(DYNAMIC_SERVER_A)]);
                w.push_config(vec![to_discovered_server(DYNAMIC_SERVER_B)]);
            }
            let nxt = stream
                .next()
                .await
                .expect("Stream ended unexpectedly")
                .expect("FIDL error occurred");
            assert_eq!(nxt, vec![to_discovered_server(DYNAMIC_SERVER_A)]);
            let nxt = stream
                .next()
                .await
                .expect("Stream ended unexpectedly")
                .expect("FIDL error occurred");
            assert_eq!(nxt, vec![to_discovered_server(DYNAMIC_SERVER_B)]);

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
