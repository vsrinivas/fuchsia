// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Utilities for manipulating paths.

use std::path::{Path, PathBuf};

pub trait PathBufExt {
    /// Joins `self` with `path`, returning `self` unmodified if `path` is empty.
    ///
    /// Takes ownership of `self`.
    fn attach<P: AsRef<Path>>(self, path: P) -> PathBuf;
}

impl PathBufExt for PathBuf {
    fn attach<P: AsRef<Path>>(mut self, path: P) -> PathBuf {
        let path: &Path = path.as_ref();
        if path.components().count() > 0 {
            self.push(path)
        }
        self
    }
}
