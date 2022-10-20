// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use anyhow::Error;
use fuchsia_async as fasync;
use fuchsia_async::TimeoutExt;
use fuchsia_component_test::{Capability, ChildOptions, RealmBuilder, Ref, Route};

use fidl_fuchsia_logger::{
    LogFilterOptions, LogLevelFilter, LogListenerSafeRequest, LogListenerSafeRequestStream,
    LogMarker,
};
use fuchsia_component::client::connect_to_protocol;
use fuchsia_zircon::Duration;

use futures::{channel::mpsc, StreamExt, TryStreamExt};

const STARTUP_LOG_MSG: &str = "recovery: started";

async fn set_up_realm() -> Result<RealmBuilder, Error> {
    let builder = RealmBuilder::new().await?;

    let system_recovery = builder
        .add_child("system_recovery", "#meta/system_recovery.cm", ChildOptions::new().eager())
        .await?;

    // Offer logsink to recovery, so we can see logs from it.
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                .from(Ref::parent())
                .to(&system_recovery),
        )
        .await?;

    Ok(builder)
}

async fn setup_log_listener(
    mut stream: LogListenerSafeRequestStream,
    sender: mpsc::Sender<String>,
) -> Result<(), fidl::Error> {
    let mut sender = sender.clone();
    while let Some(request) = stream.try_next().await? {
        match request {
            LogListenerSafeRequest::Log { log, responder } => {
                if log.msg == STARTUP_LOG_MSG.to_string() {
                    sender.start_send(log.msg).unwrap();
                }
                responder.send().ok();
            }
            LogListenerSafeRequest::LogMany { log: log_messages, responder } => {
                // LogMany is called on listen_safe to dump cached messages.
                // Check for recovery logs here in case the component started before the listener.
                for log in log_messages {
                    if log.msg == STARTUP_LOG_MSG.to_string() {
                        sender.start_send(log.msg).unwrap();
                    }
                }
                responder.send().ok();
            }
            LogListenerSafeRequest::Done { control_handle: _ } => {
                return Ok(());
            }
        }
    }
    Ok(())
}

#[fuchsia::test]
async fn test_startup() -> Result<(), Error> {
    let test_timeout_seconds = 10;
    // Set up mpsc to get log messages from the log listener.
    let (sender, mut receiver) = mpsc::channel(1);

    // Set up log listener to filter by system recovery logs only.
    fasync::Task::local(async move {
        let (listener_client, listener_server) = fidl::endpoints::create_request_stream().unwrap();
        let log_proxy = connect_to_protocol::<LogMarker>().unwrap();
        let mut filter = LogFilterOptions {
            filter_by_pid: false,
            pid: 0,
            min_severity: LogLevelFilter::None,
            verbosity: 0,
            filter_by_tid: false,
            tid: 0,
            tags: vec![String::from("system_recovery")],
        };

        log_proxy.listen_safe(listener_client, Some(&mut filter)).unwrap();
        setup_log_listener(listener_server, sender).await.unwrap();
    })
    .detach();

    // The recovery component is added as eager. Calling build() on the realm will start it.
    let builder = set_up_realm().await.unwrap();
    let _realm = builder.build().await.unwrap();

    let recovery_started_msg = receiver
        .next()
        .on_timeout(Duration::from_seconds(test_timeout_seconds), || {
            Some("test_startup timed out".to_string())
        })
        .await
        .unwrap();

    assert_eq!(recovery_started_msg, STARTUP_LOG_MSG.to_string());

    Ok(())
}
