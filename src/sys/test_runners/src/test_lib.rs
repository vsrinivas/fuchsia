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
    {anyhow::Error, futures::prelude::*},
};

#[derive(PartialEq, Debug)]
pub enum ListenerEvent {
    StartTest(String),
    FinishTest(String, TestResult),
    FinishAllTests,
}

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
