// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Arc;

use once_cell::sync::OnceCell;

use super::*;
use crate::types::*;

/// A file system that can be mounted in a namespace.
pub struct FileSystem {
    root: OnceCell<DirEntryHandle>,
    next_inode: AtomicU64,
    ops: Box<dyn FileSystemOps>,
}

impl FileSystem {
    pub fn new(
        ops: impl FileSystemOps + 'static,
        root_ops: impl FsNodeOps + 'static,
    ) -> FileSystemHandle {
        // TODO(tbodt): I would like to use Arc::new_cyclic
        let fs = Self::new_no_root(ops);
        let root = DirEntry::new_root(root_ops, &fs);
        if fs.root.set(root).is_err() {
            panic!("there's no way fs.root could have been set");
        }
        fs
    }

    pub fn new_no_root(ops: impl FileSystemOps + 'static) -> FileSystemHandle {
        Arc::new(FileSystem {
            root: OnceCell::new(),
            next_inode: AtomicU64::new(0),
            ops: Box::new(ops),
        })
    }

    pub fn root(&self) -> &DirEntryHandle {
        self.root.get().unwrap()
    }

    pub fn next_inode_num(&self) -> ino_t {
        self.next_inode.fetch_add(1, Ordering::Relaxed)
    }

    pub fn did_create_dir_entry(&self, entry: &DirEntryHandle) {
        self.ops.did_create_dir_entry(self, entry);
    }

    pub fn will_destroy_dir_entry(&self, entry: &DirEntryHandle) {
        self.ops.will_destroy_dir_entry(self, entry);
    }
}

/// The filesystem-implementation-specific data for FileSystem.
pub trait FileSystemOps: Send + Sync {
    fn did_create_dir_entry(&self, _fs: &FileSystem, _entry: &DirEntryHandle) {}
    fn will_destroy_dir_entry(&self, _fs: &FileSystem, _entry: &DirEntryHandle) {}
}

pub type FileSystemHandle = Arc<FileSystem>;
