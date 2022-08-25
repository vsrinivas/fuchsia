// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::BTreeMap;
use std::sync::Arc;

use super::*;
use crate::task::*;
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
        kernel: &Kernel,
        base_fs: FileSystemHandle,
        mappings: BTreeMap<FsString, FileSystemHandle>,
    ) -> FileSystemHandle {
        let layered_fs = Arc::new(LayeredFs { base_fs, mappings });
        let root_node = FsNode::new_root(layered_fs.clone());
        FileSystem::new_with_root(kernel, layered_fs, root_node)
    }
}

pub struct LayeredFsRootNodeOps {
    fs: Arc<LayeredFs>,
    root_file: FileHandle,
}

impl FileSystemOps for Arc<LayeredFs> {}

impl FsNodeOps for Arc<LayeredFs> {
    fn create_file_ops(&self, _node: &FsNode, flags: OpenFlags) -> Result<Box<dyn FileOps>, Errno> {
        Ok(Box::new(LayeredFsRootNodeOps {
            fs: self.clone(),
            root_file: self.base_fs.root().open_anonymous(flags)?,
        }))
    }

    fn lookup(
        &self,
        _node: &FsNode,
        current_task: &CurrentTask,
        name: &FsStr,
    ) -> Result<Arc<FsNode>, Errno> {
        if let Some(fs) = self.mappings.get(name) {
            Ok(fs.root().node.clone())
        } else {
            self.base_fs.root().node.lookup(current_task, name)
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
        _file: &FileObject,
        current_task: &CurrentTask,
        sink: &mut dyn DirentSink,
    ) -> Result<(), Errno> {
        for (key, fs) in self.fs.mappings.iter().skip(sink.offset() as usize) {
            sink.add(fs.root().node.inode_num, sink.offset() + 1, DirectoryEntryType::DIR, key)?;
        }

        struct DirentSinkWrapper<'a> {
            sink: &'a mut dyn DirentSink,
            mappings: &'a BTreeMap<FsString, FileSystemHandle>,
            offset: &'a mut off_t,
        }

        impl<'a> DirentSink for DirentSinkWrapper<'a> {
            fn add(
                &mut self,
                inode_num: ino_t,
                offset: off_t,
                entry_type: DirectoryEntryType,
                name: &FsStr,
            ) -> Result<(), Errno> {
                if !self.mappings.contains_key(name) {
                    self.sink.add(
                        inode_num,
                        offset + (self.mappings.len() as off_t),
                        entry_type,
                        name,
                    )?;
                }
                *self.offset = offset;
                Ok(())
            }
            fn offset(&self) -> off_t {
                *self.offset
            }
            fn actual(&self) -> usize {
                self.sink.actual()
            }
        }

        let mut root_file_offset = self.root_file.offset.lock();
        let mut wrapper =
            DirentSinkWrapper { sink, mappings: &self.fs.mappings, offset: &mut root_file_offset };

        self.root_file.readdir(current_task, &mut wrapper)
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::fs::tmpfs::TmpFs;
    use crate::testing::*;

    fn get_root_entry_names(current_task: &CurrentTask, fs: &FileSystem) -> Vec<Vec<u8>> {
        struct DirentNameCapturer {
            pub names: Vec<Vec<u8>>,
            offset: off_t,
        }
        impl DirentSink for DirentNameCapturer {
            fn add(
                &mut self,
                _inode_num: ino_t,
                offset: off_t,
                _entry_type: DirectoryEntryType,
                name: &FsStr,
            ) -> Result<(), Errno> {
                self.names.push(name.to_vec());
                self.offset = offset;
                Ok(())
            }
            fn offset(&self) -> off_t {
                self.offset
            }
            fn actual(&self) -> usize {
                self.offset as usize
            }
        }
        let mut sink = DirentNameCapturer { names: vec![], offset: 0 };
        fs.root()
            .open_anonymous(OpenFlags::RDONLY)
            .expect("open")
            .readdir(current_task, &mut sink)
            .expect("readdir");
        std::mem::take(&mut sink.names)
    }

    #[::fuchsia::test]
    fn test_remove_duplicates() {
        let (kernel, current_task) = create_kernel_and_task();
        let base = TmpFs::new(&kernel);
        base.root().create_dir(&current_task, b"d1").expect("create_dir");
        base.root().create_dir(&current_task, b"d2").expect("create_dir");
        let base_entries = get_root_entry_names(&current_task, &base);
        assert_eq!(base_entries.len(), 4);
        assert!(base_entries.contains(&b".".to_vec()));
        assert!(base_entries.contains(&b"..".to_vec()));
        assert!(base_entries.contains(&b"d1".to_vec()));
        assert!(base_entries.contains(&b"d2".to_vec()));

        let layered_fs = LayeredFs::new(
            &kernel,
            base,
            BTreeMap::from([
                (b"d1".to_vec(), TmpFs::new(&kernel)),
                (b"d3".to_vec(), TmpFs::new(&kernel)),
            ]),
        );
        let layered_fs_entries = get_root_entry_names(&current_task, &layered_fs);
        assert_eq!(layered_fs_entries.len(), 5);
        assert!(layered_fs_entries.contains(&b".".to_vec()));
        assert!(layered_fs_entries.contains(&b"..".to_vec()));
        assert!(layered_fs_entries.contains(&b"d1".to_vec()));
        assert!(layered_fs_entries.contains(&b"d2".to_vec()));
        assert!(layered_fs_entries.contains(&b"d3".to_vec()));
    }
}
