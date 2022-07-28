// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This is an integration test for the `gn_json` crate, ensuring that it is
//! able to parse the `gn desc` output from the in-tree GN implementation.
//!
//! The general shape of the test is:
//!
//! 1. Execute GN on a minimal fake build setup, generating json output, so that
//!    we have a controlled set of inputs to work with, generating
//!
//! 2. Read in the json produced.
//!
//! 3. Run the various tests.
//!

use anyhow::{bail, Context, Result};
use argh::FromArgs;
use camino::Utf8PathBuf;
use gn_json::target::{AllTargets, ConfigValues, Public, TargetDescription};
use pretty_assertions::assert_eq;
use serde_json;
use std::{ffi::OsStr, process::Command};

/// Test arguments for the integration test
#[derive(Debug, FromArgs)]
struct TestArgs {
    /// the path to the dir which contains the GN binary.
    #[argh(option)]
    gn_tool_dir: Utf8PathBuf,

    /// the path to the gn project.
    #[argh(option)]
    project_dir: Utf8PathBuf,
}

fn main() -> Result<()> {
    let args: TestArgs = argh::from_env();

    let gn = GN {
        tool_path: args.gn_tool_dir.join("gn"),
        project_path: args.project_dir,
        outdir: "//outdir".into(),
    };

    // Run GN on the test project.
    gn.gen().context("Running GN 'gen' on the test project")?;
    let raw_desc_json = gn.desc().context("Running GN 'desc' on the test project")?;

    let all_targets: AllTargets = serde_json::from_slice(raw_desc_json.as_bytes())
        .context("Parsing the GN desc json output")?;

    assert_eq!(
        vec!["//:default", "//:tests", "//foo/bar:bar", "//foo:foo_action", "//foo:foo_binary",],
        itertools::sorted(all_targets.keys().into_iter()).collect::<Vec<&String>>(),
        "The expected targets were not found in the parsed json output"
    );

    assert_eq!(
        &TargetDescription {
            config_values: ConfigValues {
                cflags: vec!["-Os".to_string()],
                ldflags: vec!["-some_ld_flag".to_string()],
                ..ConfigValues::default()
            },
            config_targets: vec![
                "//build:compiler_defaults".to_string(),
                "//build:executable_ldconfig".to_string(),
            ],
            crate_name: Some("foo_binary".to_string()),
            crate_root: Some("//foo/src/main.rs".to_string()),
            deps: vec!["//foo/bar:bar".to_string()],
            outputs: vec!["//outdir/foo_binary".to_string()],
            sources: vec!["//foo/src/main.rs".to_string()],
            toolchain: "//build/toolchain:main".to_string(),
            target_type: "executable".to_string(),
            metadata: [
                ("simple_key".to_string(), serde_json::json!(["simple_value"])),
                (
                    "complex_key".to_string(),
                    serde_json::json!([{
                        "arg": "arg_value",
                        "arg2": "arg2_value"
                    }]),
                ),
            ]
            .into(),
            ..TargetDescription::default()
        },
        all_targets.get("//foo:foo_binary").unwrap(),
    );

    assert_eq!(
        &TargetDescription {
            args: vec!["--arg".to_string(), "value".to_string()],
            deps: vec!["//foo:foo_binary".to_string()],
            public: Public::StringVal("*".to_string()),
            toolchain: "//build/toolchain:main".to_string(),
            target_type: "action".to_string(),
            script: Some("//foo/some_script.py".to_string()),
            inputs: vec!["//foo/src/main.rs".to_string()],
            outputs: vec!["//outdir/obj/foo/action_output.txt".to_string()],
            ..TargetDescription::default()
        },
        all_targets.get("//foo:foo_action").unwrap(),
    );

    assert_eq!(
        &TargetDescription {
            deps: vec!["//:tests".to_string(), "//foo:foo_binary".to_string()],
            toolchain: "//build/toolchain:main".to_string(),
            target_type: "group".to_string(),
            ..TargetDescription::default()
        },
        all_targets.get("//:default").unwrap(),
    );

    assert_eq!(
        &TargetDescription {
            toolchain: "//build/toolchain:main".to_string(),
            target_type: "group".to_string(),
            ..TargetDescription::default()
        },
        all_targets.get("//:tests").unwrap(),
    );

    assert_eq!(
        &TargetDescription {
            crate_name: Some("bar".to_string()),
            crate_root: Some("//foo/bar/src/lib.rs".to_string()),
            outputs: vec!["//outdir/obj/foo/bar/libbar.rlib".to_string()],
            sources: vec!["//foo/bar/src/lib.rs".to_string()],
            toolchain: "//build/toolchain:main".to_string(),
            target_type: "rust_library".to_string(),
            ..TargetDescription::default()
        },
        all_targets.get("//foo/bar:bar").unwrap(),
    );

    Ok(())
}

#[derive(Debug)]
struct GN {
    tool_path: Utf8PathBuf,
    project_path: Utf8PathBuf,
    outdir: Utf8PathBuf,
}

impl GN {
    fn gen(&self) -> Result<String> {
        self.run_cmd("gen", vec![self.outdir.as_str()])
    }

    /// Run `gn desc`
    fn desc(&self) -> Result<String> {
        self.run_cmd("desc", vec![self.outdir.as_str(), "*", "--format=json"])
    }

    /// Run GN with the given cmd and args.
    ///
    /// The Result is:
    ///   Ok(stdout)
    ///   Err(stdout if non-zero length, otherwise stderr)
    fn run_cmd<I: IntoIterator<Item = S>, S: AsRef<OsStr>>(
        &self,
        cmd: &str,
        args: I,
    ) -> Result<String> {
        let mut command = Command::new(&self.tool_path);
        command.arg(cmd);
        command.arg(format!("--root={}", &self.project_path));

        for arg in args.into_iter() {
            command.arg(arg);
        }

        let result = command.output();

        let output = result?;
        let stdout = String::from_utf8(output.stdout.clone())
            .context("Converting cmd stdout to a string")?;
        if !output.status.success() {
            if stdout.len() != 0 {
                bail!("GN failed (stdout):\n{}", stdout);
            } else {
                let stderr = String::from_utf8_lossy(&output.stderr);
                bail!("GN failed (stderr): \n{}", stderr);
            }
        }
        Ok(stdout)
    }
}
