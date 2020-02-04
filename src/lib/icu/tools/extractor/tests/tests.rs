// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{ensure, Error},
    pretty_assertions::assert_eq,
    regex::Regex,
    std::{env, path::PathBuf, process::Command},
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

fn resolve_path(suffix: &str) -> Result<String, Error> {
    let mut path = test_path().join("exe.unstripped").join(suffix);
    if !path.exists() {
        path = test_path().join(suffix);
    }
    ensure!(path.exists(), "Not found: {:?}", &path);
    Ok(path.to_str().unwrap().to_string())
}

fn assert_valid_tz_version(tz_version: &str) -> Result<(), Error> {
    let re = Regex::new(r"^20[0-9][0-9][a-z]$")?;
    assert!(
        re.is_match(tz_version),
        "Actual tz version '{}' did not match /{:?}/",
        &tz_version,
        &re
    );
    Ok(())
}

#[test]
fn test_extract_tz_version_no_tzres() -> Result<(), Error> {
    let mut cmd = Command::new(resolve_path("icu_data_extractor")?);
    cmd.arg(format!(
        "--icu-data-file={}",
        resolve_path("test_data/icu_data_extractor/icudtl.dat")?
    ))
    .arg("tz-version");

    let output = cmd.output()?;
    assert!(output.status.success());

    let tz_version = std::str::from_utf8(&output.stdout)?;
    assert_valid_tz_version(&tz_version)
}

#[test]
fn test_extract_tz_version_tzres() -> Result<(), Error> {
    let mut cmd = Command::new(resolve_path("icu_data_extractor")?);
    cmd.arg(format!(
        "--icu-data-file={}",
        resolve_path("test_data/icu_data_extractor/icudtl.dat")?
    ))
    .arg(format!("--tz-res-dir={}", resolve_path("test_data/icu_data_extractor/tzres")?))
    .arg("tz-version");

    let output = cmd.output()?;
    assert!(output.status.success());

    let tz_version = std::str::from_utf8(&output.stdout)?;
    assert_valid_tz_version(&tz_version)
}

#[test]
fn test_extract_tz_ids() -> Result<(), Error> {
    let mut cmd = Command::new(resolve_path("icu_data_extractor")?);
    cmd.arg(format!(
        "--icu-data-file={}",
        resolve_path("test_data/icu_data_extractor/icudtl.dat")?
    ))
    .arg("tz-ids")
    .arg("--fixed-order=America/Los_Angeles,Europe/Paris,AET")
    .arg("--delimiter=,");

    let output = cmd.output()?;
    assert!(output.status.success());

    let tz_ids = std::str::from_utf8(&output.stdout)?;
    let expected = "America/Los_Angeles,Europe/Paris,AET,ACT,AGT";
    assert_eq!(&tz_ids[0..expected.len()], expected);

    Ok(())
}
