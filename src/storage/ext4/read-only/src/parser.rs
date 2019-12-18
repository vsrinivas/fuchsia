/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012, 2010 Zheng Liu <lz@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        readers::Reader,
        structs::{
            BlockGroupDesc32, DirEntry2, EntryType, Extent, ExtentHeader, INode, ParseToStruct,
            ParsingError, SuperBlock, FIRST_BG_PADDING, ROOT_INODE_NUM,
        },
    },
    fuchsia_vfs_pseudo_fs_mt::{
        directory::immutable, file::pcb::asynchronous::read_only_const, tree_builder::TreeBuilder,
    },
    std::{
        mem::size_of,
        path::{Component, Path},
        str,
        sync::Arc,
    },
};

// Assuming/ensuring that we are on a 64bit system where u64 == usize.
assert_eq_size!(check_usize_is_u64; u64, usize);

pub struct Parser<T: Reader> {
    reader: Arc<T>,
    super_block: Option<Arc<SuperBlock>>,
}

/// EXT4 Parser
///
/// Takes in a `Reader` that is able to read arbitrary chunks of data from the filesystem image.
///
/// Basic use:
/// let mut parser = Parser::new(VecReader::new(vec_of_u8));
/// let tree = parser.build_fuchsia_tree()
impl<T: 'static + Reader> Parser<T> {
    pub fn new(reader: T) -> Self {
        Parser { reader: Arc::new(reader), super_block: None }
    }

    /// Returns the Super Block.
    ///
    /// If the super block has been parsed and saved before, return that.
    /// Else, parse the super block and save it and return it.
    ///
    /// We never need to re-parse the super block in this read-only
    /// implementation.
    fn super_block(&mut self) -> Result<Arc<SuperBlock>, ParsingError> {
        // Neither Option helper works here:
        // - `get_or_insert` will still call `SuperBlock::parse` first, and
        // thus call every time.
        // - `get_or_insert_with` will not be able to propagate a ParsingError
        // back to here.

        // Writing our own logic instead.
        match &self.super_block {
            Some(val) => Ok(val.clone()),
            None => {
                let sb = SuperBlock::parse(self.reader.clone())?;
                self.super_block = Some(sb.clone());
                Ok(sb)
            }
        }
    }

    /// Reads block size from the Super Block.
    fn block_size(&mut self) -> Result<usize, ParsingError> {
        self.super_block()?.block_size()
    }

    /// Reads full raw data from a given block number.
    fn block(&mut self, block_number: u64) -> Result<Box<[u8]>, ParsingError> {
        //TODO(vfcc): Better error handling.
        let block_size = self.block_size()?;
        let address = (block_number as usize)
            .checked_mul(block_size)
            .ok_or(ParsingError::BlockNumberOutOfBounds(block_number))?;

        let mut data = vec![0u8; block_size];

        self.reader.read(address, data.as_mut_slice()).map_err(Into::<ParsingError>::into)?;

        Ok(data.into_boxed_slice())
    }

    /// Reads the INode at the given inode number.
    fn inode(&mut self, inode_number: u32) -> Result<Arc<INode>, ParsingError> {
        if inode_number < 1 {
            // INode number 0 is not allowed per ext4 spec.
            return Err(ParsingError::ParseINode(inode_number));
        }
        //TODO(vfcc): Check calculation bounds.
        let sb = self.super_block()?;
        let inode_table_offset = (inode_number - 1) as usize % sb.e2fs_ipg.get() as usize
            * sb.e2fs_inode_size.get() as usize;
        let block_size = self.block_size()?;

        // The first Block Group starts with:
        // - 1024 byte padding
        // - 1024 byte Super Block
        // Then in the next block, there are many blocks worth of Block Group Descriptors.
        // If the block size is 2048 bytes or larger, then the 1024 byte padding, and the
        // Super Block both fit in the first block (0), and the Block Group Descriptors start
        // at block 1.
        //
        // A 1024 byte block size means the padding takes block 0 and the Super Block takes
        // block 1. This means the Block Group Descriptors start in block 2.
        let mut bg_descriptor_offset = block_size;
        let size_of_start_of_fs = FIRST_BG_PADDING + size_of::<SuperBlock>();
        if block_size <= size_of_start_of_fs {
            bg_descriptor_offset = block_size * 2;
        }

        // TODO(vfcc): Only checking the first block group.
        // There are potentially N block groups.
        let bgd = BlockGroupDesc32::parse_offset(
            self.reader.clone(),
            bg_descriptor_offset,
            ParsingError::ParseBlockGroupDesc(block_size),
        )?;

        let inode_addr = (bgd.ext2bgd_i_tables.get() as usize * block_size) + inode_table_offset;

        INode::parse_offset(self.reader.clone(), inode_addr, ParsingError::ParseINode(inode_number))
    }

    /// Helper function to get the root directory INode.
    fn root_inode(&mut self) -> Result<Arc<INode>, ParsingError> {
        self.inode(ROOT_INODE_NUM)
    }

    /// Read all raw data from a given extent leaf node.
    fn extent_data(
        &mut self,
        extent: &Extent,
        mut allowance: usize,
    ) -> Result<Vec<u8>, ParsingError> {
        let block_number = extent.target_block_num();
        let block_count = extent.e_len.get();
        let block_size = self.block_size()?;
        let mut read_len;

        let mut data = Vec::with_capacity(block_size * block_count as usize);

        for i in 0..block_count {
            let block_data = self.block(block_number + i as u64)?;
            if allowance >= block_size {
                read_len = block_size;
            } else {
                read_len = allowance;
            }
            let block_data = &block_data[0..read_len];
            data.append(&mut block_data.to_vec());
            allowance -= read_len;
        }

        Ok(data)
    }

    /// List of directory entries from the directory that is the given Inode.
    ///
    /// Errors if the Inode does not map to a Directory.
    ///
    /// Currently only supports a single block of DirEntrys, with only one extent entry.
    fn entries_from_inode(
        &mut self,
        inode: Arc<INode>,
    ) -> Result<Vec<Arc<DirEntry2>>, ParsingError> {
        let root_extent = inode.root_extent_header()?;
        // TODO(vfcc): Will use entry_count when we support having N entries.
        let block_size = self.block_size()?;

        let mut entries = Vec::new();

        match root_extent.eh_depth.get() {
            0 => {
                let offset = size_of::<ExtentHeader>();
                let e = Extent::to_struct_ref(
                    &(inode.e2di_blocks)[offset..offset * 2],
                    ParsingError::ParseExtent(offset),
                )?;

                let mut index = 0usize;
                let start_index = e.target_block_num() as usize * block_size;

                // The `e2d_reclen` of the last entry will be large enough be approximately the
                // end of the block.
                while (index + size_of::<DirEntry2>()) < block_size {
                    let offset = index + start_index;
                    let de = DirEntry2::parse_offset(
                        self.reader.clone(),
                        offset,
                        ParsingError::InvalidDirEntry2(offset),
                    )?;
                    index += de.e2d_reclen.get() as usize;
                    entries.push(de);
                }
                Ok(entries)
            }
            _ => {
                //TODO(vfcc): Support nested extent nodes.
                return Err(ParsingError::Incompatible(
                    "Nested extents are not supported".to_string(),
                ));
            }
        }
    }

    /// Get any DirEntry2 that isn't root.
    ///
    /// Root doesn't have a DirEntry2.
    ///
    /// When dynamic loading of files is supported, this is the required mechanism.
    pub fn entry_at_path(&mut self, path: &Path) -> Result<Arc<DirEntry2>, ParsingError> {
        let root_inode = self.root_inode()?;
        let root_entries = self.entries_from_inode(root_inode)?;
        let mut entry_map = DirEntry2::as_hash_map(root_entries)?;

        let mut components = path.components().peekable();
        let mut component = components.next();

        while component != None {
            match component {
                Some(Component::RootDir) => {
                    // Skip
                }
                Some(Component::Normal(name)) => {
                    let name = name.to_str().ok_or(ParsingError::InvalidInputPath)?;
                    if let Some(entry) = entry_map.remove(name) {
                        if components.peek() == None {
                            return Ok(entry);
                        }
                        match EntryType::from_u8(entry.e2d_type)? {
                            EntryType::Directory => {
                                let inode = self.inode(entry.e2d_ino.get())?;
                                entry_map =
                                    DirEntry2::as_hash_map(self.entries_from_inode(inode)?)?;
                            }
                            _ => {
                                break;
                            }
                        }
                    }
                }
                _ => {
                    break;
                }
            }
            component = components.next();
        }

        match path.to_str() {
            Some(s) => Err(ParsingError::PathNotFound(s.to_string())),
            None => Err(ParsingError::PathNotFound(
                "Bad path - was not able to convert into string".to_string(),
            )),
        }
    }

    /// Read all raw data for a given file (directory entry).
    ///
    /// Currently does not support extent trees, only basic "flat" trees.
    pub fn read_file(&mut self, entry: Arc<DirEntry2>) -> Result<Vec<u8>, ParsingError> {
        let inode = self.inode(entry.e2d_ino.get())?;
        let root_extent = inode.root_extent_header()?;
        let entry_count = root_extent.eh_ecount.get();
        let mut size_remaining = inode.size();

        let mut data = Vec::with_capacity(inode.size());

        match root_extent.eh_depth.get() {
            0 => {
                for i in 0..entry_count {
                    let offset = size_of::<ExtentHeader>() + (size_of::<Extent>() * i as usize);
                    let e = Extent::to_struct_ref(
                        // TODO(vfcc): Bounds check this and the other location.
                        &(inode.e2di_blocks)[offset..offset + size_of::<Extent>()],
                        ParsingError::ParseExtent(offset),
                    )?;

                    let mut extent_data = self.extent_data(e, size_remaining)?;
                    let extent_len = extent_data.len();
                    if extent_len > size_remaining {
                        return Err(ParsingError::ExtentUnexpectedLength(
                            extent_len,
                            size_remaining,
                        ));
                    }
                    size_remaining -= extent_data.len();
                    data.append(&mut extent_data);
                }
            }
            _ => {
                //TODO(vfcc): Support nested extent nodes.
                return Err(ParsingError::Incompatible(
                    "Nested extents are not supported".to_string(),
                ));
            }
        };

        Ok(data)
    }

    /// Progress through the entire directory tree starting from the given INode.
    ///
    /// If given the root directory INode, this will process through every directory entry in the
    /// filesystem in a DFS manner.
    ///
    /// Takes in a closure that will be called for each entry found.
    /// Closure should return `Ok(true)` in order to continue the process, otherwise the process
    /// will stop.
    ///
    /// Returns Ok(true) if it has indexed its subtree successfully. Otherwise, if the receiver
    /// chooses to cancel indexing early, an Ok(false) is returned and propagated up.
    pub fn index<R>(
        &mut self,
        inode: Arc<INode>,
        prefix: Vec<&str>,
        receiver: &mut R,
    ) -> Result<bool, ParsingError>
    where
        R: FnMut(&mut Parser<T>, Vec<&str>, Arc<DirEntry2>) -> Result<bool, ParsingError>,
    {
        let entries = self.entries_from_inode(inode)?;
        for entry in entries {
            let entry_name = entry.name()?;
            if entry_name == "." || entry_name == ".." {
                continue;
            }
            let mut name = Vec::new();
            name.append(&mut prefix.clone());
            name.push(entry_name);
            if !receiver(self, name.clone(), entry.clone())? {
                return Ok(false);
            }
            if EntryType::from_u8(entry.e2d_type)? == EntryType::Directory {
                let inode = self.inode(entry.e2d_ino.get())?;
                if !self.index(inode, name, receiver)? {
                    return Ok(false);
                }
            }
        }

        Ok(true)
    }

    /// Returns a `Simple` filesystem as built by `TreeBuilder.build()`.
    pub fn build_fuchsia_tree(&mut self) -> Result<Arc<immutable::Simple>, ParsingError> {
        let root_inode = self.root_inode()?;
        let mut tree = TreeBuilder::empty_dir();

        self.index(root_inode, Vec::new(), &mut |my_self, path, entry| {
            let entry_type = EntryType::from_u8(entry.e2d_type)?;
            match entry_type {
                EntryType::RegularFile => {
                    let data = my_self.read_file(entry)?;
                    tree.add_entry(path.clone(), read_only_const(data.clone()))
                        .map_err(|_| ParsingError::BadFile(path.join("/")))?;
                }
                EntryType::Directory => {
                    tree.add_empty_dir(path.clone())
                        .map_err(|_| ParsingError::BadDirectory(path.join("/")))?;
                }
                _ => {
                    // TODO(vfcc): Handle other types.
                }
            }
            Ok(true)
        })?;

        Ok(tree.build())
    }
}

