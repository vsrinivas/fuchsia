// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_test::{
        CaseListenerRequest::Finished,
        Invocation, Result_ as TestResult,
        RunListenerRequest::{OnFinished, OnTestCaseStarted},
        RunListenerRequestStream,
    },
    std::collections::HashMap,
    {anyhow::Error, futures::prelude::*},
};

#[derive(PartialEq, Debug)]
pub enum ListenerEvent {
    StartTest(String),
    FinishTest(String, TestResult),
    FinishAllTests,
}

fn get_ord_index_and_name(event: &ListenerEvent) -> (usize, &str) {
    match event {
        ListenerEvent::StartTest(name) => (0, name),
        ListenerEvent::FinishTest(name, _) => (1, name),
        ListenerEvent::FinishAllTests => (2, ""),
    }
}

// Orders by test name and then event type.
impl Ord for ListenerEvent {
    fn cmp(&self, other: &Self) -> core::cmp::Ordering {
        let (s_index, s_test_name) = get_ord_index_and_name(self);
        let (o_index, o_test_name) = get_ord_index_and_name(other);
        if s_test_name == o_test_name || s_index == 2 || o_index == 2 {
            return s_index.cmp(&o_index);
        }
        return s_test_name.cmp(&o_test_name);
    }
}

// Makes sure that FinishTest event never shows up before StartTest and FinishAllTests is always
// last.
pub fn assert_event_ord(events: &Vec<ListenerEvent>) {
    let mut tests = HashMap::new();
    let mut all_finish = false;
    for event in events {
        assert!(!all_finish, "got FinishAllTests event twice: {:#?}", events);
        match event {
            ListenerEvent::StartTest(name) => {
                assert!(
                    !tests.contains_key(&name),
                    "Multiple StartTest for test {}: {:#?}",
                    name,
                    events
                );
                tests.insert(name, false);
            }
            ListenerEvent::FinishTest(name, _) => {
                assert!(
                    tests.contains_key(&name),
                    "Got finish before start event for test {}: {:#?}",
                    name,
                    events
                );
                assert!(
                    !tests.insert(name, true).unwrap(),
                    "Multiple FinishTest for test {}: {:#?}",
                    name,
                    events
                );
            }
            ListenerEvent::FinishAllTests => {
                all_finish = true;
            }
        }
    }
}

impl PartialOrd for ListenerEvent {
    fn partial_cmp(&self, other: &ListenerEvent) -> Option<core::cmp::Ordering> {
        Some(self.cmp(other))
    }
}

impl Eq for ListenerEvent {}

impl ListenerEvent {
    pub fn start_test(name: &str) -> ListenerEvent {
        ListenerEvent::StartTest(name.to_string())
    }
    pub fn finish_test(name: &str, test_result: TestResult) -> ListenerEvent {
        ListenerEvent::FinishTest(name.to_string(), test_result)
    }
    pub fn finish_all_test() -> ListenerEvent {
        ListenerEvent::FinishAllTests
    }
}

impl Clone for ListenerEvent {
    fn clone(&self) -> Self {
        match self {
            ListenerEvent::StartTest(name) => ListenerEvent::start_test(name),
            ListenerEvent::FinishTest(name, test_result) => {
                ListenerEvent::finish_test(name, TestResult { status: test_result.status.clone() })
            }
            ListenerEvent::FinishAllTests => ListenerEvent::finish_all_test(),
        }
    }
}

/// Collects all the listener event as they come and return in a vector.
pub async fn collect_listener_event(
    mut listener: RunListenerRequestStream,
) -> Result<Vec<ListenerEvent>, Error> {
    let mut ret = vec![];
    // collect loggers so that they do not die.
    let mut loggers = vec![];
    while let Some(result_event) = listener.try_next().await? {
        match result_event {
            OnTestCaseStarted { invocation, primary_log, listener, .. } => {
                let name = invocation.name.unwrap();
                ret.push(ListenerEvent::StartTest(name.clone()));
                loggers.push(primary_log);
                let mut listener = listener.into_stream()?;
                while let Some(result) = listener.try_next().await? {
                    match result {
                        Finished { result, .. } => {
                            ret.push(ListenerEvent::FinishTest(name, result));
                            break;
                        }
                    }
                }
            }
            OnFinished { .. } => {
                ret.push(ListenerEvent::FinishAllTests);
                break;
            }
        }
    }
    Ok(ret)
}

/// Helper method to convert names to `Invocation`.
pub fn names_to_invocation(names: Vec<&str>) -> Vec<Invocation> {
    names.iter().map(|s| Invocation { name: Some(s.to_string()), tag: None }).collect()
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_test::Status;

    #[test]
    fn test_ordering_by_enum() {
        let expected_events = vec![
            ListenerEvent::start_test("a"),
            ListenerEvent::finish_test("a", TestResult { status: Some(Status::Passed) }),
            ListenerEvent::finish_all_test(),
        ];

        let mut events = expected_events.clone();
        events.reverse();

        assert_ne!(events, expected_events);
        events.sort();
        assert_eq!(events, expected_events);
    }

    #[test]
    fn test_ordering_by_test_name() {
        let mut events = vec![
            ListenerEvent::start_test("b"),
            ListenerEvent::start_test("a"),
            ListenerEvent::finish_test("a", TestResult { status: Some(Status::Passed) }),
            ListenerEvent::start_test("c"),
            ListenerEvent::finish_test("b", TestResult { status: Some(Status::Passed) }),
            ListenerEvent::finish_test("c", TestResult { status: Some(Status::Passed) }),
            ListenerEvent::finish_all_test(),
        ];

        let expected_events = vec![
            ListenerEvent::start_test("a"),
            ListenerEvent::finish_test("a", TestResult { status: Some(Status::Passed) }),
            ListenerEvent::start_test("b"),
            ListenerEvent::finish_test("b", TestResult { status: Some(Status::Passed) }),
            ListenerEvent::start_test("c"),
            ListenerEvent::finish_test("c", TestResult { status: Some(Status::Passed) }),
            ListenerEvent::finish_all_test(),
        ];
        events.sort();
        assert_eq!(events, expected_events);
    }
}
