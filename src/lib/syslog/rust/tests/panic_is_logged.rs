// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_logger::{LogFilterOptions, LogLevelFilter, LogMarker, LogMessage},
    fuchsia_async as fasync,
    fuchsia_component::client as fclient,
    fuchsia_component_test::{Capability, ChildOptions, RealmBuilder, Ref, Route},
    validating_log_listener,
};

#[fasync::run_singlethreaded(test)]
async fn listen_for_syslog() {
    let log_proxy =
        fclient::connect_to_protocol::<LogMarker>().expect("failed to connect to log server");

    let builder = RealmBuilder::new().await.expect("failed to create a realm builder");
    let child = builder
        .add_child("panicker", "#meta/panicker.cm", ChildOptions::new().eager())
        .await
        .expect("failed to create child component");
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                .from(Ref::parent())
                .to(&child),
        )
        .await
        .expect("failed to route capability");
    let _realm = builder.build_with_name("panic-is-logged").await.expect("failed to create realm");

    // only interested in catching the panic log
    let options = LogFilterOptions {
        filter_by_pid: false,
        pid: 0,
        filter_by_tid: false,
        tid: 0,
        verbosity: 0,
        min_severity: LogLevelFilter::Error,
        tags: Vec::new(),
    };

    validating_log_listener::validate_log_stream(
        vec![LogMessage {
            // these are ignored by validating-log-listener
            pid: 0,
            tid: 0,
            time: 0,
            dropped_logs: 0,

            // these are checked
            severity: LogLevelFilter::Error.into_primitive().into(),
            tags: vec!["panicker".to_string()],
            msg: "PANIC: oh no, I panicked".to_string(),
        }],
        log_proxy,
        Some(options),
    )
    .await;
}
