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

mod inner_impl {
    use super::*;
    pub trait PkgNamespaceInner {
        /// Error type returned by namespace lookups/reads.
        type Err: Debug + Display + Error + Send + Sync + 'static;

        /// Read the contents of `path`.
        fn read_impl(&mut self, path: &str) -> Result<Vec<u8>, Self::Err>;
    }
}
use inner_impl::PkgNamespaceInner;

/// A type which can be used to describe the `/pkg` directory in a component's namespace.
pub trait PkgNamespace: PkgNamespaceInner {
    /// Return a list of all paths in the package visible to the component.
    fn paths(&self) -> Vec<String>;

    fn read_file(&mut self, path: &str) -> Result<Vec<u8>, ReadError> {
        match self.read_impl(path) {
            Ok(b) => Ok(b),
            Err(e) => {
                let same_ext_alternatives = if let Some((_, extension)) = path.rsplit_once('.') {
                    self.paths().into_iter().filter(|p| p.ends_with(extension)).collect()
                } else {
                    vec![]
                };
                Err(ReadError { inner: e.to_string(), same_ext_alternatives })
            }
        }
    }
}

#[derive(Debug)]
pub struct ReadError {
    inner: String,
    same_ext_alternatives: Vec<String>,
}

impl Display for ReadError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(&self.inner)?;

        if !self.same_ext_alternatives.is_empty() {
            f.write_str("\n\tPossible alternatives with the same extension: [")?;
            for path in &self.same_ext_alternatives {
                f.write_str("\n\t    ")?;
                f.write_str(path)?;
                f.write_str(",")?;
            }
            f.write_str("\n\t]")?;
        }

        Ok(())
    }
}

impl Error for ReadError {}

// NOTE: this implementation doesn't allow access to anything outside of `/pkg/meta`
impl<T> PkgNamespaceInner for fuchsia_archive::Reader<T>
where
    T: Read + Seek,
{
    type Err = fuchsia_archive::Error;

    fn read_impl(&mut self, path: &str) -> Result<Vec<u8>, Self::Err> {
        self.read_file(path)
    }
}

impl<T> PkgNamespace for fuchsia_archive::Reader<T>
where
    T: Read + Seek,
{
    fn paths(&self) -> Vec<String> {
        self.list().map(|e| e.path().to_owned()).collect()
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

impl PkgNamespaceInner for BootfsContents {
    type Err = BootfsContentsError;

    fn read_impl(&mut self, path: &str) -> Result<Vec<u8>, Self::Err> {
        self.files
            .get(path)
            .map(|b| b.to_owned())
            .ok_or_else(|| BootfsContentsError::NoSuchFile(path.to_owned()))
    }
}

impl PkgNamespace for BootfsContents {
    fn paths(&self) -> Vec<String> {
        self.files.keys().cloned().collect()
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
    #[error("`{_0}` does not exist in bootfs.")]
    NoSuchFile(String),
}
