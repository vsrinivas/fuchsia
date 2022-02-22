// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{ensure, Error};
use std::path::PathBuf;
use structopt::StructOpt;

#[derive(StructOpt, Debug)]
pub struct Opt {
    #[structopt(short = "i", long = "input", parse(from_os_str))]
    // Path to the tests.json file.
    pub input: PathBuf,

    #[structopt(short = "o", long = "output", parse(from_os_str))]
    // Path to output test-list.
    pub output: PathBuf,

    #[structopt(short = "b", long = "build-dir", parse(from_os_str))]
    // Path to the build directory.
    pub build_dir: PathBuf,

    #[structopt(short = "d", long = "depfile", parse(from_os_str))]
    // Path to output a depfile.
    pub depfile: Option<PathBuf>,
}

impl Opt {
    pub fn validate(&self) -> Result<(), Error> {
        ensure!(self.input.exists(), "input {:?} does not exist", self.input);
        ensure!(self.build_dir.exists(), "build-dir {:?} does not exist", self.build_dir);
        Ok(())
    }
}
