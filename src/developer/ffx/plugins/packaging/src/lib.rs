// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod archive;
mod constants;
mod repository;
mod tuf_repo;

use anyhow::{Context, Result};
use ffx_core::ffx_plugin;
use ffx_packaging_args::{BuildCommand, DownloadCommand, PackageCommand, SubCommand};
use fuchsia_pkg::{build, CreationManifest};
use pkg::repository::http_repository::package_download;
use repository::Repository;
use std::fs;
use std::fs::File;
use std::io::{BufRead, Write};
use std::path::PathBuf;

#[ffx_plugin()]
pub async fn cmd_package(cmd: PackageCommand) -> Result<()> {
    let repo = &Repository::default_repo().await?;
    match cmd.sub {
        SubCommand::Build(subcmd) => cmd_package_build(subcmd, std::io::stdout(), repo),
        SubCommand::Download(subcmd) => cmd_package_download(subcmd).await,
        SubCommand::Export(subcmd) => archive::cmd_export(subcmd, repo),
        SubCommand::Import(subcmd) => archive::cmd_import(subcmd, std::io::stdout(), repo),
    }
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

fn cmd_package_build(
    cmd: BuildCommand,
    mut w: impl std::io::Write,
    repo: &Repository,
) -> Result<()> {
    let mut entries = vec![];
    let mut deps = vec![];
    for entry in cmd.entries {
        if entry.starts_with("@") {
            deps.push(entry[1..].to_owned());
            let file = fs::File::open(&entry[1..])
                .context(format!("Couldn't open entry file {}", &entry[1..]))?;
            for line in std::io::BufReader::new(file).lines() {
                entries.push(append_source_dir(line?, cmd.source_dir.clone()));
            }
        } else {
            entries.push(append_source_dir(entry, cmd.source_dir.clone()));
        }
    }

    let pm_fini = entries.join("\n");
    let creation_manifest = CreationManifest::from_pm_fini(pm_fini.as_bytes())?;
    let mut meta_far_writer = Vec::new();
    let package_manifest = build(&creation_manifest, &mut meta_far_writer)?;

    let out_dir = match cmd.source_dir {
        Some(dir) => PathBuf::from(dir),
        None => PathBuf::from("."),
    };
    let package_manifest_path = out_dir.join("package_manifest.json");
    let mut file = File::create(package_manifest_path)?;
    file.write_all(serde_json::to_string(&package_manifest)?.as_bytes())?;
    let meta_far_path = out_dir.join("meta.far");
    let mut file = File::create(meta_far_path)?;
    file.write_all(meta_far_writer.as_slice())?;

    let blobs = package_manifest.into_blobs();
    for info in blobs {
        repo.blobs().add_blob(File::open(info.source_path)?)?;
    }
    let mut cursor = std::io::Cursor::new(meta_far_writer);
    let meta_hash = repo.blobs().add_blob(&mut cursor)?;

    if let Some(hash_out) = cmd.hash_out {
        writeln!(fs::File::create(&hash_out)?, "{}", meta_hash)?;
    } else {
        writeln!(w, "{}", meta_hash)?;
    }
    Ok(())
}

async fn cmd_package_download(cmd: DownloadCommand) -> Result<()> {
    package_download(cmd.tuf_url, cmd.blob_url, cmd.target_path, cmd.output_path).await?;
    Ok(())
}

#[cfg(test)]
mod test {
    use crate::repository::Repository;
    use anyhow::Result;
    use ffx_packaging_args::BuildCommand;
    use std::{fs, io, path};

    pub fn build_command_for_test_package(tmp: &path::Path) -> Result<BuildCommand> {
        fs::write(
            tmp.join("foo.cmx"),
            r#"{
                "program": {
                    "binary": "bin/foo"
                }
            }"#,
        )?;
        fs::write(tmp.join("foo"), "")?;
        fs::write(tmp.join("entries.rsp"), "bin/foo=foo")?;
        fs::write(tmp.join("package"), r#"{"name":"test", "version":"0"}"#)?;
        Ok(BuildCommand {
            entries: vec![
                "meta/foo.cmx=foo.cmx".into(),
                "meta/package=package".into(),
                format!("@{}", tmp.join("entries.rsp").to_string_lossy()),
            ],
            source_dir: Some(tmp.to_str().unwrap().into()),
            ..Default::default()
        })
    }
    // These hashes should not change unless the package input changes
    pub static TEST_PACKAGE_HASHES: &'static [&str] = &[
        "7c828dd5d21c9fcf746acac80371178255eb6f1d2b6574962014f59807e3544e", // meta.far
        "15ec7bf0b50732b49f8228e07d24365338f9e3ab994b00af08e5a3bffe55fd8b", // bin/foo
    ];

    pub fn make_test_repo() -> Result<(tempfile::TempDir, Repository)> {
        let tmp_dir = tempfile::TempDir::new()?;
        let tmp_path = tmp_dir.path();
        let repo = Repository::new(tmp_path.into(), tmp_path.join("blobs"))?;
        Ok((tmp_dir, repo))
    }

    pub fn validate_blobs(tmp: &path::Path, blob_hashes: &[&str]) -> Result<()> {
        let blobs_dir = tmp.join("blobs");
        for blob_hash in blob_hashes {
            let blob = blobs_dir.join(blob_hash);
            let hash = fuchsia_merkle::MerkleTree::from_reader(fs::File::open(blob)?)?.root();
            assert_eq!(hash.to_string(), *blob_hash);
        }
        Ok(())
    }

    #[test]
    fn test_build() -> Result<()> {
        // Build a test package
        let (tmpdir, repo) = make_test_repo()?;
        let mut stdout = io::Cursor::new(vec![]);
        crate::cmd_package_build(
            build_command_for_test_package(tmpdir.path())?,
            &mut stdout,
            &repo,
        )?;
        validate_blobs(tmpdir.path(), &TEST_PACKAGE_HASHES)?;
        assert!(fs::read_to_string(tmpdir.path().join("package_manifest.json"))?
            .contains("15ec7bf0b50732b49f8228e07d24365338f9e3ab994b00af08e5a3bffe55fd8b"));
        Ok(())
    }
}
