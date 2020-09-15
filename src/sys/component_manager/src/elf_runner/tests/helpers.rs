// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_sys2::{Event, EventPayload, EventResult, StoppedPayload};
use regex::Regex;
use std::convert::TryFrom;
use test_utils_lib::events::{EventDescriptor, EventMatcher, EventStream};

#[derive(Debug, PartialEq, Eq, Clone)]
/// Simplifies the exit status represented by an Event. All stop status values
/// that indicate failure are crushed into `Crash`. If the Event is not a stop
/// event its `ExitStatus` value is `NotExitEvent`.
pub enum ExitStatus {
    Clean,
    Crash,
    NotExitEvent,
}

#[derive(Debug)]
pub struct ComponentStatus {
    pub child_name: Regex,
    pub exit_status: ExitStatus,
}

/// Validate that for all components whose moniker matches the `moniker_regxs`
/// the exit status of that component is `expected_status`. This function all
/// `events` have been received. This function panics if the event stream closes
/// before all events are received or if any of the components specified in
/// `moniker_regxs` exit with a status that does not match `expected_status`.
pub async fn validate_exit(
    stream: &mut EventStream,
    events: &mut Vec<EventMatcher>,
    moniker_regxs: Vec<Regex>,
    expected_status: ExitStatus,
) {
    let mut statuses: Vec<ComponentStatus> = vec![];
    for r in moniker_regxs {
        statuses.push(ComponentStatus { child_name: r, exit_status: expected_status.clone() });
    }

    get_component_exit_status(stream, events, &mut statuses).await;

    for status in &statuses {
        assert_eq!(
            status.exit_status, expected_status,
            "At least one component exited unexpectedly, expected {:?}, exit statuses: {:?}",
            expected_status, statuses
        );
    }
}

/// Given an `EventStream` validate that the expected `events` are received in
/// the order that they are specified in. It is legal for other events to
/// appear in the event stream, these are simply ignored. From the expected
/// events `ExitStatus` is extracted for events whose associated component
/// moniker matches the regex in one of the `ComponentStatus` structs in
/// `comps`. This function completes when all events in `events` are received.
/// This function consumes matching events from `events`. This function panics
/// if the event stream closes before all events are received.
pub async fn get_component_exit_status(
    stream: &mut EventStream,
    events: &mut Vec<EventMatcher>,
    comps: &mut Vec<ComponentStatus>,
) {
    loop {
        match stream.next().await {
            Ok(event) => {
                let expected = events.get(0).expect("unexpectedly out of events!");
                let actual = EventDescriptor::try_from(&event)
                    .expect("failed to convert event into descriptor");
                if expected.matches(&actual) {
                    let event_moniker = actual.target_moniker.expect("event moniker empty").clone();
                    let exit_status = is_clean_exit(&event);

                    match exit_status {
                        ExitStatus::Clean | ExitStatus::Crash => {
                            for r in &mut *comps {
                                if r.child_name.is_match(&event_moniker) {
                                    r.exit_status = exit_status;
                                    break;
                                }
                            }
                        }
                        ExitStatus::NotExitEvent => {
                            //continue
                        }
                    }
                    events.remove(0);
                    if events.is_empty() {
                        break;
                    }
                }
            }
            e => panic!("Unexpected event received from event stream {:?}", e),
        }
    }
}

pub fn is_clean_exit(event: &Event) -> ExitStatus {
    match event {
        Event {
            event_result:
                Some(EventResult::Payload(EventPayload::Stopped(StoppedPayload {
                    status: Some(exit_status),
                }))),
            ..
        } => {
            if *exit_status == 0 {
                ExitStatus::Clean
            } else {
                ExitStatus::Crash
            }
        }
        _ => ExitStatus::NotExitEvent,
    }
}
