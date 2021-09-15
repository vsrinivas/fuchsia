// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{self, Context};
use fidl_fidl_examples_routing_echo::{EchoRequest, EchoRequestStream};
use fuchsia_component::server::ServiceFs;
use fuchsia_inspect::health::Reporter;
use fuchsia_inspect::NumericProperty;
use futures::prelude::*;

enum IncomingRequest {
    Echo(EchoRequestStream),
}

#[fuchsia::component(logging = false)]
async fn main() -> Result<(), anyhow::Error> {
    let mut service_fs = ServiceFs::new_local();

    // [START initialization]
    // This creates the root of an Inspect tree
    // The Inspector is a singleton that you can access from any scope
    let inspector = fuchsia_inspect::component::inspector();
    // This serves the Inspect tree to the default path
    inspect_runtime::serve(inspector, &mut service_fs)?;
    // [END initialization]

    // [START health_check]
    fuchsia_inspect::component::health().set_starting_up();

    // [START_EXCLUDE]
    // Serve the echo protocol
    service_fs.dir("svc").add_fidl_service(IncomingRequest::Echo);
    service_fs.take_and_serve_directory_handle().context("failed to serve outgoing namespace")?;
    // [END_EXCLUDE]

    fuchsia_inspect::component::health().set_ok();
    // [END health_check]

    // [START server_stats]
    // [START properties]
    // Attach properties to the root node of the tree
    let root_node = inspector.root();
    let total_requests = root_node.create_uint("total_requests", 0);
    let bytes_processed = root_node.create_uint("bytes_processed", 0);
    // [END properties]
    let stats = EchoConnectionStats { total_requests, bytes_processed };

    // Begin serving to handle incoming requests
    service_fs
        .for_each_concurrent(None, |_request: IncomingRequest| async {
            match _request {
                IncomingRequest::Echo(stream) => handle_echo_request(stream, &stats).await,
            }
        })
        .await;
    // [END server_stats]

    Ok(())
}

// [START handler]
// Inspect properties managed by the server
struct EchoConnectionStats {
    total_requests: fuchsia_inspect::UintProperty,
    bytes_processed: fuchsia_inspect::UintProperty,
}

// Handler for incoming service requests
async fn handle_echo_request(mut stream: EchoRequestStream, stats: &EchoConnectionStats) {
    while let Some(event) = stream.try_next().await.expect("failed to serve echo service") {
        let EchoRequest::EchoString { value, responder } = event;
        responder.send(value.as_ref().map(|s| &**s)).expect("failed to send echo response");

        if let Some(message) = value {
            // Update Inspect property values
            stats.total_requests.add(1);
            stats.bytes_processed.add(message.len() as u64);
        }
    }
}
// [END handler]

#[cfg(test)]
mod tests {
    use super::{handle_echo_request, EchoConnectionStats};
    use fidl_fidl_examples_routing_echo::EchoMarker;

    #[fuchsia::test]
    async fn echo_server_writes_stats() {
        // [START inspect_test]
        // Get a reference to the root node of the Inspect tree
        let inspector = fuchsia_inspect::component::inspector();

        // [START_EXCLUDE]
        // Create connection stats properties
        let root_node = inspector.root();
        let stats = EchoConnectionStats {
            total_requests: root_node.create_uint("total_requests", 0),
            bytes_processed: root_node.create_uint("bytes_processed", 0),
        };

        // Invoke the echo server
        let (proxy, stream) = fidl::endpoints::create_proxy_and_stream::<EchoMarker>().unwrap();
        fuchsia_async::Task::spawn(async move {
            handle_echo_request(stream, &stats).await;
        })
        .detach();

        let message = String::from("Hello World!");
        proxy.echo_string(Some(&message)).await.unwrap();
        proxy.echo_string(Some(&message)).await.unwrap();
        // [END_EXCLUDE]

        // Validate the contents of the tree match
        fuchsia_inspect::assert_data_tree!(inspector, root: {
            total_requests: 2u64,
            bytes_processed: 24u64,
        });
        // [END inspect_test]
    }
}
