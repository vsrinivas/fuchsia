// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use diagnostics_data::{Data, Logs};
use diagnostics_reader::ArchiveReader;
use fidl::endpoints::create_endpoints;
use fidl_fuchsia_io::DirectoryMarker;
use fidl_fuchsia_sys2::ChildRef;
use fuchsia_async::{OnSignals, Task};
use fuchsia_zircon as zx;
use futures::StreamExt;

const LOGS_WHEN_LAUNCHED_URL: &str =
    "fuchsia-pkg://fuchsia.com/test-logs-lifecycle#meta/logs-when-launched.cm";

#[fuchsia_async::run_singlethreaded(test)]
async fn test_logs_lifecycle() {
    fuchsia_syslog::init().unwrap();
    fuchsia_syslog::set_severity(fuchsia_syslog::levels::DEBUG);

    let reader = ArchiveReader::new()
        .with_minimum_schema_count(0) // we want this to return even when no log messages
        .retry_if_empty(false);

    let (mut subscription, mut errors) =
        reader.snapshot_then_subscribe::<Logs>().unwrap().split_streams();
    let _log_errors = Task::spawn(async move {
        if let Some(error) = errors.next().await {
            panic!("{:#?}", error);
        }
    });

    let realm = fuchsia_component::client::realm().unwrap();
    let mut child_ref = ChildRef { name: "logs_when_launched".to_string(), collection: None };

    for i in 1..100 {
        // launch our child and wait for it to exit before asserting on its logs
        let (client_end, server_end) = create_endpoints::<DirectoryMarker>().unwrap();
        realm.bind_child(&mut child_ref, server_end).await.unwrap().unwrap();
        OnSignals::new(&client_end, zx::Signals::CHANNEL_PEER_CLOSED).await.unwrap();

        check_message(subscription.next().await.unwrap());

        let all_messages = reader.snapshot::<Logs>().await.unwrap();
        assert_eq!(all_messages.len(), i, "must have 1 message per launch");

        for message in all_messages {
            check_message(message);
        }
    }
}

fn check_message(message: Data<Logs>) {
    assert!(message.moniker == "logs_when_launched");
    assert_eq!(message.metadata.component_url, LOGS_WHEN_LAUNCHED_URL);

    let payload = message.payload_message().unwrap();
    assert_eq!(payload.get_property("value").unwrap().string().unwrap(), "Hello, world! ");
}
