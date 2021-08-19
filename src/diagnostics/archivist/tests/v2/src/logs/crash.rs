// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{constants::*, test_topology, utils};
use component_events::matcher::ExitStatusMatcher;
use diagnostics_reader::{assert_data_tree, ArchiveReader, Logs, Severity};
use fidl_fuchsia_component as fcomponent;
use fidl_fuchsia_diagnostics::ArchiveAccessorMarker;
use fidl_fuchsia_io::DirectoryMarker;
use fidl_fuchsia_sys2::{ChildRef, RealmMarker};
use fuchsia_async::Task;
use futures::prelude::*;

#[fuchsia::test]
async fn logs_from_crashing_component() {
    let mut builder = test_topology::create(test_topology::Options::default())
        .await
        .expect("create base topology");
    test_topology::add_component(&mut builder, "log_and_crash", LOG_AND_CRASH_COMPONENT_URL)
        .await
        .expect("add log_and_exit");

    let mut realm = builder.build();
    test_topology::expose_test_realm_protocol(&mut realm).await;
    let instance = realm.create().await.expect("create instance");

    let accessor =
        instance.root.connect_to_protocol_at_exposed_dir::<ArchiveAccessorMarker>().unwrap();
    let mut reader = ArchiveReader::new();
    reader.with_archive(accessor);
    let (mut logs, mut errors) = reader.snapshot_then_subscribe::<Logs>().unwrap().split_streams();
    let _errors = Task::spawn(async move {
        while let Some(e) = errors.next().await {
            panic!("error in subscription: {}", e);
        }
    });

    let mut child_ref = ChildRef { name: "log_and_crash".to_string(), collection: None };
    // launch our child and wait for it to exit before asserting on its logs
    let (exposed_dir, server_end) = fidl::endpoints::create_proxy::<DirectoryMarker>().unwrap();
    let realm = instance.root.connect_to_protocol_at_exposed_dir::<RealmMarker>().unwrap();
    realm.open_exposed_dir(&mut child_ref, server_end).await.unwrap().unwrap();
    let _ = fuchsia_component::client::connect_to_protocol_at_dir_root::<fcomponent::BinderMarker>(
        &exposed_dir,
    )
    .unwrap();

    utils::wait_for_component_stopped(
        &instance.root.child_name(),
        "log_and_crash",
        ExitStatusMatcher::AnyCrash,
    )
    .await;

    let crasher_info = logs.next().await.unwrap();
    assert_eq!(crasher_info.metadata.severity, Severity::Info);
    assert_data_tree!(crasher_info.payload.unwrap(), root:{"message": contains {
        "value": "crasher has initialized",
    }});

    let crasher_warn = logs.next().await.unwrap();
    assert_eq!(crasher_warn.metadata.severity, Severity::Warn);
    assert_data_tree!(crasher_warn.payload.unwrap(), root:{"message": contains {
        "value": "crasher is approaching the crash",
    }});

    let crasher_error = logs.next().await.unwrap();
    assert_eq!(crasher_error.metadata.severity, Severity::Error);
    assert_data_tree!(crasher_error.payload.unwrap(), root:{"message": contains {
        "value": "oh no we're crashing",
    }});
}
