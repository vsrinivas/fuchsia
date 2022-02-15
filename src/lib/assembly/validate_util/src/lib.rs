// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Utilities for validating packages and components, separate from the `assembly_validate_product`
//! crate so they can be depended upon by libraries used to implement product validation.

use std::{
    collections::BTreeMap,
    error::Error,
    fmt::{Debug, Display},
    io::{Read, Seek},
    path::{Path, PathBuf},
};

/// A type which can be used to describe the `/pkg` directory in a component's namespace.
pub trait PkgNamespace {
    /// Error type returned by namespace lookups/reads.
    type Err: Debug + Display + Error + Send + Sync + 'static;

    /// Return a list of all paths in the package visible to the component.
    fn paths(&self) -> Vec<String>;

    /// Read the contents of `path`.
    fn read_file(&mut self, path: &str) -> Result<Vec<u8>, Self::Err>;
}

// NOTE: this implementation doesn't allow access to anything outside of `/pkg/meta`
impl<T> PkgNamespace for fuchsia_archive::Reader<T>
where
    T: Read + Seek,
{
    type Err = fuchsia_archive::Error;

    fn paths(&self) -> Vec<String> {
        self.list().map(|e| e.path().to_owned()).collect()
    }

    fn read_file(&mut self, path: &str) -> Result<Vec<u8>, Self::Err> {
        self.read_file(path)
    }
}

/// An in-memory representation of the bootfs for a given system image.
pub struct BootfsContents {
    files: BTreeMap<String, Vec<u8>>,
}

impl BootfsContents {
    pub fn from_iter(
        bootfs_files: impl Iterator<Item = (impl AsRef<str>, impl AsRef<Path>)>,
    ) -> Result<Self, BootfsContentsError> {
        let mut files = BTreeMap::new();
        for (dest, source) in bootfs_files {
            let source = source.as_ref();
            let contents = std::fs::read(source).map_err(|e| {
                BootfsContentsError::ReadSourceFile { path: source.to_owned(), source: e }
            })?;
            files.insert(dest.as_ref().to_owned(), contents);
        }
        Ok(Self { files })
    }
}

impl PkgNamespace for BootfsContents {
    type Err = BootfsContentsError;

    fn paths(&self) -> Vec<String> {
        self.files.keys().cloned().collect()
    }

    fn read_file(&mut self, path: &str) -> Result<Vec<u8>, Self::Err> {
        self.files.get(path).map(|b| b.to_owned()).ok_or(BootfsContentsError::NoSuchFile)
    }
}

#[derive(Debug, thiserror::Error)]
pub enum BootfsContentsError {
    #[error("Failed to read `{}`.", path.display())]
    ReadSourceFile {
        path: PathBuf,
        #[source]
        source: std::io::Error,
    },
    #[error("No such file in bootfs.")]
    NoSuchFile,
}
