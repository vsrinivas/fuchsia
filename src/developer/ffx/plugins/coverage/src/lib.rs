// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Result},
    ffx_core::ffx_plugin,
    ffx_coverage_args::CoverageCommand,
    glob::glob,
    std::{
        path::{Path, PathBuf},
        process::{Command, Stdio},
    },
};

#[ffx_plugin("coverage")]
pub async fn coverage(cmd: CoverageCommand) -> Result<()> {
    let clang_bin_dir = cmd.clang_dir.join("bin");
    let llvm_profdata_bin = clang_bin_dir.join("llvm-profdata");
    llvm_profdata_bin
        .exists()
        .then_some(())
        .ok_or(anyhow!("{:?} does not exist", llvm_profdata_bin))?;
    let llvm_cov_bin = clang_bin_dir.join("llvm-cov");
    llvm_cov_bin.exists().then_some(()).ok_or(anyhow!("{:?} does not exist", llvm_cov_bin))?;

    let profraws = glob_profraws(&cmd.test_output_dir)?;
    // TODO(https://fxbug.dev/99951): find a better place to put merged.profdata.
    let merged_profile = cmd.test_output_dir.join("merged.profdata");
    merge_profraws(&llvm_profdata_bin, &profraws, &merged_profile)
        .context("failed to merge profiles")?;
    // TODO(https://fxbug.dev/99951): add symbolizer binary lookup.
    show_coverage(&llvm_cov_bin, &merged_profile, &cmd.bin_file, &cmd.src_files)
        .context("failed to show coverage")
}

/// Merges input `profraws` using llvm-profdata and writes output to `output_path`.
fn merge_profraws(
    llvm_profdata_bin: &Path,
    profraws: &[PathBuf],
    output_path: &Path,
) -> Result<()> {
    let merge_cmd = Command::new(llvm_profdata_bin)
        .args(["merge", "--sparse", "--output"])
        .arg(output_path)
        .args(profraws)
        .output()
        .context("failed to merge raw profiles")?;
    match merge_cmd.status.code() {
        Some(0) => Ok(()),
        Some(code) => Err(anyhow!(
            "failed to merge raw profiles: status code {}, stderr: {}",
            code,
            String::from_utf8_lossy(&merge_cmd.stderr)
        )),
        None => Err(anyhow!("profile merging terminated by signal unexpectedly")),
    }
}

/// Calls `llvm-cov show` to display coverage from `merged_profile` for `bin_files`.
/// `src_files` can be used to filter coverage for selected source files.
fn show_coverage(
    llvm_cov_bin: &Path,
    merged_profile: &Path,
    bin_files: &[PathBuf],
    src_files: &[PathBuf],
) -> Result<()> {
    let show_cmd = Command::new(llvm_cov_bin)
        .args(["show", "-instr-profile"])
        .arg(merged_profile)
        .args(bin_files)
        .args(src_files)
        .stdout(Stdio::inherit())
        .stderr(Stdio::inherit())
        .output()
        .context("failed to show coverage")?;
    match show_cmd.status.code() {
        Some(0) => Ok(()),
        Some(code) => Err(anyhow!(
            "failed to show coverage: status code {}, stderr: {}",
            code,
            String::from_utf8_lossy(&show_cmd.stderr)
        )),
        None => Err(anyhow!("coverage display terminated by signal unexpectedly")),
    }
}

/// Finds all raw coverage profiles in `test_out_dir`.
fn glob_profraws(test_out_dir: &Path) -> Result<Vec<PathBuf>> {
    let pattern = test_out_dir.join("**").join("*.profraw");
    Ok(glob(pattern.to_str().unwrap())?.filter_map(Result::ok).collect::<Vec<PathBuf>>())
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        assert_matches::assert_matches,
        std::{
            fs::{create_dir, set_permissions, File, Permissions},
            io::Write,
            os::unix::fs::PermissionsExt,
        },
        tempfile::TempDir,
    };

    #[test]
    fn test_glob_profraws() {
        let test_dir = TempDir::new().unwrap();
        create_dir(test_dir.path().join("nested")).unwrap();

        File::create(test_dir.path().join("foo.profraw")).unwrap();
        File::create(test_dir.path().join("nested").join("bar.profraw")).unwrap();
        File::create(test_dir.path().join("foo.not_profraw")).unwrap();
        File::create(test_dir.path().join("nested").join("baz.not_profraw")).unwrap();

        assert_eq!(
            glob_profraws(&test_dir.path().to_path_buf()).unwrap(),
            vec![
                PathBuf::from(test_dir.path().join("foo.profraw")),
                PathBuf::from(test_dir.path().join("nested").join("bar.profraw")),
            ],
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_coverage() {
        ffx_config::init(&[], None, None).unwrap();

        let test_dir = TempDir::new().unwrap();
        let test_dir_path = test_dir.path().to_path_buf();
        let test_bin_dir = test_dir_path.join("bin");
        create_dir(&test_bin_dir).unwrap();

        // Missing both llvm-profdata and llvm-cov.
        assert!(coverage(CoverageCommand {
            test_output_dir: PathBuf::from(&test_dir_path),
            clang_dir: PathBuf::from(&test_dir_path),
            src_files: Vec::new(),
            bin_file: Vec::new(),
        })
        .await
        .is_err());

        // Create empty test binaries for the coverage function to call.
        File::create(test_bin_dir.join("llvm-profdata")).unwrap().write_all(b"#!/bin/sh").unwrap();
        set_permissions(test_bin_dir.join("llvm-profdata"), Permissions::from_mode(0o770)).unwrap();

        // Still missing llvm-cov.
        assert!(coverage(CoverageCommand {
            test_output_dir: PathBuf::from(&test_dir_path),
            clang_dir: PathBuf::from(&test_dir_path),
            src_files: Vec::new(),
            bin_file: Vec::new(),
        })
        .await
        .is_err());

        File::create(test_bin_dir.join("llvm-cov")).unwrap().write_all(b"#!/bin/sh").unwrap();
        set_permissions(test_bin_dir.join("llvm-cov"), Permissions::from_mode(0o770)).unwrap();

        assert_matches!(
            coverage(CoverageCommand {
                test_output_dir: PathBuf::from(&test_dir_path),
                clang_dir: PathBuf::from(&test_dir_path),
                src_files: Vec::new(),
                bin_file: Vec::new(),
            })
            .await,
            Ok(())
        );
    }
}
