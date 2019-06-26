// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::HashSet;
use std::convert::TryFrom;
use std::fs::File;
use std::io::Read;
use std::path::Path;
use std::time::{SystemTime, UNIX_EPOCH};

use flate2::read::GzDecoder;
use flate2::write::GzEncoder;
use flate2::Compression;
use tar::{Archive, Builder, EntryType, Header};
use tempfile::TempDir;

use sdk_metadata::JsonObject;

use crate::app::{Error, Result};

/// A trait for types representing tarball contents.
pub trait TarballContent {
    /// Returns true if this piece of content is identical to a given piece of content.
    fn is_identical(&self, other: &Self) -> Result<bool>;
}

impl TarballContent for File {
    fn is_identical(&self, other: &File) -> Result<bool> {
        // Compare file sizes to avoid reading the full file contents.
        Ok(self.metadata()?.len() == other.metadata()?.len())
    }
}

/// A tarball that can be read from.
pub trait InputTarball<F: TarballContent> {
    /// Reads a file from the tarball.
    fn get_file<R>(&self, path: &str, reader: R) -> Result<()>
    where
        R: FnOnce(&mut F) -> Result<()>;

    /// Reads a metadata object from the tarball.
    fn get_metadata<T: JsonObject>(&self, path: &str) -> Result<T>;
}

/// Implementation of |InputTarball| for the |File| type.
pub struct SourceTarball {
    base: TempDir,
}

impl SourceTarball {
    /// Opens the tarball the given path.
    pub fn new(path: &str) -> Result<SourceTarball> {
        let tar_gz = File::open(path)?;
        let tar = GzDecoder::new(tar_gz);
        let mut archive = Archive::new(tar);
        let tempdir = tempfile::Builder::new().prefix("merge_sdk").tempdir()?;
        archive.unpack(tempdir.path())?;
        Ok(SourceTarball { base: tempdir })
    }
}

impl InputTarball<File> for SourceTarball {
    fn get_file<R>(&self, path: &str, reader: R) -> Result<()>
    where
        R: FnOnce(&mut File) -> Result<()>,
    {
        let mut file =
            File::open(self.base.path().join(path)).or(Err(Error::ArchiveFileNotFound {
                name: path.to_string(),
            }))?;
        // Pass the file as a reference to a callback to ensure the File object is being properly
        // disposed of. This ensures the temporary base directory gets removed when the program is
        // done.
        reader(&mut file)
    }

    fn get_metadata<T: JsonObject>(&self, path: &str) -> Result<T> {
        let mut contents = String::new();
        self.get_file(path, |file| {
            file.read_to_string(&mut contents)?;
            Ok(())
        })?;
        T::new(contents.as_bytes())
    }
}

/// A tarball that can be written into.
pub trait OutputTarball<F: TarballContent> {
    /// Writes the given content to the given path in the tarball.
    ///
    /// It is an error to write to the same path twice.
    fn write_json<T: JsonObject>(&mut self, path: &str, content: &T) -> Result<()>;

    /// Writes the given file to the given path in the tarball.
    ///
    /// It is an error to write to the same path twice.
    fn write_file(&mut self, path: &str, file: &mut F) -> Result<()>;
}

/// Implementation of |OutputTarball| for the |File| type.
pub struct ResultTarball {
    paths: HashSet<String>,
    builder: Builder<GzEncoder<File>>,
}

impl ResultTarball {
    /// Creates a new tarball.
    pub fn new(path: &str) -> Result<ResultTarball> {
        let output_path = Path::new(&path);
        if output_path.exists() {
            std::fs::remove_file(output_path)?;
        }
        let tar = File::create(&path)?;
        let tar_gz = GzEncoder::new(tar, Compression::default());
        Ok(ResultTarball {
            paths: HashSet::new(),
            builder: Builder::new(tar_gz),
        })
    }

    /// Registers a path in the tarball, erroring out if the path already exists.
    fn add_path(&mut self, path: &str) -> Result<()> {
        if self.paths.insert(path.to_owned()) {
            return Ok(());
        }
        Err(Error::PathAlreadyExists {
            path: path.to_string(),
        })?
    }

    /// Wraps up the creation of the tarball.
    pub fn export(&mut self) -> Result<()> {
        Ok(self.builder.finish()?)
    }
}

impl OutputTarball<File> for ResultTarball {
    fn write_json<T: JsonObject>(&mut self, path: &str, content: &T) -> Result<()> {
        self.add_path(path)?;
        let string = content.to_string()?;
        let bytes = string.as_bytes();
        let mut header = Header::new_gnu();
        header.set_path(path)?;
        header.set_size(u64::try_from(bytes.len())?);
        header.set_entry_type(EntryType::Regular);
        // Make the file readable.
        header.set_mode(0o444);
        // Add a timestamp.
        let start = SystemTime::now();
        let epoch = start.duration_since(UNIX_EPOCH)?.as_secs();
        header.set_mtime(epoch);
        Ok(self.builder.append_data(&mut header, path, bytes)?)
    }

    fn write_file(&mut self, path: &str, file: &mut File) -> Result<()> {
        self.add_path(path)?;
        Ok(self.builder.append_file(path, file)?)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_packing_unpacking() {
        let content_path = "some/thing.txt";
        let content = "yay it worked!";

        let tempdir = tempfile::Builder::new()
            .prefix("test_tarballs")
            .tempdir()
            .unwrap();

        // Write the file to add to a tarball.
        let file_path = tempdir.path().join("file.txt");
        let file_path_str = file_path.to_str().unwrap();
        std::fs::write(&file_path_str, content).unwrap();

        let tarball_path = tempdir.path().join("tarball.tar.gz");
        let tarball_path_str = tarball_path.to_str().unwrap();

        // Generate the tarball.
        {
            // The scope is needed in order for the tarball to get properly written...
            let mut output = ResultTarball::new(tarball_path_str).unwrap();
            let mut content_file = File::open(&file_path_str).unwrap();
            output.write_file(content_path, &mut content_file).unwrap();
            output.export().unwrap();
        }

        // Unpack the tarball.
        let input = SourceTarball::new(&tarball_path_str).unwrap();
        let mut read_content = String::new();
        input
            .get_file(content_path, |file| {
                file.read_to_string(&mut read_content)?;
                Ok(())
            })
            .unwrap();
        assert_eq!(content, read_content);
    }
}
