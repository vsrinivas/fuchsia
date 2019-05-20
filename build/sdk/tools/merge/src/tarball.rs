// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file

use std::collections::HashMap;
use std::convert::TryFrom;
use std::fs::File;
use std::io::Read;
use std::path::Path;
use std::time::{SystemTime, UNIX_EPOCH};

use flate2::read::GzDecoder;
use flate2::write::GzEncoder;
use flate2::Compression;
use tar::{Archive, Builder, EntryType, Header};

use sdk_metadata::JsonObject;

use crate::app::{Error, Result};

type SdkArchive = Archive<GzDecoder<File>>;

/// A tarball that can be read from.
pub struct SourceTarball {
    archive: SdkArchive,
}

impl SourceTarball {
    /// Creates a new tarball at the given path.
    pub fn new(path: String) -> Result<SourceTarball> {
        let tar_gz = File::open(path)?;
        let tar = GzDecoder::new(tar_gz);
        Ok(SourceTarball {
            archive: Archive::new(tar),
        })
    }

    /// Reads a file from the tarball.
    fn get_file<'a>(&'a mut self, path: &str) -> Result<impl Read + 'a> {
        let archive = &mut self.archive;
        let entries = archive.entries()?;
        Ok(entries
            .filter_map(|entry| entry.ok())
            .find(|entry| {
                if let Ok(entry_path) = entry.path() {
                    return entry_path.to_str() == Some(path);
                }
                false
            })
            .ok_or(Error::ArchiveFileNotFound {
                name: path.to_string(),
            })?)
    }

    /// Reads a metadata object from the tarball.
    pub fn get_metadata<T: JsonObject>(&mut self, path: &str) -> Result<T> {
        T::new(self.get_file(path)?)
    }
}

/// The types of content that can be written to a tarball.
enum TarballContent {
    /// Plain string content, exported as a read-only file.
    Plain(String),
}

/// A tarball that can be written into.
pub struct OutputTarball {
    contents: HashMap<String, TarballContent>,
}

impl OutputTarball {
    /// Creates a new tarball.
    pub fn new() -> OutputTarball {
        OutputTarball {
            contents: HashMap::new(),
        }
    }

    /// Writes the given content to the given path in the tarball.
    ///
    /// It is an error to write to the same path twice.
    pub fn write(&mut self, path: String, content: String) -> Result<()> {
        if let Some(_) = self
            .contents
            .insert(path.clone(), TarballContent::Plain(content))
        {
            return Err(Error::PathAlreadyExists { path })?;
        }
        Ok(())
    }

    /// Creates the tarball at the given path.
    ///
    /// This method will obliterate any file that already exists at that path.
    pub fn export(&self, path: String) -> Result<()> {
        let output_path = Path::new(&path);
        if output_path.exists() {
            std::fs::remove_file(output_path)?;
        }
        let tar = File::create(&path)?;
        let tar_gz = GzEncoder::new(tar, Compression::default());
        let mut builder = Builder::new(tar_gz);
        for (file_path, content) in &self.contents {
            match content {
                TarballContent::Plain(s) => {
                    let bytes = s.as_bytes();
                    let mut header = Header::new_gnu();
                    header.set_path(file_path)?;
                    header.set_size(u64::try_from(bytes.len())?);
                    header.set_entry_type(EntryType::Regular);
                    // Make the file readable.
                    header.set_mode(0o444);
                    // Add a timestamp.
                    let start = SystemTime::now();
                    let epoch = start.duration_since(UNIX_EPOCH)?.as_secs();
                    header.set_mtime(epoch);
                    builder.append_data(&mut header, file_path, bytes)?;
                }
            }
        }
        Ok(builder.finish()?)
    }
}
