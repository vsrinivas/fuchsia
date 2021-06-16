// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{constants::*, test_topology};
use component_events::{events::*, matcher::*};
use diagnostics_reader::{assert_data_tree, ArchiveReader, Logs, Severity};
use fidl_fuchsia_diagnostics::ArchiveAccessorMarker;
use fidl_fuchsia_sys2::EventSourceMarker;
use fuchsia_async::Task;
use fuchsia_component::client;
use futures::prelude::*;

#[fuchsia::test]
async fn logs_from_crashing_component() {
    let mut builder = test_topology::create(test_topology::Options::default())
        .await
        .expect("create base topology");
    test_topology::add_component(&mut builder, "log_and_crash", LOG_AND_CRASH_COMPONENT_URL)
        .await
        .expect("add log_and_exit");
    let instance = builder.build().create().await.expect("create instance");

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

    let event_source =
        EventSource::from_proxy(client::connect_to_protocol::<EventSourceMarker>().unwrap());
    let mut event_stream = event_source
        .subscribe(vec![EventSubscription::new(vec![Stopped::NAME], EventMode::Async)])
        .await
        .unwrap();
    EventMatcher::ok()
        .stop(Some(ExitStatusMatcher::AnyCrash))
        .moniker(format!(
            "./fuchsia_component_test_collection:{}:\\d+/test:\\d+/log_and_crash:\\d+",
            instance.root.child_name()
        ))
        .wait::<Stopped>(&mut event_stream)
        .await
        .unwrap();

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
