// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{bail, Context, Result};
use argh::FromArgs;
use prost_build;
use std::collections::HashSet;
use std::fs::File;
use std::os::unix::io::AsRawFd;
use std::path::PathBuf;

#[derive(FromArgs, Debug)]
/// compiles .proto files into Rust
struct Options {
    #[argh(option)]
    /// path to the protoc binary
    protoc: PathBuf,
    #[argh(option)]
    /// directory in which to generate rust code
    out_dir: PathBuf,
    #[argh(option)]
    /// directories to search for imports
    include_dirs: Vec<PathBuf>,
    #[argh(option)]
    /// protobuf files to compile
    protos: Vec<PathBuf>,
}

fn main() -> Result<()> {
    let options: Options = argh::from_env();
    if options.protos.is_empty() {
        bail!("Must provide at least one proto to compile")
    }
    if !options.out_dir.is_dir() {
        bail!("out_dir must be a directory")
    }
    if !options.include_dirs.iter().all(|p| p.is_dir()) {
        bail!("all includes should be directories")
    }
    if !options.protos.iter().all(|p| p.is_file()) {
        bail!("all protos should be files")
    }

    // The protoc location is read by the library from an environment variable
    std::env::set_var("PROTOC", options.protoc);

    // Add the parent directories of the provided protos to the list of include dirs.
    // Config::compile_protos requires that the .proto files passed in must be found in one of the
    // include directories.
    let mut include_set: HashSet<PathBuf> = HashSet::from_iter(options.include_dirs.into_iter());
    include_set.extend(options.protos.iter().map(|p| p.parent().unwrap().to_owned()));

    // If proto is a relative path without a CurDir component, parent() may yield ""
    // Replace such instances with CurDir
    if let Some(mut dir) = include_set.take(&PathBuf::new()) {
        dir.push(".");
        include_set.insert(dir);
    }

    let includes: Vec<PathBuf> = include_set.into_iter().collect();

    // The prost_build library prints to stdout which is undesirable.
    // Replace the stdout fd with /dev/null so as to be less disruptive.
    let dev_null = File::open("/dev/null")?;
    unsafe {
        libc::dup2(dev_null.as_raw_fd(), std::io::stdout().as_raw_fd());
    }

    let mut config = prost_build::Config::new();
    config.out_dir(&options.out_dir);
    config.include_file("lib.rs");
    config.compile_protos(&options.protos, &includes).context("Failed to compile protos")
}
