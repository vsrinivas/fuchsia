// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    diagnostics_reader::{ArchiveReader, Inspect, Logs},
    fuchsia_async as fasync,
    fuchsia_component::client::ScopedInstance,
    fuchsia_inspect::testing::{assert_inspect_tree, AnyProperty},
    futures::StreamExt,
};

const URL: &'static str =
    "fuchsia-pkg://fuchsia.com/test-manager-diagnostics-tests#meta/diagnostics-publisher.cm";

#[fasync::run_singlethreaded(test)]
async fn test_isolated_diagnostics_can_be_read_by_the_test() {
    let mut instance =
        ScopedInstance::new("coll".into(), URL.into()).await.expect("Created instance");

    // Read inspect
    let data = ArchiveReader::new()
        .add_selector(r#"coll\:auto-*:root"#)
        .snapshot::<Inspect>()
        .await
        .expect("got inspect data");
    assert_eq!(data.len(), 1);
    assert_eq!(data[0].moniker, format!(r#"coll\:{}"#, instance.child_name()));
    assert_inspect_tree!(data[0].payload.as_ref().unwrap(), root: {
        "fuchsia.inspect.Health": {
            status: "OK",
            start_timestamp_nanos: AnyProperty,
        }
    });

    // Read logs
    let (subscription, mut errors) =
        ArchiveReader::new().snapshot_then_subscribe::<Logs>().expect("subscribed").split_streams();
    fasync::Task::spawn(async move {
        if let Some(error) = errors.next().await {
            panic!("Got error: {:?}", error);
        }
    })
    .detach();
    let logs_fut = async move {
        let logs = subscription.take(2).collect::<Vec<_>>().await;
        let messages =
            logs.into_iter().map(|log| log.msg().unwrap().to_owned()).collect::<Vec<_>>();
        assert_eq!(
            messages,
            vec!["Started diagnostics publisher ".to_owned(), "Finishing through Stop ".to_owned()]
        );
    };

    let destroy_fut = instance.take_destroy_waiter();
    drop(instance);
    destroy_fut.await;

    logs_fut.await;
}
