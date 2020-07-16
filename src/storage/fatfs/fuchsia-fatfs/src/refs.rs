// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

///! This module provides abstractions over the fatfs Dir and File types,
///! erasing their lifetimes and allowing them to be kept without holding the filesystem lock.
use crate::{
    filesystem::FatFilesystemInner,
    types::{Dir, DirEntry, File},
};

pub struct FatfsDirRef {
    inner: Option<Dir<'static>>,
    dirent: Option<DirEntry<'static>>,
}

impl FatfsDirRef {
    /// Wraps and erases the lifetime. The caller assumes responsibility for
    /// ensuring the associated filesystem lives long enough and is pinned.
    pub unsafe fn from(entry: DirEntry<'_>) -> Self {
        let dir = entry.to_dir();
        FatfsDirRef {
            inner: Some(std::mem::transmute(dir)),
            dirent: Some(std::mem::transmute(entry)),
        }
    }

    /// Wraps and erases the lifetime. The caller assumes responsibility for
    /// ensuring the associated filesystem lives long enough and is pinned.
    /// This is used to encapsulate the root directory, as it doesn't have a corresponding
    /// DirEntry.
    pub unsafe fn from_root(dir: Dir<'_>) -> Self {
        FatfsDirRef { inner: Some(std::mem::transmute(dir)), dirent: None }
    }

    /// Extracts a reference to the wrapped value. The lifetime is restored to
    /// that of _fs.
    pub fn borrow<'a>(&'a self, _fs: &'a FatFilesystemInner) -> &'a Dir<'a> {
        self.inner.as_ref().unwrap()
    }

    /// Extracts a reference to the wrapped dirent. The lifetime is restored to that
    /// of _fs. Will return None if the contained directory is the root directory.
    pub fn borrow_entry<'a>(&'a self, _fs: &'a FatFilesystemInner) -> Option<&'a DirEntry<'a>> {
        self.dirent.as_ref()
    }

    /// Extracts the wrapped value, restoring its lifetime to that of _fs, and invalidate
    /// this FatfsDirRef. Any future calls to the borrow_*() functions will panic.
    pub fn take<'a>(&mut self, _fs: &'a FatFilesystemInner) -> Option<Dir<'a>> {
        self.dirent.take();
        self.inner.take()
    }
}

// Safe because whenever the `inner` is used, the filesystem lock is held.
unsafe impl Sync for FatfsDirRef {}
unsafe impl Send for FatfsDirRef {}

impl Drop for FatfsDirRef {
    fn drop(&mut self) {
        // Need to call take().
        assert!(self.inner.is_none());
        assert!(self.dirent.is_none());
    }
}

pub struct FatfsFileRef {
    inner: Option<File<'static>>,
    dirent: Option<DirEntry<'static>>,
}

impl FatfsFileRef {
    /// Wraps and erases the lifetime. The caller assumes responsibility for
    /// ensuring the associated filesystem lives long enough and is pinned.
    pub unsafe fn from(entry: DirEntry<'_>) -> Self {
        let file = entry.to_file();
        FatfsFileRef {
            inner: Some(std::mem::transmute(file)),
            dirent: Some(std::mem::transmute(entry)),
        }
    }

    /// Extracts a reference to the wrapped value. The lifetime is restored to
    /// that of _fs.
    pub fn borrow_mut<'a>(&'a mut self, _fs: &'a FatFilesystemInner) -> &'a mut File<'a> {
        // We need to transmute() back to the right lifetime because otherwise rust forces us to
        // return a &'static mut, because it thinks that any references within the file must be to
        // objects with a static lifetime. This isn't the case (because the lifetime is determined
        // by the lock on FatFilesystemInner, which we know is held), so this is safe.
        unsafe { std::mem::transmute(self.inner.as_mut().unwrap()) }
    }

    /// Extracts a reference to the wrapped dirent. The lifetime is restored to that
    /// of _fs.
    pub fn borrow_dirent<'a>(&'a self, _fs: &'a FatFilesystemInner) -> &'a DirEntry<'a> {
        self.dirent.as_ref().unwrap()
    }

    /// Extracts the wrapped value, restoring its lifetime to that of _fs, and invalidate
    /// this FatFsRef. Any future calls to the borrow_*() functions will panic.
    pub fn take<'a>(&mut self, _fs: &'a FatFilesystemInner) -> Option<File<'a>> {
        self.dirent.take();
        self.inner.take()
    }
}

// Safe because whenever the `inner` is used, the filesystem lock is held.
unsafe impl Sync for FatfsFileRef {}
unsafe impl Send for FatfsFileRef {}

impl Drop for FatfsFileRef {
    fn drop(&mut self) {
        // Need to call take().
        assert!(self.inner.is_none());
        assert!(self.dirent.is_none());
    }
}
