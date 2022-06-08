// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::BTreeMap;
use std::sync::Arc;

use super::*;
use crate::task::CurrentTask;
use crate::types::*;

/// A filesystem that will delegate most operation to a base one, but have a number of top level
/// directory that points to other filesystems.
pub struct LayeredFs {
    base_fs: FileSystemHandle,
    mappings: BTreeMap<FsString, FileSystemHandle>,
}

impl LayeredFs {
    /// Build a new filesystem.
    ///
    /// `base_fs`: The base file system that this file system will delegate to.
    /// `mappings`: The map of top level directory to filesystems that will be layered on top of
    /// `base_fs`.
    pub fn new(
        base_fs: FileSystemHandle,
        mappings: BTreeMap<FsString, FileSystemHandle>,
    ) -> FileSystemHandle {
        let layered_fs = Arc::new(LayeredFs { base_fs, mappings });
        let root_node = FsNode::new_root(layered_fs.clone());
        FileSystem::new_with_root(layered_fs.clone(), root_node)
    }
}

pub struct LayeredFsRootNodeOps {
    fs: Arc<LayeredFs>,
    root_file: FileHandle,
}

impl FileSystemOps for Arc<LayeredFs> {}

impl FsNodeOps for Arc<LayeredFs> {
    fn open(&self, _node: &FsNode, flags: OpenFlags) -> Result<Box<dyn FileOps>, Errno> {
        let root_node = &self.base_fs.root().node;
        Ok(Box::new(LayeredFsRootNodeOps {
            fs: self.clone(),
            root_file: root_node.open_anonymous(flags)?,
        }))
    }

    fn lookup(&self, _node: &FsNode, name: &FsStr) -> Result<Arc<FsNode>, Errno> {
        if let Some(fs) = self.mappings.get(name) {
            Ok(fs.root().node.clone())
        } else {
            self.base_fs.root().node.lookup(name)
        }
    }
}

impl FileOps for LayeredFsRootNodeOps {
    fileops_impl_directory!();

    fn seek(
        &self,
        file: &FileObject,
        current_task: &CurrentTask,
        offset: off_t,
        whence: SeekOrigin,
    ) -> Result<off_t, Errno> {
        let new_offset = file.unbounded_seek(offset, whence)?;
        if new_offset >= self.fs.mappings.len() as off_t {
            self.root_file.seek(
                current_task,
                new_offset - self.fs.mappings.len() as off_t,
                SeekOrigin::SET,
            )?;
        }
        Ok(new_offset)
    }

    fn readdir(
        &self,
        file: &FileObject,
        current_task: &CurrentTask,
        sink: &mut dyn DirentSink,
    ) -> Result<(), Errno> {
        let mut current_offset = file.offset.lock();

        for (key, fs) in self.fs.mappings.iter().skip(*current_offset as usize) {
            sink.add(fs.root().node.inode_num, *current_offset + 1, DirectoryEntryType::DIR, key)?;
            *current_offset += 1;
        }

        struct DirentSinkWrapper<'a> {
            sink: &'a mut dyn DirentSink,
            offset: &'a mut off_t,
            mappings_count: off_t,
        }

        impl<'a> DirentSink for DirentSinkWrapper<'a> {
            fn add(
                &mut self,
                inode_num: ino_t,
                offset: off_t,
                entry_type: DirectoryEntryType,
                name: &FsStr,
            ) -> Result<(), Errno> {
                self.sink.add(inode_num, offset + self.mappings_count, entry_type, name)?;
                *self.offset = offset + self.mappings_count;
                Ok(())
            }
            fn actual(&self) -> usize {
                self.sink.actual()
            }
        }

        let mut wrapper = DirentSinkWrapper {
            sink,
            offset: &mut current_offset,
            mappings_count: self.fs.mappings.len() as off_t,
        };

        self.root_file.readdir(current_task, &mut wrapper)
    }
}
