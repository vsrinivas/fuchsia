// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod repository;

use anyhow::{anyhow, Context, Error};
use ffx_core::ffx_plugin;
use ffx_packaging_args::{BuildCommand, PackageCommand, SubCommand};
use std::collections::BTreeMap;
use std::fs;
use std::io::BufRead;
use std::path::PathBuf;

#[ffx_plugin()]
pub async fn cmd_package(cmd: PackageCommand) -> Result<(), Error> {
    let repo = &repository::DEFAULT_REPO;
    match cmd.sub {
        SubCommand::Build(subcmd) => cmd_package_build(subcmd, std::io::stdout(), repo),
    }
}

struct ManifestEntry {
    path: String,
    source: std::path::PathBuf,
}

fn parse_entry(entry: String) -> Result<ManifestEntry, Error> {
    let equals_index = entry.find('=').ok_or(anyhow!("manifest entry must contain ="))?;
    let (path, source) = entry.split_at(equals_index);
    let source = &source[1..];
    Ok(ManifestEntry { path: path.into(), source: source.into() })
}

fn cmd_package_build(
    cmd: BuildCommand,
    mut w: impl std::io::Write,
    repo: &repository::Repository,
) -> Result<(), Error> {
    let mut entries = Vec::new();
    for entry in cmd.entries {
        if entry.starts_with("@") {
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
        if entry.path.starts_with("meta/") {
            meta_files.insert(entry.path, fs::read(source)?);
        } else {
            contents.insert(entry.path, source);
        }
    }

    let meta_hash = build_package(repo, contents, meta_files)?;
    write!(w, "{}", meta_hash)?;
    Ok(())
}

fn build_package(
    repo: &repository::Repository,
    contents: BTreeMap<String, PathBuf>,
    mut meta_files: BTreeMap<String, Vec<u8>>,
) -> Result<fuchsia_merkle::Hash, Error> {
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

#[cfg(test)]
mod test {
    use crate::repository;
    use anyhow::Error;
    use std::fs;
    use std::io;

    #[test]
    fn test_build() -> Result<(), Error> {
        let tmp_dir = tempfile::TempDir::new()?;
        let tmp_path = tmp_dir.path();
        let repo = repository::Repository::new_with_blobs(tmp_path.into(), tmp_path.join("blobs"));
        fs::write(
            tmp_path.join("foo.cmx"),
            r#"{
            "program": {
                "binary": "bin/foo"
            }
        }"#,
        )?;
        fs::write(tmp_path.join("foo"), "")?;
        fs::write(tmp_path.join("entries.rsp"), "bin/foo=foo")?;
        let mut stdout = io::Cursor::new(vec![]);
        crate::cmd_package_build(
            ffx_packaging_args::BuildCommand {
                entries: vec![
                    "meta/foo.cmx=foo.cmx".into(),
                    format!("@{}", tmp_path.join("entries.rsp").to_string_lossy()),
                ],
                source_dir: Some(tmp_path.to_str().unwrap().into()),
            },
            &mut stdout,
            &repo,
        )?;

        assert_eq!(
            stdout.get_ref().as_slice(),
            "a5cad0a8391e1df27704c569e203461872724e648bdc4a887ac8736a7120daef".as_bytes()
        );
        for blob_dirent in tmp_path.join("blobs").read_dir()? {
            let blob_dirent = blob_dirent?;
            let hash =
                fuchsia_merkle::MerkleTree::from_reader(fs::File::open(blob_dirent.path())?)?
                    .root();
            assert_eq!(hash.to_string().as_str(), blob_dirent.file_name());
        }
        Ok(())
    }

    #[test]
    fn test_manifest_syntax_error() -> Result<(), Error> {
        let tmp_dir = tempfile::TempDir::new()?;
        let tmp_path = tmp_dir.path();
        let repo = repository::Repository::new_with_blobs(tmp_path.into(), tmp_path.join("blobs"));
        let res = crate::cmd_package_build(
            ffx_packaging_args::BuildCommand {
                entries: vec!["bad entry".into()],
                source_dir: None,
            },
            &mut std::io::stdout(),
            &repo,
        );
        assert!(res.is_err());
        Ok(())
    }
}
