// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file

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

/// A tarball that can be read from.
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

    /// Reads a file from the tarball.
    pub fn get_file<F>(&self, path: &str, reader: F) -> Result<()>
    where
        F: FnOnce(&mut File) -> Result<()>,
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

    /// Reads a metadata object from the tarball.
    pub fn get_metadata<T: JsonObject>(&self, path: &str) -> Result<T> {
        let mut contents = String::new();
        self.get_file(path, |file| {
            file.read_to_string(&mut contents)?;
            Ok(())
        })?;
        T::new(contents.as_bytes())
    }
}

/// A tarball that can be written into.
pub struct OutputTarball {
    paths: HashSet<String>,
    builder: Builder<GzEncoder<File>>,
}

impl OutputTarball {
    /// Creates a new tarball.
    pub fn new(path: &str) -> Result<OutputTarball> {
        let output_path = Path::new(&path);
        if output_path.exists() {
            std::fs::remove_file(output_path)?;
        }
        let tar = File::create(&path)?;
        let tar_gz = GzEncoder::new(tar, Compression::default());
        Ok(OutputTarball {
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

    /// Writes the given content to the given path in the tarball.
    ///
    /// It is an error to write to the same path twice.
    pub fn write_json<T: JsonObject>(&mut self, path: &str, content: &T) -> Result<()> {
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

    /// Writes the given file to the given path in the tarball.
    ///
    /// It is an error to write to the same path twice.
    pub fn write_file(&mut self, path: &str, file: &mut File) -> Result<()> {
        self.add_path(path)?;
        Ok(self.builder.append_file(path, file)?)
    }

    /// Wraps up the creation of the tarball.
    pub fn export(&mut self) -> Result<()> {
        Ok(self.builder.finish()?)
    }
}
