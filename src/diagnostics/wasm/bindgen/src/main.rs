// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Result};
use std::process::Command;
use structopt::StructOpt;

#[derive(StructOpt)]
struct Opts {
    input_path: String,
    output_path: String,
}

const MISSING_MESSAGE: &'static str = "Failed to execute wasm-bindgen.
Ensure you have 'wasm-bindgen' installed locally: cargo install wasm-bindgen-cli
If you are seeing this error in CQ you have made a mistake with your dependencies, ensure 'rust_wasmify' is not included in your build.";

// Simply apply wasm-bindgen functionality to the input path save it to the output path.
fn main() -> Result<()> {
    let opts = Opts::from_args();

    let result = Command::new("wasm-bindgen")
        .arg("--target=web")
        .arg("--typescript")
        .arg("--out-dir")
        .arg(&opts.output_path)
        .arg(&opts.input_path)
        .status();

    match result {
        Err(e) => {
            println!("{}", MISSING_MESSAGE);

            Err(e.into())
        }
        Ok(s) => {
            if s.success() {
                Ok(())
            } else {
                Err(format_err!("Exit status was {}", s.code().unwrap_or_default()))
            }
        }
    }
}
