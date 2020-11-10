// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use {
    crate::{command_line::CommandLine, commands::*, types::Error},
    anyhow::format_err,
    argh::FromArgs,
    difference::{Changeset, Difference},
    fidl_fuchsia_sys::ComponentControllerEvent,
    fuchsia_async as fasync,
    fuchsia_component::{
        client::{self, App},
        server::{NestedEnvironment, ServiceFs},
    },
    futures::StreamExt,
    regex::Regex,
};

pub const BASIC_COMPONENT_URL: &'static str =
    "fuchsia-pkg://fuchsia.com/iquery-tests#meta/basic_component.cmx";
pub const TEST_COMPONENT_URL: &'static str =
    "fuchsia-pkg://fuchsia.com/iquery-tests#meta/test_component.cmx";

/// Creates a new environment named `env_label` and a starts the basic component under it.
pub async fn start_basic_component(
    env_label: &str,
) -> Result<(NestedEnvironment, App), anyhow::Error> {
    let (env, app) = launch(env_label, BASIC_COMPONENT_URL, None)?;
    wait_for_out_ready(&app).await?;
    Ok((env, app))
}

/// Creates a new environment named `env_label` and a starts the test component under it.
pub async fn start_test_component(
    env_label: &str,
) -> Result<(NestedEnvironment, App), anyhow::Error> {
    let args =
        vec!["--columns".to_string(), "3".to_string(), "--rows".to_string(), "3".to_string()];
    let (env, app) = launch(env_label, TEST_COMPONENT_URL, Some(args))?;
    wait_for_out_ready(&app).await?;
    Ok((env, app))
}

/// Execute a command: [command, flags, and, args]
pub async fn execute_command(command: &[&str]) -> Result<String, Error> {
    let command_line = CommandLine::from_args(&["iquery"], command).expect("create command line");
    command_line.execute().await
}

/// Validates that a command result matches the expected json string
pub fn assert_result(result: &str, expected: &str) {
    let clean_result = cleanup_variable_strings(result);
    let Changeset { diffs, distance, .. } = Changeset::new(&clean_result, expected.trim(), "\n");
    if distance == 0 {
        return;
    }
    for diff in &diffs {
        match diff {
            Difference::Same(ref x) => {
                eprintln!(" {}", x);
            }
            Difference::Add(ref x) => {
                eprintln!("+{}", x);
            }
            Difference::Rem(ref x) => {
                eprintln!("-{}", x);
            }
        }
    }
    assert_eq!(distance, 0);
}

/// Checks that the result string (cleaned) and the expected string are equal
pub fn result_equals_expected(result: &str, expected: &str) -> bool {
    let clean_result = cleanup_variable_strings(result);
    clean_result.trim() == expected.trim()
}

pub async fn wait_for_terminated(mut app: App) {
    app.kill().expect("app killed correctly");
    app.wait().await.expect("app exited correctly");
}

fn launch(
    env_label: &str,
    url: impl Into<String>,
    args: Option<Vec<String>>,
) -> Result<(NestedEnvironment, App), anyhow::Error> {
    let mut service_fs = ServiceFs::new();
    let env = service_fs.create_nested_environment(env_label)?;
    let app = client::launch(&env.launcher(), url.into(), args)?;
    fasync::Task::spawn(service_fs.collect()).detach();
    Ok((env, app))
}

async fn wait_for_out_ready(app: &App) -> Result<(), anyhow::Error> {
    let mut component_stream = app.controller().take_event_stream();
    match component_stream
        .next()
        .await
        .expect("component event stream ended before termination event")?
    {
        ComponentControllerEvent::OnTerminated { return_code, termination_reason } => {
            Err(format_err!(
                "Component terminated unexpectedly. Code: {}. Reason: {:?}",
                return_code,
                termination_reason
            ))
        }
        ComponentControllerEvent::OnDirectoryReady {} => Ok(()),
    }
}

/// Cleans-up instances of:
/// - `"start_timestamp_nanos": 7762005786231` by `"start_timestamp_nanos": TIMESTAMP`
/// - instance ids by INSTANCE_ID
fn cleanup_variable_strings(string: impl Into<String>) -> String {
    // Replace start_timestamp_nanos in fuchsia.inspect.Health entries and
    // timestamp in metadatas.
    let mut string: String = string.into();
    for value in &["timestamp", "start_timestamp_nanos"] {
        let re = Regex::new(&format!("\"{}\": \\d+", value)).unwrap();
        let replacement = format!("\"{}\": \"TIMESTAMP\"", value);
        string = re.replace_all(&string, replacement.as_str()).to_string();

        let re = Regex::new(&format!("{} = \\d+", value)).unwrap();
        let replacement = format!("{} = TIMESTAMP", value);
        string = re.replace_all(&string, replacement.as_str()).to_string();
    }

    // Replace instance IDs in paths.
    let re = Regex::new("/\\d+/").unwrap();
    re.replace_all(&string, "/INSTANCE_ID/").trim().to_string()
}
