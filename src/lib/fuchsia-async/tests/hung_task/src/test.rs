// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use diagnostics_data::LogsField;
use diagnostics_log::PublishOptions;
use diagnostics_reader::{ArchiveReader, DiagnosticsHierarchy, Logs, Subscription};
use fidl_fuchsia_diagnostics::{Interest, Severity};
use fuchsia_async::{Task, Timer};
use futures::prelude::*;
use std::time::Duration;
use tracing::trace;

#[fuchsia_async::run_singlethreaded(test)]
async fn initialize_logging_and_find_hung_tasks() {
    // initialize logging at TRACE severity without spawning any tasks to the runtime
    let _ignore_external_severity_changes = diagnostics_log::init_publishing(PublishOptions {
        interest: Interest { min_severity: Some(Severity::Trace), ..Interest::EMPTY },
        ..Default::default()
    })
    .unwrap();

    // start listening to our own logs
    let mut events = EventsFromLogs::new().await;

    // send a plain test message through
    trace!("init'd");
    events.next().await.expect_message("init'd");

    // create a task with a known source location that will hang until we send to this channel
    let (send, recv) = futures::channel::oneshot::channel();
    let spawned_on_line = line!() + 1;
    let _spawned = Task::spawn(async move {
        trace!("waiting for oneshot channel message");
        recv.await.unwrap();
        trace!("received oneshot channel message");
    });
    // the above task should be logged as spawned from this file and the above statement
    let expected_source_prefix = format!("{}:{}:", file!(), spawned_on_line);

    // check that we got the task spawn event
    let expected_id;
    match events.next().await {
        LogEvent::TaskSpawned { id, source } => {
            if !source.starts_with(&expected_source_prefix) {
                panic!("expected source starting with {}, got {}", expected_source_prefix, source);
            }
            expected_id = id;
        }
        other => panic!("expected task spawn event, got {:?}", other),
    }

    // wait for the task to print its first log message
    events.next().await.expect_message("waiting for oneshot channel message");

    // unblock the task in a sub-scope so we can keep a mutable borrow into `events` for it
    {
        // make sure we haven't gotten any more messages yet by attempting to out-wait latency in
        // the logging pipeline. using a delay here means that legitimate failures will appear as
        // flakes, but given the lack of a "flush everything and wait" api from archivist i think
        // it's the best we can do for now
        let pending_until_channel_send = events.next();
        pin_utils::pin_mut!(pending_until_channel_send);
        Timer::new(Duration::from_secs(1)).await;
        assert!(futures::poll!(&mut pending_until_channel_send).is_pending());

        // allow the task to proceed, make sure we get both messages we expect
        send.send(()).unwrap();
        pending_until_channel_send.await.expect_message("received oneshot channel message");
    }

    match events.next().await {
        LogEvent::TaskCompleted { id, source } => {
            if !source.starts_with(&expected_source_prefix) {
                panic!("expected source starting with {}, got {}", expected_source_prefix, source);
            }
            assert_eq!(id, expected_id);
        }
        other => panic!("expected task complete event, got {:?}", other),
    }
}

#[derive(Debug)]
enum LogEvent {
    Message(String),
    TaskSpawned { id: u64, source: String },
    TaskCompleted { id: u64, source: String },
}

impl LogEvent {
    #[track_caller]
    fn expect_message(&self, expected: &str) {
        match self {
            LogEvent::Message(msg) => assert_eq!(msg, expected),
            other => panic!("expected string message, got {:?}", other),
        }
    }
}

struct EventsFromLogs {
    logs: Subscription<Logs>,
}

impl EventsFromLogs {
    async fn new() -> Self {
        let reader = ArchiveReader::new();
        let mut events = EventsFromLogs { logs: reader.snapshot_then_subscribe::<Logs>().unwrap() };

        // manually initializing the logging library with DEBUG or below emits this message
        events.next().await.expect_message("Logging initialized");
        // creating the log stream from the archivereader spawns a task, which creates a log message
        match events.next().await {
            LogEvent::TaskSpawned { id: _, source } => {
                // this is a hack that will break as soon as the library changes names, alas
                if !source.contains("diagnostics/reader") {
                    panic!("unrecognized source of diagnostics reader task: {}", source);
                }
            }
            other => panic!("unexpected log event: {:?}", other),
        }

        events
    }

    async fn next(&mut self) -> LogEvent {
        let next = self.logs.next().await.unwrap().unwrap();

        let payload = next.payload.unwrap();
        assert_eq!(payload.name, "root");

        match get_log_message(&payload) {
            "Task spawned" => {
                let (id, source) = get_task_id_and_source(&payload);
                LogEvent::TaskSpawned { id, source }
            }
            "Task completed" => {
                let (id, source) = get_task_id_and_source(&payload);
                LogEvent::TaskCompleted { id, source }
            }
            other => LogEvent::Message(other.to_string()),
        }
    }
}

fn get_log_message(payload: &DiagnosticsHierarchy<LogsField>) -> &str {
    payload.get_child("message").unwrap().get_property("value").unwrap().string().unwrap()
}

fn get_task_id_and_source(payload: &DiagnosticsHierarchy<LogsField>) -> (u64, String) {
    let keys = payload.get_child("keys").unwrap();
    let id = keys.get_property("id").unwrap().uint().unwrap();
    let source = keys.get_property("source").unwrap().string().unwrap().to_string();
    (*id as u64, source)
}
