// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This is a shim for `std::fs`, with both real and mock implementations.

use std::{io, path::Path};

/// A shim trait between these structs, and `std::fs` operations, so that file
/// operations can be more easily handled in tests, without writing to temp files
pub trait FilesystemProvider {
    /// Shim for `std::fs::read()`
    fn read<P: AsRef<Path>>(&self, path: P) -> io::Result<Vec<u8>>;

    /// Shim for `std::fs::read_to_string()`
    fn read_to_string<P: AsRef<Path>>(&self, path: P) -> io::Result<String>;

    /// Shim for 'std::fs::write()`
    fn write<P: AsRef<Path>, C: AsRef<[u8]>>(&self, path: P, contents: C) -> io::Result<()>;
}

/// The "real" filesystem.
pub(crate) struct RealFilesystemProvider;
impl FilesystemProvider for RealFilesystemProvider {
    /// Shim for `std::fs::read()`
    fn read<P: AsRef<Path>>(&self, path: P) -> io::Result<Vec<u8>> {
        std::fs::read(path)
    }

    /// Shim for `std::fs::read_to_string()`
    fn read_to_string<P: AsRef<Path>>(&self, path: P) -> io::Result<String> {
        std::fs::read_to_string(path)
    }

    /// Shim for 'std::fs::write()`
    fn write<P: AsRef<Path>, C: AsRef<[u8]>>(&self, path: P, contents: C) -> io::Result<()> {
        std::fs::write(path, contents)
    }
}

#[cfg(test)]
pub(crate) mod mock {
    use super::FilesystemProvider;
    use std::{
        cell::RefCell,
        collections::HashMap,
        io,
        path::{Path, PathBuf},
        rc::Rc,
    };

    /// A mocked filesystem
    pub(crate) struct MockFilesystemProvider {
        /// All of the files in the filesystem.
        files: Rc<RefCell<HashMap<PathBuf, Vec<u8>>>>,
    }

    impl MockFilesystemProvider {
        pub fn new() -> Self {
            Self { files: Rc::new(RefCell::new(HashMap::new())) }
        }

        /// Add a file that can be read to the mocked filesystem.
        pub fn add<P: AsRef<Path>>(&mut self, path: P, contents: &[u8]) {
            self.files.borrow_mut().insert(path.as_ref().to_path_buf(), Vec::from(contents));
        }
    }

    impl FilesystemProvider for MockFilesystemProvider {
        /// Implementation of `std::fs::read()`.
        fn read<P: AsRef<Path>>(&self, path: P) -> io::Result<Vec<u8>> {
            self.files
                .borrow()
                .get(path.as_ref())
                .map(Clone::clone)
                .ok_or(io::Error::new(io::ErrorKind::NotFound, path.as_ref().to_string_lossy()))
        }

        /// Shim for `std::fs::read_to_string()`
        fn read_to_string<P: AsRef<Path>>(&self, path: P) -> io::Result<String> {
            if let Some(contents) = self.files.borrow().get(path.as_ref()) {
                String::from_utf8(contents.clone()).map_err(|_| {
                    io::Error::new(io::ErrorKind::InvalidData, path.as_ref().to_string_lossy())
                })
            } else {
                Err(io::Error::new(io::ErrorKind::NotFound, path.as_ref().to_string_lossy()))
            }
        }

        /// Shim for 'std::fs::write()`
        fn write<P: AsRef<Path>, C: AsRef<[u8]>>(&self, path: P, contents: C) -> io::Result<()> {
            self.files
                .borrow_mut()
                .insert(path.as_ref().to_path_buf(), Vec::from(contents.as_ref()));
            Ok(())
        }
    }
}
