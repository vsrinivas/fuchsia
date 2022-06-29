// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Context as _;
use diagnostics_data::Severity;
use std::path::PathBuf;

mod test_spec;
use test_spec::{LoggingSpec, TestKind, TestSpec};

/// check that a //src/lib/fuchsia user on host OSes correctly logs
#[derive(Debug, argh::FromArgs)]
struct Options {
    /// path to JSON test spec
    #[argh(option)]
    test_spec: PathBuf,

    /// path to binary to run
    #[argh(option)]
    binary: PathBuf,
}

#[fuchsia::main]
fn main() {
    let Options { test_spec, binary } = argh::from_env();

    let raw_test_spec = std::fs::read_to_string(&test_spec)
        .with_context(|| format!("reading {}", test_spec.display()))
        .unwrap();
    let test_spec: TestSpec = serde_json::from_str(&raw_test_spec).context("parsing spec").unwrap();

    let mut cmd = std::process::Command::new(&binary);

    // we only need to let it send stderr, all of the libtest output goes to stdout
    if let TestKind::Test = test_spec.kind {
        cmd.arg("--nocapture");
    }

    let output = cmd.output().unwrap();
    if !output.status.success() {
        panic!("failed to run {}: {:#?}", binary.display(), output);
    }

    let stderr = String::from_utf8(output.stderr).expect("need utf8 from the wrapped binary");
    let mut logs = stderr.trim().lines();
    if let Some(LoggingSpec { min_severity, .. }) = &test_spec.logging {
        let min_severity = min_severity.unwrap_or(Severity::Info);

        // start reading logs from the user's code
        if min_severity <= Severity::Trace {
            assert_next_hello_world_expected(&mut logs, Severity::Trace);
        }
        if min_severity <= Severity::Debug {
            assert_next_hello_world_expected(&mut logs, Severity::Debug);
        }
        if min_severity <= Severity::Info {
            assert_next_hello_world_expected(&mut logs, Severity::Info);
        }
        if min_severity <= Severity::Warn {
            assert_next_hello_world_expected(&mut logs, Severity::Warn);
        }
        if min_severity <= Severity::Error {
            assert_next_hello_world_expected(&mut logs, Severity::Error);
        }

        // panics produce extra log messages when they terminate the process
        if !test_spec.panics {
            assert_eq!(logs.next(), None, "no more logs should be available");
        }
    }
    assert_eq!(logs.next(), None);
}

/// parse this format:
///
/// ```
/// datetime severity module: message
/// ```
///
/// and assert on the expected contents
#[track_caller]
fn assert_next_hello_world_expected<'a>(
    mut lines: impl Iterator<Item = &'a str>,
    expected_severity: Severity,
) {
    let line = lines.next().unwrap();

    let mut segments = line.split(' ');
    let _datetime = segments.next().unwrap();
    let mut severity = segments.next().unwrap().trim();
    if severity.is_empty() {
        // tracing_subscriber writes an extra space before four-letter severities to align them
        severity = segments.next().unwrap().trim();
    }
    let _target = segments.next().unwrap();
    let message = segments.collect::<Vec<_>>().join(" ");

    assert_eq!(message, "Hello, World!");
    assert_eq!(severity, expected_severity.to_string());
}
