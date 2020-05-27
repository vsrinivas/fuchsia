// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use {
    anyhow::{format_err, Error},
    fidl_fuchsia_sys::ComponentControllerEvent,
    fuchsia_async as fasync,
    fuchsia_component::{
        client::{self, App},
        server::{NestedEnvironment, ServiceFs},
    },
    futures::StreamExt,
    pretty_assertions::assert_eq,
    regex::Regex,
    serde::Serialize,
};

const TEST_COMPONENT_URL: &'static str =
    "fuchsia-pkg://fuchsia.com/iquery_tests#meta/basic_component.cmx";

/// Creates a new environment named `env_label` and a starts the test component under it.
pub async fn start_basic_component(env_label: &str) -> Result<(NestedEnvironment, App), Error> {
    let mut service_fs = ServiceFs::new();
    let env = service_fs.create_nested_environment(env_label)?;
    let app = client::launch(&env.launcher(), TEST_COMPONENT_URL.to_string(), None)?;
    fasync::spawn(service_fs.collect());

    let mut component_stream = app.controller().take_event_stream();
    match component_stream
        .next()
        .await
        .expect("component event stream ended before termination event")?
    {
        ComponentControllerEvent::OnTerminated { return_code, termination_reason } => {
            return Err(format_err!(
                "Component terminated unexpectedly. Code: {}. Reason: {:?}",
                return_code,
                termination_reason
            ));
        }
        ComponentControllerEvent::OnDirectoryReady {} => {}
    }
    Ok((env, app))
}

/// Validates that a command result matches the expected json string
pub fn assert_result<T: Serialize>(result: T, expected: &str) {
    let result = serde_json::to_string_pretty(&result).expect("result is json");
    let result: serde_json::Value =
        serde_json::from_str(&cleanup_timestamps(result)).expect("cleaned result is json");
    let expected: serde_json::Value = serde_json::from_str(expected).expect("expected is json");
    assert_eq!(result, expected);
}

/// Cleans-up instances of `"start_timestamp_nanos": 7762005786231` by
/// `"start_timestamp_nanos": TIMESTAMP`
fn cleanup_timestamps(string: String) -> String {
    let re = Regex::new("\"start_timestamp_nanos\": \\d+").unwrap();
    re.replace_all(&string, "\"start_timestamp_nanos\": \"TIMESTAMP\"").to_string()
}
