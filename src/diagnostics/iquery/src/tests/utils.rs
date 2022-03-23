// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use argh::FromArgs;
use difference::{Changeset, Difference};
use fuchsia_async as fasync;
use fuchsia_component_test::{Capability, ChildOptions, RealmBuilder, RealmInstance, Ref, Route};
use fuchsia_zircon::{self as zx, DurationNum};
use iquery::{command_line::CommandLine, commands::*, types::Error};
use regex::Regex;
use serde::ser::Serialize;
use serde_json::ser::{PrettyFormatter, Serializer};
use std::{fmt, fs, path::Path};

pub const BASIC_COMPONENT_URL: &'static str =
    "fuchsia-pkg://fuchsia.com/iquery-tests#meta/basic_component.cm";
pub const BASIC_COMPONENT_WITH_LOGS_URL: &'static str =
    "fuchsia-pkg://fuchsia.com/iquery-tests#meta/basic_component_with_logs.cm";
pub const TEST_COMPONENT_URL: &'static str =
    "fuchsia-pkg://fuchsia.com/iquery-tests#meta/test_component.cm";

pub struct TestBuilder {
    builder: RealmBuilder,
}

impl TestBuilder {
    pub async fn new() -> Self {
        Self { builder: RealmBuilder::new().await.expect("Created realm builder") }
    }

    pub async fn add_basic_component(mut self, name: &str) -> Self {
        self.add_child(name, BASIC_COMPONENT_URL).await;
        self
    }

    pub async fn add_basic_component_with_logs(mut self, name: &str) -> Self {
        self.add_child(name, BASIC_COMPONENT_WITH_LOGS_URL).await;
        self
    }

    pub async fn add_test_component(mut self, name: &str) -> Self {
        self.add_child(name, TEST_COMPONENT_URL).await;
        self
    }

    pub async fn start(self) -> TestInExecution {
        let instance = self.builder.build().await.expect("create instance");
        TestInExecution { instance }
    }

    async fn add_child(&mut self, name: &str, url: &str) -> &mut Self {
        let child_ref =
            self.builder.add_child(name, url, ChildOptions::new().eager()).await.unwrap();
        self.builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                    .from(Ref::parent())
                    .to(&child_ref),
            )
            .await
            .unwrap();
        self
    }
}

pub enum AssertionOption {
    Retry,
    RemoveArchivist,
}

#[derive(Clone, Copy, Debug)]
pub enum IqueryCommand {
    List,
    ListAccessors,
    ListFiles,
    Logs,
    Selectors,
    ShowFile,
    Show,
}

impl fmt::Display for IqueryCommand {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let s = match self {
            Self::List => "list",
            Self::ListAccessors => "list-accessors",
            Self::ListFiles => "list-files",
            Self::Logs => "logs",
            Self::Selectors => "selectors",
            Self::ShowFile => "show-file",
            Self::Show => "show",
        };
        write!(f, "{}", s)
    }
}

pub struct AssertionParameters<'a> {
    pub command: IqueryCommand,
    pub golden_basename: &'static str,
    pub iquery_args: Vec<&'a str>,
    pub opts: Vec<AssertionOption>,
}

#[derive(Clone, Copy, Debug)]
pub enum Format {
    Json,
    Text,
}

impl fmt::Display for Format {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let s = match self {
            Self::Json => "json",
            Self::Text => "text",
        };
        write!(f, "{}", s)
    }
}

impl Format {
    fn all() -> impl Iterator<Item = Format> {
        vec![Format::Json, Format::Text].into_iter()
    }
}

pub struct TestInExecution {
    instance: RealmInstance,
}

impl TestInExecution {
    pub async fn assert(&self, params: AssertionParameters<'_>) {
        for format in Format::all() {
            let expected = self.read_golden(params.golden_basename, format);
            let mut assertion = CommandAssertion::new(
                params.command,
                &expected,
                format,
                &params.iquery_args,
                self.instance.root.child_name(),
            );
            for opt in &params.opts {
                match opt {
                    AssertionOption::Retry => assertion.with_retries(),
                    AssertionOption::RemoveArchivist => assertion.remove_archivist(),
                }
            }
            assertion.assert().await;
        }
    }

    pub fn instance_child_name(&self) -> &str {
        self.instance.root.child_name()
    }

