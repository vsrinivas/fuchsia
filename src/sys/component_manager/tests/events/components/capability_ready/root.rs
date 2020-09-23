// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::endpoints::{create_proxy, DiscoverableService, ServerEnd},
    fidl_fidl_examples_routing_echo as fecho, fidl_fidl_test_components as ftest,
    fidl_fuchsia_io::{self as fio, DirectoryProxy},
    files_async, fuchsia_async as fasync,
    fuchsia_component::{client::connect_to_service, server::ServiceFs},
    futures::StreamExt,
    io_util,
    maplit::hashmap,
    std::collections::HashSet,
    test_utils_lib::events::{CapabilityReady, Event, EventMatcher, EventSource, Handler},
};

async fn list_entries(directory: &DirectoryProxy) -> Vec<String> {
    files_async::readdir_recursive(&directory, /*timeout=*/ None)
        .map(|entry_result| entry_result.expect("entry ok").name)
        .collect::<Vec<_>>()
        .await
}

async fn call_trigger(directory: &DirectoryProxy, paths: &Vec<String>) {
    for path in paths {
        let (trigger, server_end) = create_proxy::<ftest::TriggerMarker>().unwrap();
        directory
            .open(
                fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE,
                fio::MODE_TYPE_SERVICE,
                path,
                ServerEnd::new(server_end.into_channel()),
            )
            .expect("open dir");
        // We're only interested in this function successfully returning, we don't care about the
        // contents of the string returned.
        let _ = trigger.run().await.expect("call trigger");
    }
}

/// This component receives `CapabilityReady` events when its child makes them available.
/// Those directories contain a `Trigger` service that should be accessible when opening the
/// directory.
/// It sends "Saw: /path/to/dir on /some_moniker:0" for each successful read.
#[fasync::run_singlethreaded]
async fn main() {
    let fs = ServiceFs::new_local();
    let event_source = EventSource::new_sync().unwrap();
    let mut event_stream = event_source.subscribe(vec![CapabilityReady::NAME]).await.unwrap();

    event_source.start_component_tree().await;

    let echo = connect_to_service::<fecho::EchoMarker>().unwrap();

    let expected_entries = hashmap! {
        "foo".to_string() => vec![ftest::TriggerMarker::SERVICE_NAME.to_string()],
        "bar".to_string() => vec![format!("baz/{}", ftest::TriggerMarker::SERVICE_NAME).to_string()],
    };

    let mut seen = HashSet::new();

    while seen.len() != 3 {
        let event = event_stream.expect_match::<CapabilityReady>(EventMatcher::new()).await;
        let (node_clone, server_end) = fidl::endpoints::create_proxy().expect("create proxy");
        match &event.result {
            Ok(payload) if !seen.contains(&payload.path) => {
                payload.node.clone(fio::CLONE_FLAG_SAME_RIGHTS, server_end).expect("clone node");
                let directory = io_util::node_to_directory(node_clone).expect("node to directory");

                let entries = list_entries(&directory).await;
                assert_eq!(&entries, expected_entries.get(&payload.path).expect("entries"));

                call_trigger(&directory, expected_entries.get(&payload.path).expect("entries"))
                    .await;

                let _ = echo
                    .echo_string(Some(&format!(
                        "[{}] Saw {} on {}",
                        event.component_url(),
                        payload.path,
                        event.target_moniker()
                    )))
                    .await;
                seen.insert(payload.path.clone());
            }
            Err(error) if !seen.contains(&error.path) => {
                let _ = echo
                    .echo_string(Some(&format!(
                        "[{}] error {} on {}",
                        event.component_url(),
                        error.path,
                        event.target_moniker()
                    )))
                    .await;
                seen.insert(error.path.clone());
            }
            _ => {}
        }
        event.resume().await.unwrap();
    }

    fs.collect::<()>().await;
}
