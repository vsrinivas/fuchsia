// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{constants::*, test_topology};
use cm_rust::{ExposeDecl, ExposeProtocolDecl, ExposeSource, ExposeTarget};
use component_events::{events::*, matcher::*};
use diagnostics_reader::{assert_data_tree, ArchiveReader, Data, Logs};
use fidl_fuchsia_diagnostics::ArchiveAccessorMarker;
use fidl_fuchsia_io::DirectoryMarker;
use fidl_fuchsia_sys2::{ChildRef, EventSourceMarker, RealmMarker};
use fuchsia_async as fasync;
use fuchsia_component::client;
use fuchsia_component_test::Moniker;
use futures::StreamExt;

#[fuchsia::test]
async fn test_logs_lifecycle() {
    let mut builder = test_topology::create(test_topology::Options::default())
        .await
        .expect("create base topology");
    test_topology::add_component(&mut builder, "log_and_exit", LOG_AND_EXIT_COMPONENT_URL)
        .await
        .expect("add log_and_exit");

    // Currently RealmBuilder doesn't support to expose a capability from framework, therefore we
    // manually update the decl that the builder creates.
    let mut realm = builder.build();
    let mut test_decl = realm.get_decl(&"test".into()).await.unwrap();
    test_decl.exposes.push(ExposeDecl::Protocol(ExposeProtocolDecl {
        source: ExposeSource::Framework,
        source_name: "fuchsia.sys2.Realm".into(),
        target: ExposeTarget::Parent,
        target_name: "fuchsia.sys2.Realm".into(),
    }));
    realm.set_component(&"test".into(), test_decl).await.unwrap();
    let mut root_decl = realm.get_decl(&Moniker::root()).await.unwrap();
    root_decl.exposes.push(ExposeDecl::Protocol(cm_rust::ExposeProtocolDecl {
        source: ExposeSource::Child("test".to_string()),
        source_name: "fuchsia.sys2.Realm".into(),
        target: ExposeTarget::Parent,
        target_name: "fuchsia.sys2.Realm".into(),
    }));
    realm.set_component(&Moniker::root(), root_decl).await.unwrap();

    let instance = realm.create().await.expect("create instance");
    let accessor =
        instance.root.connect_to_protocol_at_exposed_dir::<ArchiveAccessorMarker>().unwrap();

    let mut reader = ArchiveReader::new();
    reader
        .with_archive(accessor)
        .with_minimum_schema_count(0) // we want this to return even when no log messages
        .retry_if_empty(false);

    let (mut subscription, mut errors) =
        reader.snapshot_then_subscribe::<Logs>().unwrap().split_streams();
    let _log_errors = fasync::Task::spawn(async move {
        if let Some(error) = errors.next().await {
            panic!("{:#?}", error);
        }
    });

    let moniker = format!(
        "fuchsia_component_test_collection:{}/test/log_and_exit",
        instance.root.child_name()
    );

    let event_source =
        EventSource::from_proxy(client::connect_to_protocol::<EventSourceMarker>().unwrap());

    let mut event_stream = event_source
        .subscribe(vec![EventSubscription::new(vec![Stopped::NAME], EventMode::Async)])
        .await
        .unwrap();

    let mut child_ref = ChildRef { name: "log_and_exit".to_string(), collection: None };
    for i in 1..100 {
        // launch our child and wait for it to exit before asserting on its logs
        let (_client_end, server_end) =
            fidl::endpoints::create_endpoints::<DirectoryMarker>().unwrap();
        let realm = instance.root.connect_to_protocol_at_exposed_dir::<RealmMarker>().unwrap();
        realm.bind_child(&mut child_ref, server_end).await.unwrap().unwrap();

        EventMatcher::ok()
            .stop(Some(ExitStatusMatcher::Clean))
            .moniker(format!(
                "./fuchsia_component_test_collection:{}:\\d+/test:\\d+/log_and_exit:\\d+",
                instance.root.child_name()
            ))
            .wait::<Stopped>(&mut event_stream)
            .await
            .unwrap();

        check_message(&moniker, subscription.next().await.unwrap());

        let all_messages = reader.snapshot::<Logs>().await.unwrap();
        assert_eq!(all_messages.len(), i, "must have 1 message per launch");

        for message in all_messages {
            check_message(&moniker, message);
        }
    }
}

fn check_message(expected_moniker: &str, message: Data<Logs>) {
    assert_eq!(message.moniker, expected_moniker,);
    assert_eq!(message.metadata.component_url, LOG_AND_EXIT_COMPONENT_URL);

    assert_data_tree!(message.payload.unwrap(), root: {
        message: {
            value: "Hello, world!",
        }
    });
}
