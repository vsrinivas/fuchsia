// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_examples_inspect::{ReverserRequest, ReverserRequestStream},
    fuchsia_async as fasync,
    fuchsia_inspect::{self as inspect, NumericProperty},
    futures::TryStreamExt,
    std::sync::Arc,
};

pub struct ReverserServerFactory {
    node: inspect::Node,
    request_count: Arc<inspect::UintProperty>,
    connection_count: inspect::UintProperty,
}

impl ReverserServerFactory {
    pub fn new(node: inspect::Node) -> Self {
        let request_count = Arc::new(node.create_uint("total_requests", 0));
        let connection_count = node.create_uint("connection_count", 0);
        Self { node, request_count, connection_count }
    }

    pub fn spawn_new(&self, stream: ReverserRequestStream) {
        self.spawn_new_internal(stream, || {})
    }

    fn spawn_new_internal<F>(&self, stream: ReverserRequestStream, callback: F)
    where
        F: FnOnce() -> () + 'static,
    {
        let node = self.node.create_child(inspect::unique_name("connection"));
        let metrics = ReverserServerMetrics::new(node, self.request_count.clone());
        ReverserServer::new(metrics).spawn(stream, callback);
        self.connection_count.add(1);
    }
}

// The default implementation of ReverserServerMetrics is a no-op on the inspect properties.
#[derive(Default)]
struct ReverserServerMetrics {
    global_request_count: Arc<inspect::UintProperty>,
    request_count: inspect::UintProperty,
    response_count: inspect::UintProperty,
    _node: inspect::Node,
}

impl ReverserServerMetrics {
    fn new(node: inspect::Node, global_request_count: Arc<inspect::UintProperty>) -> Self {
        let request_count = node.create_uint("request_count", 0);
        let response_count = node.create_uint("response_count", 0);
        let metrics = Self {
            global_request_count,
            // Hold to the node until we are done with this connection.
            _node: node,
            request_count,
            response_count,
        };
        metrics
    }
}

struct ReverserServer {
    metrics: ReverserServerMetrics,
}

impl ReverserServer {
    fn new(metrics: ReverserServerMetrics) -> Self {
        Self { metrics }
    }

    pub fn spawn<F>(self, mut stream: ReverserRequestStream, test_on_done: F)
    where
        F: FnOnce() -> () + 'static,
    {
        fasync::Task::local(async move {
            while let Some(request) = stream.try_next().await.expect("serve reverser") {
                self.metrics.request_count.add(1);
                self.metrics.global_request_count.add(1);

                let ReverserRequest::Reverse { input, responder } = request;
                let result = input.chars().rev().collect::<String>();
                responder.send(&result).expect("send reverse request response");

                self.metrics.response_count.add(1);
            }
            test_on_done();
        })
        .detach();
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        anyhow::Error,
        fidl_fuchsia_examples_inspect::{ReverserMarker, ReverserProxy},
        fuchsia_inspect::{self, assert_inspect_tree},
        futures::channel::oneshot,
    };

    #[fasync::run_singlethreaded(test)]
    async fn test_reverser() -> Result<(), Error> {
        let inspector = inspect::Inspector::new();

        let node = inspector.root().create_child("reverser_service");
        let factory = ReverserServerFactory::new(node);

        let (channel_closed_snd_1, channel_closed_rcv_1) = oneshot::channel::<()>();
        let reverser1 = open_reverser(&factory, || {
            channel_closed_snd_1.send(()).expect("send close event");
        })?;

        let (channel_closed_snd_2, channel_closed_rcv_2) = oneshot::channel::<()>();
        let reverser2 = open_reverser(&factory, || {
            channel_closed_snd_2.send(()).expect("send close event");
        })?;

        let result = reverser1.reverse("hello").await?;
        assert_eq!(result, "olleh");

        let result = reverser1.reverse("world").await?;
        assert_eq!(result, "dlrow");

        let result = reverser2.reverse("another").await?;
        assert_eq!(result, "rehtona");

        assert_inspect_tree!(inspector, root: {
            reverser_service: {
                total_requests: 3u64,
                connection_count: 2u64,
                "connection0": {
                    request_count: 2u64,
                    response_count: 2u64,
                },
                "connection1": {
                    request_count: 1u64,
                    response_count: 1u64,
                },
            }
        });

        drop(reverser1);
        channel_closed_rcv_1.await?;

        assert_inspect_tree!(inspector, root: {
            reverser_service: {
                total_requests: 3u64,
                connection_count: 2u64,
                "connection1": {
                    request_count: 1u64,
                    response_count: 1u64,
                },
            }
        });

        drop(reverser2);
        channel_closed_rcv_2.await?;

        Ok(())
    }

    fn open_reverser<F>(
        factory: &ReverserServerFactory,
        callback: F,
    ) -> Result<ReverserProxy, Error>
    where
        F: FnOnce() -> () + 'static,
    {
        let (proxy, stream) = fidl::endpoints::create_proxy_and_stream::<ReverserMarker>()?;
        factory.spawn_new_internal(stream, callback);
        Ok(proxy)
    }
}
