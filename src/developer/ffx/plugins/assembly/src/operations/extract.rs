// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, Context, Result};
use ffx_assembly_args::ExtractArgs;
use std::process::Command;

pub fn extract(args: ExtractArgs) -> Result<()> {
    let outdir = args.outdir.to_str().ok_or(anyhow!("outdir is not valid UTF-8 path"))?;
    let zbi = args.zbi.to_str().ok_or(anyhow!("zbi is not valid UTF-8 path"))?;

    // TODO(fxbug.dev/76378): replace this path with a separate config.
    let status = Command::new("host_x64/zbi")
        .args(make_zbi_args(zbi, outdir)?)
        .status()
        .context("Failed to run the zbi tool")?;

    if !status.success() {
        anyhow::bail!("zbi exited with status: {}", status);
    }

    Ok(())
}

fn make_zbi_args(zbi_path: &str, outdir_path: &str) -> Result<Vec<String>> {
    let mut args: Vec<String> = Vec::new();
    args.push("--extract".to_string());
    args.push("--output-dir".to_string());
    args.push(format!("{}/bootfs", outdir_path));
    args.push(zbi_path.to_string());
    Ok(args)
}

#[cfg(test)]
mod tests {
    use super::*;
    use serial_test::serial;
    use std::path::PathBuf;

    #[test]
    fn test_creates_zbi_cmd_args() {
        let args = make_zbi_args("i/am/the/zbi", "output/here").expect("could not make zbi args");
        assert_eq!(args, ["--extract", "--output-dir", "output/here/bootfs", "i/am/the/zbi",]);
    }

    // These tests must be ran serially, because otherwise they will affect each
    // other through process spawming. If a test spawns a process while the
    // other test has an open file, then the spawned process will get a copy of
    // the open file descriptor, preventing the other test from executing it.
    #[test]
    #[serial]
    fn test_extract_no_args() {
        let result =
            extract(ExtractArgs { outdir: PathBuf::from("."), zbi: PathBuf::from("invalid_path") });
        assert!(result.is_err());
    }
}
