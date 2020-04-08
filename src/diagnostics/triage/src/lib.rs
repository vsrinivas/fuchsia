// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod act; // Perform appropriate actions.
pub mod config; // Read the config file(s) for metric and action specs.
pub mod metrics; // Retrieve and calculate the metrics.
pub mod result_format; // Formats the triage results.
pub mod validate; // Check config - including that metrics/triggers work correctly.

use {crate::config::OutputFormat, structopt::StructOpt};

#[derive(StructOpt, Debug)]
pub struct Options {
    /// Config files
    // TODO(cphoenix): #[argh(option, long = "config")]
    #[structopt(long = "config")]
    config_files: Vec<String>,

    /// inspect.json file
    // TODO(cphoenix): #[argh(option, long = "inspect")]
    #[structopt(long)]
    inspect: Option<String>,

    /// How to print the results.
    #[structopt(long = "output_format", default_value = "text")]
    output_format: OutputFormat,

    /// Directories to read "inspect.json" files from.
    #[structopt(long = "directory")]
    directories: Vec<String>,

    /// Which tags we should include
    #[structopt(long = "tag")]
    tags: Vec<String>,

    /// Which tags we should exclude
    #[structopt(long = "exclude-tag")]
    exclude_tags: Vec<String>,
}
