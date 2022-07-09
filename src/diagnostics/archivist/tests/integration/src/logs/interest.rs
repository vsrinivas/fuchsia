// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{constants::*, test_topology};
use diagnostics_data::LogsData;
use diagnostics_hierarchy::assert_data_tree;
use diagnostics_reader::{ArchiveReader, Error, Logs, Severity};
use fidl_fuchsia_diagnostics::{
    ArchiveAccessorMarker, Interest, LogInterestSelector, LogSettingsMarker,
    Severity as FidlSeverity,
};
use futures::{Stream, StreamExt};
use selectors::{self, VerboseError};

#[fuchsia::test]
async fn register_interest() {
    let (builder, test_realm) = test_topology::create(test_topology::Options::default())
        .await
        .expect("create test topology");
    test_topology::add_eager_child(&test_realm, "child", LOGGER_COMPONENT_FOR_INTEREST_URL)
        .await
        .expect("add child");
    let instance = builder.build().await.expect("create instance");

    let accessor = instance
        .root
        .connect_to_protocol_at_exposed_dir::<ArchiveAccessorMarker>()
        .expect("connect to archive accessor");
    let mut logs = ArchiveReader::new()
        .with_archive(accessor)
        .snapshot_then_subscribe::<Logs>()
        .expect("subscribe to logs");
    let log_settings = instance
        .root
        .connect_to_protocol_at_exposed_dir::<LogSettingsMarker>()
        .expect("connect to log settings");

    let expected_logs = vec![
        (Severity::Debug, "debug msg"),
        (Severity::Info, "info msg"),
        (Severity::Warn, "warn msg"),
        (Severity::Error, "error msg"),
    ];

    let selector = selectors::parse_component_selector::<VerboseError>(&format!(
        "realm_builder\\:{}/test/child",
        instance.root.child_name()
    ))
    .unwrap();

    // 1. Assert logs for default interest registration (info)
    assert_messages(&mut logs, &expected_logs[1..]).await;

    // 2. Interest registration with min_severity = debug
    let mut interests = vec![LogInterestSelector {
        selector: selector.clone(),
        interest: Interest { min_severity: Some(FidlSeverity::Debug), ..Interest::EMPTY },
    }];
    log_settings.register_interest(&mut interests.iter_mut()).expect("registered interest");

    // 3. Assert logs
    assert_messages(&mut logs, &expected_logs).await;

    // 4. Interest registration with min_severity = warn
    let mut interests = vec![LogInterestSelector {
        selector,
        interest: Interest { min_severity: Some(FidlSeverity::Warn), ..Interest::EMPTY },
    }];
    log_settings.register_interest(&mut interests.iter_mut()).expect("registered interest");

    // 5. Assert logs
    assert_messages(&mut logs, &expected_logs[2..]).await;

    // 6. Disconnecting the protocol, brings back an EMPTY interest, which defaults to INFO.
    drop(log_settings);
    assert_messages(&mut logs, &expected_logs[1..]).await;
}

async fn assert_messages<S>(mut logs: S, messages: &[(Severity, &str)])
where
    S: Stream<Item = Result<LogsData, Error>> + std::marker::Unpin,
{
    for (expected_severity, expected_msg) in messages {
        let log = logs.next().await.expect("got log response").expect("log isn't an error");
        assert_eq!(log.metadata.component_url, Some(LOGGER_COMPONENT_FOR_INTEREST_URL.to_string()));
        assert_eq!(log.metadata.severity, *expected_severity);
        assert_data_tree!(log.payload.unwrap(), root: contains {
            message: {
                value: expected_msg.to_string(),
            }
        });
    }
}
