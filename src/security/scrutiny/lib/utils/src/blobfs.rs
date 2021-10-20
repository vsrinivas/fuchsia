// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, bail, Context, Result},
    std::process::Command,
};

#[derive(Debug, PartialEq)]
pub struct Blob {
    pub merkle: String,
    pub buffer: Vec<u8>,
}

#[derive(Debug)]
pub struct BlobFsReader {
    directory: tempfile::TempDir,
}

impl BlobFsReader {
    /// Constructs a new reader from a file.  Validates the contents of blobfs at this point.
    /// While the reader exists, a temporary directory containing the extracted blobfs image will
    /// exist.  This directory is removed on Drop.
    pub fn new(tool_path: &std::path::Path, path: &std::path::Path) -> Result<Self> {
        let directory = tempfile::TempDir::new()?;
        export_blobfs(tool_path, path, directory.path())?;
        Ok(Self { directory })
    }

    /// Reads all blobs from blobfs.
    pub fn read_blobs(&self) -> Result<Vec<Blob>> {
        let mut blobs = vec![];
        for file in std::fs::read_dir(self.directory.path())? {
            let file = file?;
            let merkle = file
                .file_name()
                .into_string()
                .map_err(|name| anyhow!("Invalid filename {:?}", name))?;
            let buffer = std::fs::read(file.path())?;
            blobs.push(Blob { merkle, buffer });
        }
        Ok(blobs)
    }
}

pub fn export_blobfs(
    tool_path: &std::path::Path,
    input_path: &std::path::Path,
    output_dir: &std::path::Path,
) -> Result<()> {
    log::info!("Running {:?} export {:?} {:?}", tool_path, input_path, output_dir);
    // TODO(fxbug.dev/76378): Take the tool location from a config.
    if !Command::new(tool_path.as_os_str())
        .args([
            "export",
            input_path.to_str().ok_or(anyhow!("Invalid input path {:?}", input_path))?,
            output_dir.to_str().ok_or(anyhow!("Invalid output path {:?}", output_dir))?,
        ])
        .status()
        .context("Failed to launch blobfs export")?
        .success()
    {
        bail!("Failed to export blobfs");
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::{io::Write, path::Path};

    fn blobfs_path() -> std::path::PathBuf {
        let mut path = std::env::current_exe().unwrap();
        path.pop();
        path.push("blobfs");
        path
    }

    fn create_blobfs(files: Vec<Vec<u8>>) -> Result<tempfile::NamedTempFile> {
        let dir = tempfile::TempDir::new()?;
        let mut i = 0;
        let mut file_paths = vec![];
        for data in files {
            let mut path = dir.path().to_owned();
            path.push(format!("{}", i));
            let mut file = std::fs::File::create(path.clone())?;
            file.write_all(&data[..])?;
            file_paths.push(path.to_str().unwrap().to_owned());
            i += 1;
        }
        let mut manifest = tempfile::NamedTempFile::new()?;
        manifest.as_file_mut().write_all(&file_paths.join("\n").into_bytes()[..])?;

        let file = tempfile::NamedTempFile::new()?;
        if !Command::new(&blobfs_path())
            .args([
                file.path().to_str().unwrap(),
                "create",
                "--manifest",
                manifest.path().to_str().unwrap(),
            ])
            .status()
            .context(format!("Failed to launch blobfs create, path {:?}", blobfs_path()))?
            .success()
        {
            bail!("Failed to create blobfs");
        }
        Ok(file)
    }

    #[test]
    fn test_blobfs_zero_length_file_invalid() {
        let file = tempfile::NamedTempFile::new().expect("Create temp file failed");
        let path = file.into_temp_path();
        BlobFsReader::new(&Path::new(&blobfs_path()), &path)
            .expect_err("Expected empty blobfs to fail reading");
    }

    #[test]
    fn test_blobfs_garbage_file_invalid() {
        let mut file = tempfile::NamedTempFile::new().expect("Create temp file failed");
        file.as_file_mut().write_all(&[0u8, 1u8, 2u8, 3u8]).expect("Write temp file failed");
        let path = file.into_temp_path();
        BlobFsReader::new(&Path::new(&blobfs_path()), &path)
            .expect_err("Expected garbage blobfs to fail reading");
    }

    #[test]
    fn test_blobfs_empty_valid() {
        let file = create_blobfs(vec![]).expect("create_blobfs failed");
        let path = file.into_temp_path();
        let reader = BlobFsReader::new(Path::new(&blobfs_path()), &path)
            .expect("Failed to initialize empty blobfs reader");
        assert_eq!(reader.read_blobs().expect("Failed to read blobs"), vec![]);
    }

    #[test]
    fn test_blobfs_valid() {
        let file = create_blobfs(vec![vec![], vec![0u8, 1u8, 2u8]]).expect("create_blobfs failed");
        let path = file.into_temp_path();
        let reader = BlobFsReader::new(Path::new(&blobfs_path()), &path)
            .expect("Failed to initialize empty blobfs reader");
        let blobs = reader.read_blobs().expect("Failed to read blobs");
        assert_eq!(blobs.len(), 2);
        let mut expected_contents = vec![vec![], vec![0u8, 1u8, 2u8]];
        for file in blobs {
            match expected_contents.iter().position(|x| x == &file.buffer) {
                Some(idx) => expected_contents.remove(idx),
                None => panic!("Unexpected entry {:?}", file),
            };
        }
        assert_eq!(expected_contents.len(), 0);
    }
}
