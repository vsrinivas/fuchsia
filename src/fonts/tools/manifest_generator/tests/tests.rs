// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use std::fs::File;
use {
    failure::{ensure, format_err, Error},
    pretty_assertions::assert_eq,
    std::{env, path::PathBuf, process::Command},
    tempfile::NamedTempFile,
};

fn test_path() -> PathBuf {
    let mut path = env::current_exe().unwrap();
    path.pop();
    if !path.join("test_data").exists() {
        // We're inside exe.unstripped.
        path.pop();
    }
    path
}

fn resolve_path(suffix: impl AsRef<str>) -> Result<String, Error> {
    let mut path = test_path().join("exe.unstripped").join(suffix.as_ref());
    if !path.exists() {
        path = test_path().join(suffix.as_ref());
    }
    ensure!(path.exists(), "Not found: {:?}", &path);
    Ok(path.to_str().unwrap().to_string())
}

fn run_golden_test(
    font_catalog_paths: Vec<String>,
    font_pkgs_paths: Vec<String>,
    all_fonts_path: String,
    local_fonts_path: String,
    font_dir: String,
    fake_code_points: bool,
    golden_manifest_path: String,
) -> Result<(), Error> {
    let mut cmd = Command::new(resolve_path("font_manifest_generator")?);

    cmd.arg("--font-catalog");
    for path in font_catalog_paths {
        let path = resolve_path(path)?;
        cmd.arg(path);
    }

    cmd.arg("--font-pkgs");
    for path in font_pkgs_paths {
        let path = resolve_path(path)?;
        cmd.arg(path);
    }

    cmd.arg("--all-fonts").arg(resolve_path(all_fonts_path)?);
    cmd.arg("--local-fonts").arg(resolve_path(local_fonts_path)?);
    cmd.arg("--font-dir").arg(resolve_path(font_dir)?);

    if fake_code_points {
        cmd.arg("--fake-code-points");
    }

    let manifest_output_file = NamedTempFile::new()?;
    cmd.arg("--output")
        .arg(manifest_output_file.path().to_str().ok_or_else(|| format_err!("Bad temp path"))?);

    let output = cmd.output().expect("Failed to execute");
    assert!(
        output.status.success(),
        "\nSTDOUT:\n===\n{}===\nSTDERR:\n===\n{}\n===",
        std::str::from_utf8(&output.stdout)?,
        std::str::from_utf8(&output.stderr)?
    );

    let golden_manifest_path = resolve_path(golden_manifest_path)?;
    let golden_json: serde_json::Value =
        serde_json::from_reader(File::open(&golden_manifest_path)?)?;

    let actual_json: serde_json::Value = serde_json::from_reader(manifest_output_file.reopen()?)?;

    assert_eq!(actual_json, golden_json);

    Ok(())
}

#[test]
fn test_manifest_generator_goldens() -> Result<(), Error> {
    run_golden_test(
        vec![
            "test_data/font_manifest_generator/a.font_catalog.json".to_string(),
            "test_data/font_manifest_generator/b.font_catalog.json".to_string(),
        ],
        vec![
            "test_data/font_manifest_generator/a.font_pkgs.json".to_string(),
            "test_data/font_manifest_generator/b.font_pkgs.json".to_string(),
        ],
        "test_data/font_manifest_generator/product_ab.all_fonts.json".to_string(),
        "test_data/font_manifest_generator/product_ab.local_fonts.json".to_string(),
        "test_data".to_string(),
        true,
        "test_data/font_manifest_generator/product_ab.font_manifest.json".to_string(),
    )?;

    Ok(())
}
