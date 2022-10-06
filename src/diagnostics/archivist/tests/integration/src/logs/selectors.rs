// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{constants, test_topology, utils};
use component_events::{events::*, matcher::*};
use diagnostics_reader::{assert_data_tree, ArchiveReader, Logs};
use fidl_fuchsia_component::BinderMarker;
use fidl_fuchsia_component::RealmMarker;
use fidl_fuchsia_component_decl::ChildRef;
use fidl_fuchsia_diagnostics::ArchiveAccessorMarker;
use fidl_fuchsia_io as fio;
use fuchsia_async as fasync;
use fuchsia_component::client;
use fuchsia_component_test::RealmInstance;
use futures::{FutureExt, StreamExt};

#[fuchsia::test]
async fn component_selectors_filter_logs() {
    let (builder, test_realm) = test_topology::create(test_topology::Options::default())
        .await
        .expect("create base topology");
    test_topology::add_lazy_child(&test_realm, "a", constants::LOG_AND_EXIT_COMPONENT_URL)
        .await
        .expect("add log_and_exit a");
    test_topology::add_lazy_child(&test_realm, "b", constants::LOG_AND_EXIT_COMPONENT_URL)
        .await
        .expect("add log_and_exit b");

    test_topology::expose_test_realm_protocol(&builder, &test_realm).await;
    let instance = builder.build().await.expect("create instance");
    let accessor =
        instance.root.connect_to_protocol_at_exposed_dir::<ArchiveAccessorMarker>().unwrap();

    let mut event_stream = EventStream::open().await.unwrap();

    // Start a few components.
    let mut child_ref_a = ChildRef { name: "a".to_string(), collection: None };
    let mut child_ref_b = ChildRef { name: "b".to_string(), collection: None };
    for _ in 0..3 {
        launch_and_wait_for_exit(&instance, &mut child_ref_a, &mut event_stream).await;
        launch_and_wait_for_exit(&instance, &mut child_ref_b, &mut event_stream).await;
    }

    // Start listening
    let mut reader = ArchiveReader::new();
    reader
        .add_selector(format!("realm_builder\\:{}/test/a:root", instance.root.child_name()))
        .with_archive(accessor)
        .with_minimum_schema_count(5)
        .retry_if_empty(true);

    let (mut stream, mut errors) =
        reader.snapshot_then_subscribe::<Logs>().unwrap().split_streams();
    let _errors = fasync::Task::spawn(async move {
        while let Some(e) = errors.next().await {
            panic!("error in subscription: {}", e);
        }
    });

    // Start a few more components
    for _ in 0..3 {
        launch_and_wait_for_exit(&instance, &mut child_ref_a, &mut event_stream).await;
        launch_and_wait_for_exit(&instance, &mut child_ref_b, &mut event_stream).await;
    }

    // We should see logs from components started before and after we began to listen.
    for _ in 0..6 {
        let log = stream.next().await.unwrap();
        assert_eq!(log.moniker, format!("realm_builder:{}/test/a", instance.root.child_name()));
        assert_data_tree!(log.payload.unwrap(), root: {
            message: {
                value: "Hello, world!",
            }
        });
    }
    // We only expect 6 logs.
    assert!(stream.next().now_or_never().is_none());
}

async fn launch_and_wait_for_exit(
    instance: &RealmInstance,
    child_ref: &mut ChildRef,
    event_stream: &mut EventStream,
) {
    let (exposed_dir, server_end) =
        fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
    let realm = instance.root.connect_to_protocol_at_exposed_dir::<RealmMarker>().unwrap();
    realm.open_exposed_dir(child_ref, server_end).await.unwrap().unwrap();

    let _ = client::connect_to_protocol_at_dir_root::<BinderMarker>(&exposed_dir).unwrap();

    utils::wait_for_component_stopped_event(
        instance.root.child_name(),
        &child_ref.name,
        ExitStatusMatcher::Clean,
        event_stream,
    )
    .await;
}
