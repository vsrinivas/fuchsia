// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use rand::random;
use serde::{Deserialize, Serialize};
use serde_json;
use std::env;
use std::fs;
use std::io::BufReader;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};

/// The result of running a benchmark.
#[derive(Serialize, Deserialize, Clone)]
pub struct BenchmarkResult {
    pub label: String,
    pub test_suite: String,
    pub unit: String,
    pub values: Vec<f64>,
    pub split_first: Option<bool>,
}

/// Run a benchmark command.
/// The output filepath is appended as the last argument to the passed-in command.
pub fn run(cmd: &str, args: &[&str]) -> Vec<BenchmarkResult> {
    let tmpfile = tmpfile_path();
    let mut args_mut = args.to_vec();
    args_mut.push(tmpfile.to_str().unwrap());
    println!("Running {} {}", cmd, args_mut.join(" "));
    run_cmd(cmd, &args_mut);
    read_results(&tmpfile)
}

fn run_cmd(cmd: &str, args: &[&str]) {
    assert!(Path::new(cmd).exists(), "binary {} doesn't exist", cmd);
    let mut child = Command::new(cmd)
        .args(args)
        .stdout(Stdio::inherit())
        .stderr(Stdio::inherit())
        .spawn()
        .expect("failed to run command");
    let ecode = child.wait().expect("failed to wait for process to exit");
    assert!(ecode.success());
}

fn tmpfile_path() -> PathBuf {
    let tmpdir = env::temp_dir();
    assert!(
        Path::new(&tmpdir).exists(),
        "temporary directory {} doesn't exist",
        tmpdir.to_str().unwrap()
    );
    let tmpfile = tmpdir.join(format!("result{}", random::<u64>()));
    assert!(
        !Path::new(&tmpfile).exists(),
        "temporary file {} already exists",
        tmpfile.to_str().unwrap()
    );
    tmpfile
}

fn read_results(path: &Path) -> Vec<BenchmarkResult> {
    let file = fs::File::open(path).expect("failed to open results file");
    let reader = BufReader::new(file);
    let results: Vec<BenchmarkResult> =
        serde_json::from_reader(reader).expect("failed to parse JSON to BenchmarkResult");
    results
}