#[cfg(test)]
mod tests {
    use {
        crate::{
            readers::VecReader,
            structs::{EntryType, SB_MAGIC},
        },
        crypto::{digest::Digest, sha2::Sha256},
        std::{collections::HashSet, fs, path::Path, str},
    };

    #[test]
    fn list_root_1_file() {
        let data = fs::read("/pkg/data/1file.img").expect("Unable to read file");
        let mut parser = super::Parser::new(VecReader::new(data));
        assert_eq!(parser.super_block().expect("Super Block").e2fs_magic.get(), SB_MAGIC);
        let root_inode = parser.root_inode().expect("Parse INode");
        let entries = parser.entries_from_inode(root_inode).expect("List entries");
        let mut expected_entries = vec!["file1", "lost+found", "..", "."];

        for de in &entries {
            assert_eq!(expected_entries.pop().unwrap(), de.name().unwrap());
        }
        assert_eq!(expected_entries.len(), 0);
    }

    #[test]
    fn list_root() {
        let data = fs::read("/pkg/data/nest.img").expect("Unable to read file");
        let mut parser = super::Parser::new(VecReader::new(data));
        assert_eq!(parser.super_block().expect("Super Block").e2fs_magic.get(), SB_MAGIC);
        let root_inode = parser.root_inode().expect("Parse INode");
        let entries = parser.entries_from_inode(root_inode).expect("List entries");
        let mut expected_entries = vec!["inner", "file1", "lost+found", "..", "."];

        for de in &entries {
            assert_eq!(expected_entries.pop().unwrap(), de.name().unwrap());
        }
        assert_eq!(expected_entries.len(), 0);
    }

