// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_examples_inspect::{ReverserRequest, ReverserRequestStream},
    fuchsia_async as fasync,
    fuchsia_inspect::NumericProperty,
    futures::TryStreamExt,
    std::sync::Arc,
};

// [START part_1_use]
use fuchsia_inspect as inspect;
// [END part_1_use]

// [START part_1_update_reverser]
pub struct ReverserServerFactory {
    node: inspect::Node,
    // [START_EXCLUDE]
    request_count: Arc<inspect::UintProperty>,
    connection_count: inspect::UintProperty,
    // [END_EXCLUDE]
}

impl ReverserServerFactory {
    pub fn new(node: inspect::Node) -> Self {
        // [START EXCLUDE]
        let request_count = Arc::new(node.create_uint("total_requests", 0));
        let connection_count = node.create_uint("connection_count", 0);
        // [END_EXCLUDE]
        Self {
            node,
            // [START_EXCLUDE]
            request_count,
            connection_count, // [END_EXCLUDE]}
        }
    }

    // [START_EXCLUDE]
    pub fn spawn_new(&self, stream: ReverserRequestStream) {
        // [START part_1_connection_child]
        let node = self.node.create_child(inspect::unique_name("connection"));
        // [END part_1_connection_child]
        let metrics = ReverserServerMetrics::new(node, self.request_count.clone());
        ReverserServer::new(metrics).spawn(stream);
        self.connection_count.add(1);
    }
    // [END_EXCLUDE]
}
// [END part_1_update_reverser]

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
                // [START part_1_respond]
                responder.send(&result).expect("send reverse request response");
                // [END part_1_respond]

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
    };

    #[fasync::run_singlethreaded(test)]
    async fn test_reverser() -> Result<(), Error> {
        let reverser = open_reverser()?;
        let result = reverser.reverse("hello").await?;
        assert_eq!(result, "olleh");
        Ok(())
    }

    fn open_reverser() -> Result<ReverserProxy, Error> {
        // [START open_reverser]
        let (proxy, stream) = fidl::endpoints::create_proxy_and_stream::<ReverserMarker>()?;
        let reverser = ReverserServer::new(ReverserServerMetrics::default());
        reverser.spawn(stream);
        // [END open_reverser]
        Ok(proxy)
    }
}
