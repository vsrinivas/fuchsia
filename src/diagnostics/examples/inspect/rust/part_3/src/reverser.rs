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

// [START part_1_add_connection_count]
pub struct ReverserServerFactory {
    node: inspect::Node,
    // [START_EXCLUDE]
    request_count: Arc<inspect::UintProperty>,
    // [END_EXCLUDE]
    connection_count: inspect::UintProperty,
}

impl ReverserServerFactory {
    pub fn new(node: inspect::Node) -> Self {
        // [START_EXCLUDE]
        let request_count = Arc::new(node.create_uint("total_requests", 0));
        // [END_EXCLUDE]
        let connection_count = node.create_uint("connection_count", 0);
        Self {
            node,
            // [START_EXCLUDE]
            request_count,
            // [END_EXCLUDE]
            connection_count,
        }
    }

    pub fn spawn_new(&self, stream: ReverserRequestStream) {
        self.connection_count.add(1);
        // [END part_1_add_connection_count]
        let node = self.node.create_child(inspect::unique_name("connection"));
        let metrics = ReverserServerMetrics::new(node, self.request_count.clone());
        ReverserServer::new(metrics).spawn(stream);
    }
}

// The default implementation of ReverserServerMetrics is a no-op on the inspect properties. We use
// it for testing in this codelab. Future parts of the codelab will actually test our inspect
// writing.
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

    pub fn spawn(self, mut stream: ReverserRequestStream) {
        fasync::Task::local(async move {
            while let Some(request) = stream.try_next().await.expect("serve reverser") {
                self.metrics.request_count.add(1);
                self.metrics.global_request_count.add(1);

                let ReverserRequest::Reverse { input, responder } = request;
                let result = input.chars().rev().collect::<String>();
                responder.send(&result).expect("send reverse request response");

                self.metrics.response_count.add(1);
            }
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
        // CODELAB: Include the inspect test module.
    };

    #[fasync::run_singlethreaded(test)]
    async fn test_reverser() -> Result<(), Error> {
        let reverser = open_reverser()?;
        let result = reverser.reverse("hello").await?;
        assert_eq!(result, "olleh");
        // CODELAB: assert that the inspect data is correct.
        Ok(())
    }

    fn open_reverser() -> Result<ReverserProxy, Error> {
        let (proxy, stream) = fidl::endpoints::create_proxy_and_stream::<ReverserMarker>()?;
        let reverser = ReverserServer::new(ReverserServerMetrics::default());
        reverser.spawn(stream);
        Ok(proxy)
    }
}