    #[test]
    fn get_from_path() {
        let data = fs::read("/pkg/data/nest.img").expect("Unable to read file");
        let mut parser = super::Parser::new(VecReader::new(data));
        assert_eq!(parser.super_block().expect("Super Block").e2fs_magic.get(), SB_MAGIC);

        let entry = parser.entry_at_path(Path::new("/inner")).expect("Entry at path");
        assert_eq!(entry.e2d_ino.get(), 12);
        assert_eq!(entry.name().unwrap(), "inner");

        let entry = parser.entry_at_path(Path::new("/inner/file2")).expect("Entry at path");
        assert_eq!(entry.e2d_ino.get(), 17);
        assert_eq!(entry.name().unwrap(), "file2");
    }

    #[test]
    fn read_file() {
        let data = fs::read("/pkg/data/1file.img").expect("Unable to read file");
        let mut parser = super::Parser::new(VecReader::new(data));
        assert_eq!(parser.super_block().expect("Super Block").e2fs_magic.get(), SB_MAGIC);

        let entry = parser.entry_at_path(Path::new("file1")).expect("Entry at path");
        assert_eq!(entry.e2d_ino.get(), 15);
        assert_eq!(entry.name().unwrap(), "file1");

        let data = parser.read_file(entry).expect("File data");
        let compare = "file1 contents.\n";
        assert_eq!(data.len(), compare.len());
        assert_eq!(str::from_utf8(data.as_slice()).expect("File data"), compare);
    }

