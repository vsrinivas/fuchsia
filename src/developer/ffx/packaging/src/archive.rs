// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::repository::Repository;
use anyhow::Result;
use ffx_packaging_args::{ExportCommand, ImportCommand};
use std::collections::BTreeMap;
use std::io::Seek;
use std::{fs, io};

pub fn cmd_export(cmd: ExportCommand, repo: &Repository) -> Result<()> {
    let mut meta_far_blob = repo.blobs().open_blob(&cmd.package.parse()?)?;
    let mut meta_far = fuchsia_archive::Reader::new(&mut meta_far_blob)?;
    let meta_contents = meta_far.read_file("meta/contents")?;
    let meta_contents =
        fuchsia_pkg::MetaContents::deserialize(meta_contents.as_slice())?.into_contents();
    let mut contents: BTreeMap<_, (_, Box<dyn io::Read>)> = BTreeMap::new();
    for hash in meta_contents.values() {
        let blob = repo.blobs().open_blob(&hash)?;
        contents.insert(hash.to_string(), (blob.metadata()?.len(), Box::new(blob)));
    }
    meta_far_blob.seek(io::SeekFrom::Start(0))?;
    contents
        .insert("meta.far".to_string(), (meta_far_blob.metadata()?.len(), Box::new(meta_far_blob)));
    let output = fs::File::create(cmd.output)?;
    fuchsia_archive::write(output, contents)?;
    Ok(())
}

pub fn cmd_import(cmd: ImportCommand, mut w: impl io::Write, repo: &Repository) -> Result<()> {
    let mut archive = fs::File::open(cmd.archive)?;
    let mut archive = fuchsia_archive::Reader::new(&mut archive)?;
    let mut meta_far = io::Cursor::new(archive.read_file("meta.far")?);
    let meta_hash = repo.blobs().add_blob(&mut meta_far)?;
    meta_far.set_position(0);
    let mut meta_far = fuchsia_archive::Reader::new(&mut meta_far)?;
    let meta_contents = meta_far.read_file("meta/contents")?;
    let meta_contents =
        fuchsia_pkg::MetaContents::deserialize(meta_contents.as_slice())?.into_contents();
    for hash in meta_contents.values() {
        repo.blobs().add_blob(io::Cursor::new(archive.read_file(&hash.to_string())?))?;
    }
    writeln!(w, "{}", meta_hash)?;
    Ok(())
}

#[cfg(test)]
mod test {
    use crate::archive;
    use crate::test::{
        build_command_for_test_package, make_test_repo, validate_blobs, TEST_PACKAGE_HASHES,
    };
    use anyhow::Result;
    use ffx_packaging_args::{ExportCommand, ImportCommand};
    use std::io;

    #[test]
    fn test_import_export() -> Result<()> {
        // Build a test package
        let (tmpdir, repo) = make_test_repo()?;
        let mut stdout = io::Cursor::new(vec![]);
        crate::cmd_package_build(
            build_command_for_test_package(tmpdir.path())?,
            &mut stdout,
            &repo,
        )?;
        let hash = &std::str::from_utf8(stdout.get_ref())?;
        let hash = &hash[..hash.len() - 1];

        // Export it
        let temp_far = tempfile::NamedTempFile::new()?;
        let temp_far_path = temp_far.path().to_str().unwrap();
        archive::cmd_export(
            ExportCommand { package: hash.to_owned(), output: temp_far_path.to_owned() },
            &repo,
        )?;

        // Import it into a fresh repository
        let (tmpdir, repo) = make_test_repo()?;
        let mut stdout = io::Cursor::new(vec![]);
        archive::cmd_import(
            ImportCommand { archive: temp_far_path.to_owned() },
            &mut stdout,
            &repo,
        )?;
        assert_eq!(std::str::from_utf8(stdout.get_ref())?, format!("{}\n", TEST_PACKAGE_HASHES[0]));
        validate_blobs(tmpdir.path(), &TEST_PACKAGE_HASHES)?;
        Ok(())
    }
}
