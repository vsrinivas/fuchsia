// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::blobfs_export::blobfs_export,
    anyhow::{anyhow, Result},
    tempfile,
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
    pub fn new(input_path: &std::path::Path) -> Result<Self> {
        let output_dir = tempfile::TempDir::new()?;
        log::info!("Running blobfs_export export {:?} {:?}", input_path, output_dir);
        blobfs_export(
            input_path.to_str().ok_or(anyhow!("Invalid input path {:?}", input_path))?,
            output_dir.path().to_str().ok_or(anyhow!("Invalid output path {:?}", output_dir))?,
        )?;
        Ok(Self { directory: output_dir })
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

#[cfg(test)]
mod tests {
    use {super::*, std::io::Write};

    #[test]
    fn test_blobfs_zero_length_file_invalid() {
        let file = tempfile::NamedTempFile::new().expect("Create temp file failed");
        let path = file.into_temp_path();
        BlobFsReader::new(&path).expect_err("Expected empty blobfs to fail reading");
    }

    #[test]
    fn test_blobfs_garbage_file_invalid() {
        let mut file = tempfile::NamedTempFile::new().expect("Create temp file failed");
        file.as_file_mut().write_all(&[0u8, 1u8, 2u8, 3u8]).expect("Write temp file failed");
        let path = file.into_temp_path();
        BlobFsReader::new(&path).expect_err("Expected garbage blobfs to fail reading");
    }
}