    #[test]
    fn fail_inode_zero() {
        let data = fs::read("/pkg/data/1file.img").expect("Unable to read file");
        let mut parser = super::Parser::new(VecReader::new(data));
        assert!(parser.inode(0).is_err());
    }

    #[test]
    fn index() {
        let data = fs::read("/pkg/data/nest.img").expect("Unable to read file");
        let mut parser = super::Parser::new(VecReader::new(data));
        assert_eq!(parser.super_block().expect("Super Block").e2fs_magic.get(), SB_MAGIC);

        let mut count = 0;
        let mut entries: HashSet<u32> = HashSet::new();
        let root_inode = parser.root_inode().expect("Root inode");

        parser
            .index(root_inode, Vec::new(), &mut |_, _, entry| {
                count += 1;

                // Make sure each inode only appears once.
                assert_ne!(entries.contains(&entry.e2d_ino.get()), true);
                entries.insert(entry.e2d_ino.get());

                Ok(true)
            })
            .expect("Index");

        assert_eq!(count, 4);
    }

    #[test]
    fn check_data() {
        let data = fs::read("/pkg/data/nest.img").expect("Unable to read file");
        let mut parser = super::Parser::new(VecReader::new(data));
        assert_eq!(parser.super_block().expect("Super Block").e2fs_magic.get(), SB_MAGIC);

        let root_inode = parser.root_inode().expect("Root inode");

        parser
            .index(root_inode, Vec::new(), &mut |my_self, path, entry| {
                let entry_type = EntryType::from_u8(entry.e2d_type).expect("Entry Type");
                match entry_type {
                    EntryType::RegularFile => {
                        let data = my_self.read_file(entry).expect("File data");

                        let mut hasher = Sha256::new();
                        hasher.input(&data);
                        if path == vec!["inner", "file2"] {
                            assert_eq!(
                                "215ca145cbac95c9e2a6f5ff91ca1887c837b18e5f58fd2a7a16e2e5a3901e10",
                                hasher.result_str()
                            );
                        } else if path == vec!["file1"] {
                            assert_eq!(
                                "6bc35bfb2ca96c75a1fecde205693c19a827d4b04e90ace330048f3e031487dd",
                                hasher.result_str()
                            );
                        } else {
                            assert!(false, "Got an invalid file.");
                        }
                    }
                    EntryType::Directory => {
                        if path != vec!["inner"] && path != vec!["lost+found"] {
                            // These should be the only possible directories.
                            assert!(false, format!("Unexpected path {:?}", path));
                        }
                    }
                    _ => {
                        assert!(false, "No other types should exist in this image.");
                    }
                }
                Ok(true)
            })
            .expect("Index");
    }
}
