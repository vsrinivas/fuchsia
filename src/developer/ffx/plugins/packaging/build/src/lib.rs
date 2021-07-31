// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context, Result};
use ffx_core::ffx_plugin;
pub use ffx_packaging_build_args::BuildCommand;
use ffx_packaging_repository::Repository;
use fuchsia_pkg::{build, CreationManifest};
use std::fs::File;
use std::io::{BufRead, Write};
use std::path::PathBuf;

#[ffx_plugin("ffx_package")]
pub async fn cmd_package(cmd: BuildCommand) -> Result<()> {
    let repo = &Repository::default_repo().await?;
    cmd_package_build(cmd, std::io::stdout(), repo)?;
    Ok(())
}

fn append_source_dir(line: String, source_dir: Option<String>) -> String {
    if let Some(dir) = source_dir {
        let mut to_string = "=".to_owned();
        to_string.push_str(&dir);
        to_string.push_str("/");

        return line.replace("=", &to_string);
    }
    line
}

pub fn cmd_package_build(
    cmd: BuildCommand,
    mut w: impl std::io::Write,
    repo: &Repository,
) -> Result<()> {
    let mut entries = vec![];
    let mut deps = vec![];
    for entry in cmd.entries {
        if entry.starts_with("@") {
            deps.push(entry[1..].to_owned());
            let file = File::open(&entry[1..])
                .context(format!("Couldn't open entry file {}", &entry[1..]))?;
            for line in std::io::BufReader::new(file).lines() {
                entries.push(append_source_dir(line?, cmd.source_dir.clone()));
            }
        } else {
            entries.push(append_source_dir(entry, cmd.source_dir.clone()));
        }
    }

    let out_dir = match cmd.source_dir {
        Some(dir) => PathBuf::from(dir),
        None => PathBuf::from("."),
    };

    let pm_fini = entries.join("\n");
    let creation_manifest = CreationManifest::from_pm_fini(pm_fini.as_bytes())?;
    let meta_far_path = out_dir.join("meta.far");
    let package_manifest = build(&creation_manifest, &meta_far_path)?;
    let package_manifest_path = out_dir.join("package_manifest.json");
    let mut file = File::create(package_manifest_path)?;
    file.write_all(serde_json::to_string(&package_manifest)?.as_bytes())?;

    let blobs = package_manifest.into_blobs();
    for info in blobs {
        repo.blobs().add_blob(File::open(info.source_path)?)?;
    }

    // The meta-far will not get added twice, but we `add_blob` to get the hash.
    let meta_hash = repo.blobs().add_blob(File::open(&meta_far_path)?)?;

    if let Some(hash_out) = cmd.hash_out {
        writeln!(File::create(&hash_out)?, "{}", meta_hash)?;
    } else {
        writeln!(w, "{}", meta_hash)?;
    }
    Ok(())
}