    fn read_golden(&self, golden_basename: &str, format: Format) -> String {
        let path = format!("/pkg/data/goldens/{}.{}", golden_basename, format);
        fs::read_to_string(Path::new(&path)).expect(&format!("loaded golden {}", path))
    }
}

#[derive(Clone, Debug)]
pub struct CommandAssertion<'a> {
    command: IqueryCommand,
    iquery_args: &'a [&'a str],
    max_retry_time_seconds: i64,
    expected: &'a str,
    format: Format,
    remove_archivist: bool,
    instance_child_name: &'a str,
}

impl<'a> CommandAssertion<'a> {
    pub fn new(
        command: IqueryCommand,
        expected: &'a str,
        format: Format,
        iquery_args: &'a [&'a str],
        instance_child_name: &'a str,
    ) -> Self {
        Self {
            command,
            iquery_args,
            max_retry_time_seconds: 0,
            expected: expected.into(),
            format,
            remove_archivist: false,
            instance_child_name,
        }
    }

    pub fn with_retries(&mut self) {
        self.max_retry_time_seconds = 120;
    }

    pub fn remove_archivist(&mut self) {
        self.remove_archivist = true;
    }

    pub async fn assert(self) {
        let started = zx::Time::get_monotonic().into_nanos();
        let format_str = self.format.to_string();
        let command_str = self.command.to_string();
        let mut command_line = vec!["--format", &format_str, &command_str];
        command_line.append(&mut self.iquery_args.iter().map(|s| s.as_ref()).collect::<Vec<_>>());
        loop {
            match execute_command(&command_line[..]).await {
                Ok(mut result) => {
                    result = self.cleanup_unrelated_components(result);
                    let now = zx::Time::get_monotonic().into_nanos();
                    if now >= started + self.max_retry_time_seconds.seconds().into_nanos() {
                        self.assert_result(&result, &self.expected);
                        break;
                    }
                    if self.result_equals_expected(&result, &self.expected) {
                        break;
                    }
                }
                Err(e) => {
                    let now = zx::Time::get_monotonic().into_nanos();
                    if now >= started + self.max_retry_time_seconds.seconds().into_nanos() {
                        assert!(false, "Error: {:?}", e);
                    }
                }
            }
            fasync::Timer::new(fasync::Time::after(100.millis())).await;
        }
    }

    fn cleanup_unrelated_components(&self, result: String) -> String {
        match self.format {
            Format::Json => {
                // Removes the entry in the vector for the archivist
                let mut result_json: serde_json::Value =
                    serde_json::from_str(&result).expect("expected json");
                self.retain_with_moniker_match_in_json(&mut result_json, |val| {
                    // Also retain paths (for accessor and files lists)
                    val.starts_with("/")
                        || val.starts_with("./")
                        || val.ends_with("archivist")
                        || val == "realm_builder_server"
                        || val.starts_with(&format!("realm_builder\\:{}", self.instance_child_name))
                        || val.starts_with(&format!("realm_builder:{}", self.instance_child_name))
                });
                if self.remove_archivist {
                    self.retain_with_moniker_match_in_json(&mut result_json, |val| {
                        !val.ends_with("archivist")
                    });
                }
                // Use 4 spaces for indentation since `fx format-code` enforces that in the
                // goldens.
                let mut buf = Vec::new();
                let mut ser =
                    Serializer::with_formatter(&mut buf, PrettyFormatter::with_indent(b"    "));
                result_json.serialize(&mut ser).unwrap();
                String::from_utf8(buf).unwrap()
            }
            Format::Text => self.retain_with_moniker_match_in_text(result),
        }
    }

    fn retain_with_moniker_match_in_json<F>(&self, result_json: &mut serde_json::Value, f: F)
    where
        F: Fn(&str) -> bool,
    {
        if let Some(arr) = result_json.as_array_mut() {
            arr.retain(|value| value.as_str().map(&f).unwrap_or(true));
            arr.retain(|value| {
                value.get("moniker").and_then(|val| val.as_str()).map(&f).unwrap_or(true)
            });
        }
    }

    // We want to filter out results from other tests. So we only include the components under our
    // test case root realm.
    fn retain_with_moniker_match_in_text(&self, initial: String) -> String {
        let mut result = String::new();
        let mut include_data = true;
        for line in initial.lines() {
            if line.starts_with(" ") {
                if include_data {
                    result.push_str(line);
                    result.push_str("\n");
                }
                continue;
            }

            if line.starts_with("archivist") {
                if !self.remove_archivist {
                    result.push_str(line);
                    result.push_str("\n");
                }
                include_data = !self.remove_archivist;
                continue;
            }

            if line.starts_with("realm_builder\\:") {
                if !line.starts_with(&format!("realm_builder\\:{}", self.instance_child_name)) {
                    include_data = false;
                    continue;
                } else {
                    include_data = true;
                }
            }

            result.push_str(line);
            result.push_str("\n");
        }
        result.trim().to_string()
    }

    /// Validates that a command result matches the expected json string
    fn assert_result(&self, result: &str, expected: &str) {
        let clean_result = self.cleanup_variable_strings(result);
        let Changeset { diffs, distance, .. } =
            Changeset::new(&clean_result, expected.trim(), "\n");
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
    fn result_equals_expected(&self, result: &str, expected: &str) -> bool {
        let clean_result = self.cleanup_variable_strings(result);
        clean_result.trim() == expected.trim()
    }

    /// Cleans-up instances of:
    /// - RealmBuilder collection child names by CHILD_NAME
    /// - `"start_timestamp_nanos": 7762005786231` by `"start_timestamp_nanos": TIMESTAMP`
    /// - process IDs and thread IDs
    /// - instance IDs in monikers
    /// - timestamps in log strings
    /// - process and thread IDs in log strings
    fn cleanup_variable_strings(&self, string: impl Into<String>) -> String {
        // Replace start_timestamp_nanos in fuchsia.inspect.Health entries and
        // timestamp in metadatas.
        let mut string: String = string.into();

        // Replace the autogenerated realm builder collection child name (TEXT)
        let re = Regex::new(r#"realm_builder\\:auto-[[:xdigit:]]+/"#).unwrap();
        let replacement = "realm_builder\\:CHILD_NAME/";
        string = re.replace_all(&string, replacement).to_string();

        // Replace the autogenerated realm builder collection child name (JSON)
        let re = Regex::new(r#"realm_builder\\\\:auto-[[:xdigit:]]+/"#).unwrap();
        let replacement = "realm_builder\\\\:CHILD_NAME/";
        string = re.replace_all(&string, replacement).to_string();

        // Replace the autogenerated realm builder collection child name (path)
        let re = Regex::new(r#"realm_builder:auto-[[:xdigit:]]+/"#).unwrap();
        let replacement = "realm_builder:CHILD_NAME/";
        string = re.replace_all(&string, replacement).to_string();

        // Moniker in log metadatas requires special treatment to remove instance ids.
        let re = Regex::new(r#""moniker": "(.+:)(\d+)""#).unwrap();
        let replacement = "\"moniker\": \"${1}INSTANCE_ID";
        string = re.replace_all(&string, replacement).to_string();

        // Timestamp, pid, instance id and tid in log text.
        let re = Regex::new(r#"\[\d+\.\d+\]\[\d+\]\[\d+\]\[(.+)\]"#).unwrap();
        string = re.replace_all(&string, "[TIMESTAMP][PID][TID][${1}]").to_string();

        // Make PID and TID constant in JSON outputs.
        for value in &["pid", "tid"] {
            let re = Regex::new(&format!("\"{}\": \\d+", value)).unwrap();
            let replacement = format!("\"{}\": \"{}\"", value, value.to_string().to_uppercase());
            string = re.replace_all(&string, replacement.as_str()).to_string();
        }

        for value in &["timestamp", "start_timestamp_nanos"] {
            let re = Regex::new(&format!("\"{}\": \\d+", value)).unwrap();
            let replacement = format!("\"{}\": \"TIMESTAMP\"", value);
            string = re.replace_all(&string, replacement.as_str()).to_string();

            let re = Regex::new(&format!("{} = \\d+", value)).unwrap();
            let replacement = format!("{} = TIMESTAMP", value);
            string = re.replace_all(&string, replacement.as_str()).to_string();
        }

        string
    }
}

/// Execute a command: [command, flags, and, iquery_args]
pub async fn execute_command(command: &[&str]) -> Result<String, Error> {
    let provider = ArchiveAccessorProvider::default();
    let command_line = CommandLine::from_args(&["iquery"], command).expect("create command line");
    command_line.execute(&provider).await
}
