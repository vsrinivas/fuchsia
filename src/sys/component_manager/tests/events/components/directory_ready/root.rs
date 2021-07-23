// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    component_events::{
        events::{DirectoryReady, Event, EventSource},
        matcher::EventMatcher,
    },
    fidl::endpoints::{create_proxy, DiscoverableProtocolMarker, ServerEnd},
    fidl_fidl_test_components as ftest,
    fidl_fuchsia_io::{self as fio, DirectoryProxy},
    files_async, fuchsia_async as fasync,
    futures::StreamExt,
    io_util,
    maplit::hashmap,
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

/// This component receives `DirectoryReady` events when its child makes them available.
/// Those directories contain a `Trigger` service that should be accessible when opening the
/// directory.
/// It sends "Saw: /path/to/dir on /some_moniker:0" for each successful read.
#[fasync::run_singlethreaded]
async fn main() {
    let event_source = EventSource::new().unwrap();
    let mut event_stream =
        event_source.take_static_event_stream("DirectoryReadyStream").await.unwrap();

    // For successful CapablityReady events, this is a map of the directory to expected contents
    let mut all_expected_entries = hashmap! {
        "normal".to_string() => vec![ftest::TriggerMarker::PROTOCOL_NAME.to_string()],
        "nested".to_string() => vec![format!("inner/{}", ftest::TriggerMarker::PROTOCOL_NAME).to_string()],
    };

    let mut err_event_names = vec!["insufficient_rights", "not_published"];

    for _ in 0..4 {
        let event = EventMatcher::default().expect_match::<DirectoryReady>(&mut event_stream).await;

        assert_eq!(
            event.component_url(),
            "fuchsia-pkg://fuchsia.com/events_integration_test#meta/directory_ready_child.cm"
        );
        assert_eq!(event.target_moniker(), "./child:0");

        match event.result() {
            Ok(payload) => {
                let expected_entries = all_expected_entries.remove(&payload.name).unwrap();

                // Open the directory and verify its contents
                let (node_clone, server_end) = fidl::endpoints::create_proxy().unwrap();
                payload.node.clone(fio::CLONE_FLAG_SAME_RIGHTS, server_end).unwrap();
                let directory = io_util::node_to_directory(node_clone).unwrap();
                let entries = list_entries(&directory).await;

                assert_eq!(entries, expected_entries);

                // Call the trigger service on each expected entry
                call_trigger(&directory, &expected_entries).await;
            }
            Err(error) => {
                let index = err_event_names.iter().position(|x| *x == error.name).unwrap();
                err_event_names.remove(index);
            }
        }
    }
}
