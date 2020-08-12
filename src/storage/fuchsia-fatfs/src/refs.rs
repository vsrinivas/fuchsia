// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

///! This module provides abstractions over the fatfs Dir and File types,
///! erasing their lifetimes and allowing them to be kept without holding the filesystem lock.
use {
    crate::{
        directory::FatDirectory,
        filesystem::FatFilesystemInner,
        node::Node,
        types::{Dir, File},
    },
    fuchsia_zircon::Status,
    scopeguard::defer,
    std::sync::Arc,
};

pub struct FatfsDirRef {
    inner: Option<Dir<'static>>,
    open_count: usize,
}

impl FatfsDirRef {
    /// Wraps and erases the lifetime. The caller assumes responsibility for
    /// ensuring the associated filesystem lives long enough and is pinned.
    pub unsafe fn from(dir: Dir<'_>) -> Self {
        FatfsDirRef { inner: Some(std::mem::transmute(dir)), open_count: 1 }
    }

    pub fn empty() -> Self {
        FatfsDirRef { inner: None, open_count: 0 }
    }

    /// Extracts a reference to the wrapped value. The lifetime is restored to
    /// that of _fs.
    pub fn borrow<'a>(&'a self, _fs: &'a FatFilesystemInner) -> Option<&'a Dir<'a>> {
        unsafe { std::mem::transmute(self.inner.as_ref()) }
    }

    /// Extracts a mutable reference to the wrapped value. The lifetime is restored to
    /// that of _fs.
    pub fn borrow_mut<'a>(&'a mut self, _fs: &'a FatFilesystemInner) -> Option<&'a mut Dir<'a>> {
        // We need to transmute() back to the right lifetime because otherwise rust forces us to
        // return a &'static mut, because it thinks that any references within the file must be to
        // objects with a static lifetime. This isn't the case (because the lifetime is determined
        // by the lock on FatFilesystemInner, which we know is held), so this is safe.
        unsafe { std::mem::transmute(self.inner.as_mut()) }
    }

    /// Reopen the FatfsDirRef, without affecting the open_count.
    pub unsafe fn reopen(
        &mut self,
        fs: &FatFilesystemInner,
        parent: Option<&Arc<FatDirectory>>,
        name: &str,
    ) -> Result<(), Status> {
        let dir = if let Some(parent) = parent {
            parent.open_ref(fs)?;
            defer! { parent.close_ref(fs) }
            parent.find_child(fs, name)?.ok_or(Status::NOT_FOUND)?.to_dir()
        } else {
            fs.root_dir()
        };
        self.inner.replace(std::mem::transmute(dir));
        Ok(())
    }

    /// Open the FatfsDirRef, incrementing the open count.
    pub unsafe fn open(
        &mut self,
        fs: &FatFilesystemInner,
        parent: Option<&Arc<FatDirectory>>,
        name: &str,
    ) -> Result<(), Status> {
        if self.open_count == 0 {
            self.reopen(fs, parent, name)?;
        }
        if self.open_count == std::usize::MAX {
            Err(Status::BAD_HANDLE)
        } else {
            self.open_count += 1;
            Ok(())
        }
    }

    /// Close the FatfsDirRef, dropping the underlying Dir if the open count reaches zero.
    pub fn close(&mut self, fs: &FatFilesystemInner) {
        assert!(self.open_count > 0);
        self.open_count -= 1;
        if self.open_count == 0 {
            self.take(&fs);
        }
    }

    /// Extracts the wrapped value, restoring its lifetime to that of _fs, and invalidate
    /// this FatfsDirRef. Any future calls to the borrow_*() functions will panic.
    pub fn take<'a>(&mut self, _fs: &'a FatFilesystemInner) -> Option<Dir<'a>> {
        unsafe { std::mem::transmute(self.inner.take()) }
    }
}

// Safe because whenever the `inner` is used, the filesystem lock is held.
unsafe impl Sync for FatfsDirRef {}
unsafe impl Send for FatfsDirRef {}

impl Drop for FatfsDirRef {
    fn drop(&mut self) {
        assert_eq!(self.open_count, 0);
        // Need to call take().
        assert!(self.inner.is_none());
    }
}

pub struct FatfsFileRef {
    inner: Option<File<'static>>,
    open_count: usize,
}

impl FatfsFileRef {
    /// Wraps and erases the lifetime. The caller assumes responsibility for
    /// ensuring the associated filesystem lives long enough and is pinned.
    pub unsafe fn from(file: File<'_>) -> Self {
        FatfsFileRef { inner: Some(std::mem::transmute(file)), open_count: 1 }
    }

    /// Extracts a mutable reference to the wrapped value. The lifetime is restored to
    /// that of _fs.
    pub fn borrow_mut<'a>(&'a mut self, _fs: &'a FatFilesystemInner) -> Option<&'a mut File<'a>> {
        // We need to transmute() back to the right lifetime because otherwise rust forces us to
        // return a &'static mut, because it thinks that any references within the file must be to
        // objects with a static lifetime. This isn't the case (because the lifetime is determined
        // by the lock on FatFilesystemInner, which we know is held), so this is safe.
        unsafe { std::mem::transmute(self.inner.as_mut()) }
    }

    /// Extracts a reference to the wrapped value. The lifetime is restored to that
    /// of _fs.
    pub fn borrow<'a>(&'a self, _fs: &'a FatFilesystemInner) -> Option<&'a File<'a>> {
        self.inner.as_ref()
    }

    pub unsafe fn reopen(
        &mut self,
        fs: &FatFilesystemInner,
        parent: &FatDirectory,
        name: &str,
    ) -> Result<(), Status> {
        if self.open_count == 0 {
            // No need to reopen if the open count is zero.
            return Ok(());
        }
        let file = parent.find_child(fs, name)?.ok_or(Status::NOT_FOUND)?.to_file();
        self.inner.replace(std::mem::transmute(file));
        Ok(())
    }

    pub unsafe fn open(
        &mut self,
        fs: &FatFilesystemInner,
        parent: Option<&FatDirectory>,
        name: &str,
    ) -> Result<(), Status> {
        if self.open_count == 0 {
            self.reopen(fs, parent.ok_or(Status::BAD_HANDLE)?, name)?;
        }
        if self.open_count == std::usize::MAX {
            Err(Status::UNAVAILABLE)
        } else {
            self.open_count += 1;
            Ok(())
        }
    }

    pub fn close(&mut self, fs: &FatFilesystemInner) {
        assert!(self.open_count > 0);
        self.open_count -= 1;
        if self.open_count == 0 {
            self.take(&fs);
        }
    }

    /// Extracts the wrapped value, restoring its lifetime to that of _fs, and invalidate
    /// this FatFsRef. Any future calls to the borrow_*() functions will panic.
    pub fn take<'a>(&mut self, _fs: &'a FatFilesystemInner) -> Option<File<'a>> {
        self.inner.take()
    }
}

// Safe because whenever the `inner` is used, the filesystem lock is held.
unsafe impl Sync for FatfsFileRef {}
unsafe impl Send for FatfsFileRef {}

impl Drop for FatfsFileRef {
    fn drop(&mut self) {
        // Need to call take().
        assert_eq!(self.open_count, 0);
        assert!(self.inner.is_none());
    }
}
