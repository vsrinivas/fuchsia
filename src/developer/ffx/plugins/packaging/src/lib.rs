// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod archive;
mod constants;
mod repository;

use anyhow::{Context, Result};
use ffx_core::{ffx_error, ffx_plugin};
use ffx_packaging_args::{BuildCommand, PackageCommand, SubCommand};
use repository::Repository;
use std::collections::BTreeMap;
use std::fs;
use std::io::{BufRead, Write};
use std::path::PathBuf;

#[ffx_plugin()]
pub async fn cmd_package(cmd: PackageCommand) -> Result<()> {
    let repo = &Repository::default_repo().await;
    match cmd.sub {
        SubCommand::Build(subcmd) => cmd_package_build(subcmd, std::io::stdout(), repo),
        SubCommand::Export(subcmd) => archive::cmd_export(subcmd, repo),
        SubCommand::Import(subcmd) => archive::cmd_import(subcmd, std::io::stdout(), repo),
    }
}

struct ManifestEntry {
    path: String,
    source: std::path::PathBuf,
}

fn parse_entry(entry: String) -> Result<ManifestEntry> {
    let equals_index = entry.find('=').ok_or(ffx_error!("manifest entry must contain ="))?;
    let (path, source) = entry.split_at(equals_index);
    let source = &source[1..];
    Ok(ManifestEntry { path: path.into(), source: source.into() })
}

fn cmd_package_build(
    cmd: BuildCommand,
    mut w: impl std::io::Write,
    repo: &Repository,
) -> Result<()> {
    if cmd.depfile.is_some() && cmd.hash_out.is_none() {
        anyhow::bail!("--depfile only makes sense with --hash-out");
    }

    let mut entries = vec![];
    let mut deps = vec![];
    for entry in cmd.entries {
        if entry.starts_with("@") {
            deps.push(entry[1..].to_owned());
            let file = fs::File::open(&entry[1..])
                .context(format!("Couldn't open entry file {}", &entry[1..]))?;
            for line in std::io::BufReader::new(file).lines() {
                entries.push(parse_entry(line?)?);
            }
        } else {
            entries.push(parse_entry(entry)?);
        }
    }

    // collect contents from arguments
    let mut contents = BTreeMap::new();
    let mut meta_files = BTreeMap::new();
    for entry in entries {
        let mut source = entry.source;
        if let Some(ref source_dir) = cmd.source_dir {
            source = PathBuf::from(source_dir).join(source);
        }
        deps.push(source.to_str().unwrap().to_owned());
        if entry.path.starts_with("meta/") {
            meta_files.insert(entry.path, fs::read(source)?);
        } else {
            contents.insert(entry.path, source);
        }
    }

    let meta_hash = build_package(repo, contents, meta_files)?;
    if let Some(hash_out) = cmd.hash_out {
        writeln!(fs::File::create(&hash_out)?, "{}", meta_hash)?;
        if let Some(depfile) = cmd.depfile {
            write_depfile(depfile, hash_out, deps)?;
        }
    } else {
        writeln!(w, "{}", meta_hash)?;
    }
    Ok(())
}

fn build_package(
    repo: &Repository,
    contents: BTreeMap<String, PathBuf>,
    mut meta_files: BTreeMap<String, Vec<u8>>,
) -> Result<fuchsia_merkle::Hash> {
    // copy the blobs and build the meta/contents map
    let mut merkles = BTreeMap::new();
    for (path, source) in contents {
        let hash = repo.blobs().add_blob(fs::File::open(source)?)?;
        merkles.insert(path, hash);
    }
    let mut meta_contents = Vec::new();
    fuchsia_pkg::MetaContents::from_map(merkles)?.serialize(&mut meta_contents)?;
    meta_files.insert("meta/contents".into(), meta_contents);

    // construct the meta.far
    let meta_files = meta_files
        .iter()
        .map(|(path, content)| -> (&str, (_, Box<dyn std::io::Read>)) {
            (path, (content.len() as u64, Box::new(content.as_slice())))
        })
        .collect();
    let mut meta_far = std::io::Cursor::new(Vec::new());
    fuchsia_archive::write(&mut meta_far, meta_files)?;
    meta_far.set_position(0);
    repo.blobs().add_blob(&mut meta_far)
}

