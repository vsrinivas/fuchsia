// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl::endpoints::{create_proxy, DiscoverableService, ServerEnd},
    fidl_fidl_examples_routing_echo as fecho, fidl_fidl_test_components as ftest,
    fidl_fuchsia_io::{self as fio, DirectoryProxy},
    files_async, fuchsia_async as fasync,
    fuchsia_component::{client::connect_to_service, server::ServiceFs},
    futures::StreamExt,
    io_util,
    maplit::hashmap,
    test_utils_lib::events::{CapabilityReady, Event, EventSource, Handler},
};

async fn list_entries(directory: &DirectoryProxy) -> Vec<String> {
    files_async::readdir_recursive(&directory, /*timeout=*/ None)
        .await
        .expect("read entries")
        .into_iter()
        .map(|entry| entry.name)
        .collect::<Vec<_>>()
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
        trigger.run().await.expect("call trigger")
    }
}

/// This component receives `CapabilityReady` events when its child makes them available.
/// Those directories contain a `Trigger` service that should be accessible when opening the
/// directory.
/// It sends "Saw: /path/to/dir on /some_moniker:0" for each successful read.
#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let fs = ServiceFs::new_local();
    let event_source = EventSource::new_sync()?;
    let mut event_stream = event_source.subscribe(vec![CapabilityReady::TYPE]).await?;

    event_source.start_component_tree().await?;

    let echo = connect_to_service::<fecho::EchoMarker>()?;

    let expected_entries = hashmap! {
        "/foo".to_string() => vec![ftest::TriggerMarker::SERVICE_NAME.to_string()],
        "/bar".to_string() => vec![format!("baz/{}", ftest::TriggerMarker::SERVICE_NAME).to_string()],
    };

    for _ in 0..2 {
        let event = event_stream.expect_type::<CapabilityReady>().await?;
        let (node_clone, server_end) = fidl::endpoints::create_proxy().expect("create proxy");
        event.node.clone(fio::CLONE_FLAG_SAME_RIGHTS, server_end).expect("clone node");
        let directory = io_util::node_to_directory(node_clone).expect("node to directory");

        let entries = list_entries(&directory).await;
        assert_eq!(&entries, expected_entries.get(&event.path).expect("entries"));

        call_trigger(&directory, expected_entries.get(&event.path).expect("entries")).await;

        let _ = echo
            .echo_string(Some(&format!("Saw {} on {}", event.path, event.target_moniker())))
            .await;
        event.resume().await?;
    }

    fs.collect::<()>().await;
    Ok(())
}
