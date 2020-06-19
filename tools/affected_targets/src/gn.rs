// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    serde::{Deserialize, Serialize},
    serde_json,
    std::{
        io::Write,
        process::{Command, Stdio},
    },
};

/// `Gn` is a wrapper around a GN binary and a given build directory.
pub trait Gn {
    /// Calls `gn analyze` with the provided `GnAnalyzeInput` and returns
    /// the result.
    fn analyze(&self, input: GnAnalyzeInput) -> Result<GnAnalyzeOutput, Error>;
}

pub struct DefaultGn {
    /// The path of the GN binary.
    binary_path: String,

    /// The path to the build directory to use for `gn` commands.
    build_directory: String,
}

impl DefaultGn {
    pub fn new(build_directory: &str, binary_path: &str) -> Self {
        DefaultGn {
            binary_path: binary_path.to_string(),
            build_directory: build_directory.to_string(),
        }
    }
}

impl Gn for DefaultGn {
    fn analyze(&self, input: GnAnalyzeInput) -> Result<GnAnalyzeOutput, Error> {
        println!("Analyzing...");
        println!("Build Directory: {:?}", self.build_directory);
        println!("GN Path: {:?}", self.binary_path);
        println!("Changed Files: {:?}", input.files);

        let serialized_input = serde_json::to_string(&input)?;

        let mut gn_child = Command::new(&self.binary_path)
            .arg("analyze") // The gn command.
            .arg(&self.build_directory) // The path to the build directory to analyze.
            .arg("-") // Signal that the input comes from stdin.
            .arg("-") // Signal that the output should be written to stdout.
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .spawn()?;

        let gn_std_in = gn_child.stdin.as_mut().unwrap();
        gn_std_in.write_all(serialized_input.as_bytes())?;

        let gn_output = gn_child.wait_with_output()?;
        serde_json::from_slice(&gn_output.stdout)
            .map_err(|err| Error::new(err).context("Failed to parse GN output"))
    }
}

/// GnAnalyzeInput represents the parameters for `gn analyze`.
#[derive(Serialize, Deserialize, Debug)]
pub struct GnAnalyzeInput {
    /// The files to use when determining affected targets.
    /// See `gn help analyze` for more information.
    files: Vec<String>,

    /// A list of labels for targets that are needed to run the
    /// desired tests.
    /// See `gn help analyze` for more information.
    test_targets: Vec<String>,

    /// A list of labels for targets that should be rebuilt.
    /// See `gn help analyze` for more information.
    additional_compile_targets: Vec<String>,
}

impl GnAnalyzeInput {
    /// Returns an analyze input object where all GN targets will
    /// be analyzed.
    pub fn all_targets(files: Vec<String>) -> Self {
        GnAnalyzeInput {
            files,
            test_targets: vec![],
            additional_compile_targets: vec!["all".to_string()],
        }
    }
}

#[derive(Serialize, Deserialize, Debug)]
pub enum GnAnalyzeStatus {
    #[serde(rename = "Found dependency")]
    FoundDependency,

    #[serde(rename = "No dependency")]
    NoDependency,

    #[serde(rename = "Found dependency (all)")]
    UnknownDependency,
}

/// GnAnalyzeInput represents the output of a call to `gn analyze`.
#[derive(Serialize, Deserialize, Debug)]
pub struct GnAnalyzeOutput {
    /// A list of targets that are impacted by the input files.
    /// See `gn help analyze` for more information.
    pub compile_targets: Option<Vec<String>>,

    /// A list of labels for test targets that are impacted by the input
    /// files.
    /// See `gn help analyze` for more information.
    pub test_targets: Option<Vec<String>>,

    /// A list of names from the input that do not exist in the build graph.
    /// See `gn help analyze` for more information.
    pub invalid_targets: Option<Vec<String>>,

    /// The status of the analyze call.
    pub status: Option<GnAnalyzeStatus>,

    /// The error, if present, associated with the analyze call.
    pub error: Option<String>,
}