fn write_depfile(depfile: String, output_file: String, deps: Vec<String>) -> Result<()> {
    let mut f = fs::File::create(depfile)?;
    write!(f, "{}: ", output_file)?;
    for dep in deps {
        write!(f, "{} ", dep)?;
    }
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
        Ok(BuildCommand {
            entries: vec![
                "meta/foo.cmx=foo.cmx".into(),
                format!("@{}", tmp.join("entries.rsp").to_string_lossy()),
            ],
            source_dir: Some(tmp.to_str().unwrap().into()),
            ..Default::default()
        })
    }
    // These hashes should not change unless the package input changes
    pub static TEST_PACKAGE_HASHES: &'static [&str] = &[
        "01f9a5aa102a75f1f9e034f9ed3f57c0351bd3962ae283d9f58ec0c66b3ee486", // meta.far
        "15ec7bf0b50732b49f8228e07d24365338f9e3ab994b00af08e5a3bffe55fd8b", // bin/foo
    ];

    #[test]
    fn test_build() -> Result<()> {
        let (tmpdir, repo) = make_test_repo()?;
        let mut stdout = io::Cursor::new(vec![]);
        crate::cmd_package_build(
            build_command_for_test_package(tmpdir.path())?,
            &mut stdout,
            &repo,
        )?;

        assert_eq!(std::str::from_utf8(stdout.get_ref())?, format!("{}\n", TEST_PACKAGE_HASHES[0]));
        validate_blobs(tmpdir.path(), &TEST_PACKAGE_HASHES)?;
        Ok(())
    }
    #[test]
    fn test_build_hash_out() -> Result<()> {
        let (tmpdir, repo) = make_test_repo()?;
        let tmp = tmpdir.path();
        let mut cmd = build_command_for_test_package(tmpdir.path())?;
        cmd.hash_out = Some(tmp.join("hash.txt").to_str().unwrap().into());
        crate::cmd_package_build(cmd, &mut std::io::stdout(), &repo)?;

        assert_eq!(
            fs::read_to_string(tmp.join("hash.txt"))?,
            format!("{}\n", TEST_PACKAGE_HASHES[0])
        );
        validate_blobs(tmp, &TEST_PACKAGE_HASHES)?;
        Ok(())
    }

    #[test]
    fn test_build_with_depfile() -> Result<()> {
        let (tmpdir, repo) = make_test_repo()?;
        let tmp = tmpdir.path();

        // check error case
        let mut cmd = build_command_for_test_package(tmpdir.path())?;
        cmd.depfile = Some(tmp.join("foo.d").to_str().unwrap().into());
        assert!(crate::cmd_package_build(cmd, &mut std::io::stdout(), &repo).is_err());

        let mut cmd = build_command_for_test_package(tmpdir.path())?;
        cmd.depfile = Some(tmp.join("foo.d").to_str().unwrap().into());
        cmd.hash_out = Some(tmp.join("hash.txt").to_str().unwrap().into());
        crate::cmd_package_build(cmd, &mut std::io::stdout(), &repo)?;

        let mut expected_depfile = String::new();
        for entry in &["hash.txt:", "entries.rsp", "foo.cmx", "foo"] {
            expected_depfile.push_str(tmp.join(entry).to_str().unwrap());
            expected_depfile.push(' ');
        }
        assert_eq!(fs::read_to_string(tmp.join("foo.d"))?, expected_depfile);
        Ok(())
    }

    #[test]
    fn test_manifest_syntax_error() -> Result<()> {
        let (_tmpdir, repo) = make_test_repo()?;
        let res = crate::cmd_package_build(
            BuildCommand { entries: vec!["bad entry".into()], ..Default::default() },
            &mut std::io::stdout(),
            &repo,
        );
        assert!(res.is_err());
        Ok(())
    }

    pub fn make_test_repo() -> Result<(tempfile::TempDir, Repository)> {
        let tmp_dir = tempfile::TempDir::new()?;
        let tmp_path = tmp_dir.path();
        let repo = Repository::new(tmp_path.into(), tmp_path.join("blobs"));
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
}
