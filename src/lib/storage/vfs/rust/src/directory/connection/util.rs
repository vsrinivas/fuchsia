// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::directory::entry_container::Directory,
    fuchsia_zircon::Status,
    std::{ops::Deref, sync::Arc},
};

/// This struct is a RAII wrapper around a directory that will call close() on it unless the
/// succeed() function is called.
pub struct OpenDirectory<T: Directory + ?Sized> {
    directory: Option<Arc<T>>,
}

impl<T: Directory + ?Sized> OpenDirectory<T> {
    pub fn new(dir: Arc<T>) -> Self {
        Self { directory: Some(dir) }
    }

    /// Explicitly close the directory.
    pub fn close(&mut self) -> Result<(), Status> {
        let dir = self.directory.take().unwrap();
        dir.close()
    }
}

impl<T: Directory + ?Sized> Drop for OpenDirectory<T> {
    fn drop(&mut self) {
        if let Some(dir) = self.directory.take() {
            let _ = dir.close();
        }
    }
}

impl<T: Directory + ?Sized> Deref for OpenDirectory<T> {
    type Target = Arc<T>;

    fn deref(&self) -> &Self::Target {
        self.directory.as_ref().unwrap()
    }
}
