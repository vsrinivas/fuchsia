// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Result;
use ffx_core::ffx_plugin;
use ffx_packaging_import_args::ImportCommand;
use ffx_packaging_repository::Repository;
use std::fs::File;
use std::io::{Cursor, Write};

#[ffx_plugin("ffx_package")]
pub async fn cmd_package(cmd: ImportCommand) -> Result<()> {
    let repo = &Repository::default_repo().await?;
    cmd_package_import(cmd, std::io::stdout(), repo)?;
    Ok(())
}

pub fn cmd_package_import(cmd: ImportCommand, mut w: impl Write, repo: &Repository) -> Result<()> {
    let mut archive = File::open(cmd.archive)?;
    let mut archive = fuchsia_archive::Reader::new(&mut archive)?;
    let mut meta_far = Cursor::new(archive.read_file("meta.far")?);
    let meta_hash = repo.blobs().add_blob(&mut meta_far)?;
    meta_far.set_position(0);
    let mut meta_far = fuchsia_archive::Reader::new(&mut meta_far)?;
    let meta_contents = meta_far.read_file("meta/contents")?;
    let meta_contents =
        fuchsia_pkg::MetaContents::deserialize(meta_contents.as_slice())?.into_contents();
    for hash in meta_contents.values() {
        repo.blobs().add_blob(Cursor::new(archive.read_file(&hash.to_string())?))?;
    }
    writeln!(w, "{}", meta_hash)?;
    Ok(())
}

#[cfg(test)]
mod test {
    use crate::cmd_package_import;
    use anyhow::Result;
    use ffx_packaging_build::{cmd_package_build, BuildCommand};
    use ffx_packaging_export::{cmd_package_export, ExportCommand};
    use ffx_packaging_import_args::ImportCommand;
    use ffx_packaging_repository::Repository;
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
            published_name: "published-name".to_string(),
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
    fn test_build_export_import() -> Result<()> {
        // Build a test package
        let (tmpdir, repo) = make_test_repo()?;
        let mut stdout = io::Cursor::new(vec![]);
        cmd_package_build(build_command_for_test_package(tmpdir.path())?, &mut stdout, &repo)?;
        validate_blobs(tmpdir.path(), &TEST_PACKAGE_HASHES)?;
        assert!(fs::read_to_string(tmpdir.path().join("package_manifest.json"))?
            .contains("15ec7bf0b50732b49f8228e07d24365338f9e3ab994b00af08e5a3bffe55fd8b"));

        let hash = &std::str::from_utf8(stdout.get_ref())?;
        let hash = &hash[..hash.len() - 1];

        // Export it
        let temp_far = tempfile::NamedTempFile::new()?;
        let temp_far_path = temp_far.path().to_str().unwrap();
        cmd_package_export(
            ExportCommand { package: hash.to_owned(), output: temp_far_path.to_owned() },
            &repo,
        )?;

        // Import it into a fresh repository
        let (tmpdir, repo) = make_test_repo()?;
        let mut stdout = io::Cursor::new(vec![]);
        cmd_package_import(
            ImportCommand { archive: temp_far_path.to_owned() },
            &mut stdout,
            &repo,
        )?;
        assert_eq!(std::str::from_utf8(stdout.get_ref())?, format!("{}\n", TEST_PACKAGE_HASHES[0]));
        validate_blobs(tmpdir.path(), &TEST_PACKAGE_HASHES)
    }
}
