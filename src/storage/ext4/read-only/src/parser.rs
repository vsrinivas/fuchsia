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
            BlockGroupDesc32, DirEntry2, DirEntryHeader, EntryType, Extent, ExtentHeader,
            ExtentIndex, ExtentTreeNode, INode, InvalidAddressErrorType, ParseToStruct,
            ParsingError, SuperBlock, FIRST_BG_PADDING, MIN_EXT4_SIZE, ROOT_INODE_NUM,
        },
    },
    once_cell::sync::OnceCell,
    std::{
        convert::TryInto,
        mem::size_of,
        path::{Component, Path},
        str,
        sync::Arc,
    },
    vfs::{
        directory::immutable, file::vmo::asynchronous::read_only_const, tree_builder::TreeBuilder,
    },
    zerocopy::ByteSlice,
};

// Assuming/ensuring that we are on a 64bit system where u64 == usize.
assert_eq_size!(u64, usize);

pub struct Parser<T: Reader> {
    reader: Arc<T>,
    super_block: OnceCell<Arc<SuperBlock>>,
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
        Parser { reader: Arc::new(reader), super_block: OnceCell::new() }
    }

    /// Returns the Super Block.
    ///
    /// If the super block has been parsed and saved before, return that.
    /// Else, parse the super block and save it and return it.
    ///
    /// We never need to re-parse the super block in this read-only
    /// implementation.
    fn super_block(&self) -> Result<Arc<SuperBlock>, ParsingError> {
        Ok(self.super_block.get_or_try_init(|| SuperBlock::parse(self.reader.clone()))?.clone())
    }

    /// Reads block size from the Super Block.
    fn block_size(&self) -> Result<u64, ParsingError> {
        self.super_block()?.block_size()
    }

    /// Reads full raw data from a given block number.
    fn block(&self, block_number: u64) -> Result<Box<[u8]>, ParsingError> {
        if block_number == 0 {
            return Err(ParsingError::InvalidAddress(
                InvalidAddressErrorType::Lower,
                0,
                FIRST_BG_PADDING,
            ));
        }
        let block_size = self.block_size()?;
        let address = block_number
            .checked_mul(block_size)
            .ok_or(ParsingError::BlockNumberOutOfBounds(block_number))?;

        let mut data = vec![0u8; block_size.try_into().unwrap()];
        self.reader.read(address, data.as_mut_slice()).map_err(Into::<ParsingError>::into)?;

        Ok(data.into_boxed_slice())
    }

    /// Reads the INode at the given inode number.
    pub fn inode(&self, inode_number: u32) -> Result<Arc<INode>, ParsingError> {
        if inode_number < 1 {
            // INode number 0 is not allowed per ext4 spec.
            return Err(ParsingError::InvalidInode(inode_number));
        }
        let sb = self.super_block()?;
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
        let bgd_table_offset = if block_size >= MIN_EXT4_SIZE {
            // Padding and Super Block both fit in the first block, so offset to the next
            // block.
            block_size
        } else {
            // Block size is less than 2048. The only valid block size smaller than 2048 is 1024.
            // Padding and Super Block take one block each, so offset to the third block.
            block_size * 2
        };

        let bgd_offset = (inode_number - 1) as u64 / sb.e2fs_ipg.get() as u64
            * size_of::<BlockGroupDesc32>() as u64;
        let bgd = BlockGroupDesc32::from_reader_with_offset(
            self.reader.clone(),
            bgd_table_offset + bgd_offset,
            ParsingError::InvalidBlockGroupDesc(block_size),
        )?;

        // Offset could really be anywhere, and the Reader will enforce reading within the
        // filesystem size. Not much can be checked here.
        let inode_table_offset =
            (inode_number - 1) as u64 % sb.e2fs_ipg.get() as u64 * sb.e2fs_inode_size.get() as u64;
        let inode_addr = (bgd.ext2bgd_i_tables.get() as u64 * block_size) + inode_table_offset;
        if inode_addr < MIN_EXT4_SIZE {
            return Err(ParsingError::InvalidAddress(
                InvalidAddressErrorType::Lower,
                inode_addr,
                MIN_EXT4_SIZE,
            ));
        }

        INode::from_reader_with_offset(
            self.reader.clone(),
            inode_addr,
            ParsingError::InvalidInode(inode_number),
        )
    }

    /// Helper function to get the root directory INode.
    pub fn root_inode(&self) -> Result<Arc<INode>, ParsingError> {
        self.inode(ROOT_INODE_NUM)
    }

    /// Reads all raw data from a given extent leaf node.
    fn extent_data(&self, extent: &Extent, mut allowance: u64) -> Result<Vec<u8>, ParsingError> {
        let block_number = extent.target_block_num();
        let block_count = extent.e_len.get() as u64;
        let block_size = self.block_size()?;
        let mut read_len;

        let mut data = Vec::with_capacity((block_size * block_count).try_into().unwrap());

        for i in 0..block_count {
            let block_data = self.block(block_number + i as u64)?;
            if allowance >= block_size {
                read_len = block_size;
            } else {
                read_len = allowance;
            }
            let block_data = &block_data[0..read_len.try_into().unwrap()];
            data.append(&mut block_data.to_vec());
            allowance -= read_len;
        }

        Ok(data)
    }

    /// Reads extent data from a leaf node.
    ///
    /// # Arguments
    /// * `extent`: Extent from which to read data from.
    /// * `data`: Vec where data that is read is added.
    /// * `allowance`: The maximum number of bytes to read from the extent. The
    ///    given file allowance is updated on each call to track sizing for an
    ///    entire extent tree.
    fn read_extent_data(
        &self,
        extent: &Extent,
        data: &mut Vec<u8>,
        allowance: &mut u64,
    ) -> Result<(), ParsingError> {
        let mut extent_data = self.extent_data(&extent, *allowance)?;
        let extent_len = extent_data.len() as u64;
        if extent_len > *allowance {
            return Err(ParsingError::ExtentUnexpectedLength(extent_len, *allowance));
        }
        *allowance -= extent_len;
        data.append(&mut extent_data);
        Ok(())
    }

    /// Reads directory entries from an extent leaf node.
    fn read_dir_entries(
        &self,
        extent: &Extent,
        entries: &mut Vec<Arc<DirEntry2>>,
    ) -> Result<(), ParsingError> {
        let block_size = self.block_size()?;
        let target_block_offset = extent.target_block_num() * block_size;

        // The `e2d_reclen` of the last entry will be large enough fill the
        // remaining space of the block.
        for block_index in 0..extent.e_len.get() {
            let mut dir_entry_offset = 0u64;
            while (dir_entry_offset + size_of::<DirEntryHeader>() as u64) < block_size {
                let offset =
                    dir_entry_offset + target_block_offset + (block_index as u64 * block_size);

                let de_header = DirEntryHeader::from_reader_with_offset(
                    self.reader.clone(),
                    offset,
                    ParsingError::InvalidDirEntry2(offset),
                )?;
                let mut de = DirEntry2 {
                    e2d_ino: de_header.e2d_ino,
                    e2d_reclen: de_header.e2d_reclen,
                    e2d_namlen: de_header.e2d_namlen,
                    e2d_type: de_header.e2d_type,
                    e2d_name: [0u8; 255],
                };
                self.reader.read(
                    offset + size_of::<DirEntryHeader>() as u64,
                    &mut de.e2d_name[..de.e2d_namlen as usize],
                )?;

                dir_entry_offset += de.e2d_reclen.get() as u64;

                if de.e2d_ino.get() != 0 {
                    entries.push(Arc::new(de));
                }
            }
        }
        Ok(())
    }

    /// Handles an extent tree leaf node by invoking `extent_handler` for each contained extent.
    fn iterate_extents_in_leaf<B: ByteSlice, F: FnMut(&Extent) -> Result<(), ParsingError>>(
        &self,
        extent_tree_node: &ExtentTreeNode<B>,
        extent_handler: &mut F,
    ) -> Result<(), ParsingError> {
        for e_index in 0..extent_tree_node.header.eh_ecount.get() {
            let start = size_of::<Extent>() * e_index as usize;
            let end = start + size_of::<Extent>() as usize;
            let e = Extent::to_struct_ref(
                &(extent_tree_node.entries)[start..end],
                ParsingError::InvalidExtent(start as u64),
            )?;

            extent_handler(e)?;
        }

        Ok(())
    }

    /// Handles traversal down an extent tree.
    fn iterate_extents_in_tree<B: ByteSlice, F: FnMut(&Extent) -> Result<(), ParsingError>>(
        &self,
        extent_tree_node: &ExtentTreeNode<B>,
        extent_handler: &mut F,
    ) -> Result<(), ParsingError> {
        let block_size = self.block_size()?;

        match extent_tree_node.header.eh_depth.get() {
            0 => {
                self.iterate_extents_in_leaf(extent_tree_node, extent_handler)?;
            }
            1..=4 => {
                for e_index in 0..extent_tree_node.header.eh_ecount.get() {
                    let start: usize = size_of::<Extent>() * e_index as usize;
                    let end = start + size_of::<Extent>();
                    let e = ExtentIndex::to_struct_ref(
                        &(extent_tree_node.entries)[start..end],
                        ParsingError::InvalidExtent(start as u64),
                    )?;

                    let next_level_offset = e.target_block_num() as u64 * block_size;

                    let next_extent_header = ExtentHeader::from_reader_with_offset(
                        self.reader.clone(),
                        next_level_offset,
                        ParsingError::InvalidExtent(next_level_offset),
                    )?;

                    let entry_count = next_extent_header.eh_ecount.get() as usize;
                    let entry_size = match next_extent_header.eh_depth.get() {
                        0 => size_of::<Extent>(),
                        _ => size_of::<ExtentIndex>(),
                    };
                    let node_size = size_of::<ExtentHeader>() + (entry_count * entry_size);

                    let mut data = vec![0u8; node_size];
                    self.reader.read(next_level_offset, data.as_mut_slice())?;

                    let next_level_node = ExtentTreeNode::parse(data.as_slice())
                        .ok_or_else(|| ParsingError::InvalidExtent(next_level_offset))?;

                    self.iterate_extents_in_tree(&next_level_node, extent_handler)?;
                }
            }
            _ => return Err(ParsingError::InvalidExtentHeader),
        };

        Ok(())
    }

    /// Lists directory entries from the directory that is the given Inode.
    ///
    /// Errors if the Inode does not map to a Directory.
    pub fn entries_from_inode(
        &self,
        inode: &Arc<INode>,
    ) -> Result<Vec<Arc<DirEntry2>>, ParsingError> {
        let root_extent_tree_node = inode.extent_tree_node()?;
        let mut dir_entries = Vec::new();

        self.iterate_extents_in_tree(&root_extent_tree_node, &mut |extent| {
            self.read_dir_entries(extent, &mut dir_entries)
        })?;

        Ok(dir_entries)
    }

    /// Gets any DirEntry2 that isn't root.
    ///
    /// Root doesn't have a DirEntry2.
    ///
    /// When dynamic loading of files is supported, this is the required mechanism.
    pub fn entry_at_path(&self, path: &Path) -> Result<Arc<DirEntry2>, ParsingError> {
        let root_inode = self.root_inode()?;
        let root_entries = self.entries_from_inode(&root_inode)?;
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
                                    DirEntry2::as_hash_map(self.entries_from_inode(&inode)?)?;
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

    /// Reads all raw data for a given inode.
    ///
    /// For a file, this will be the file data. For a symlink,
    /// this will be the symlink target.
    pub fn read_data(&self, inode_num: u32) -> Result<Vec<u8>, ParsingError> {
        let inode = self.inode(inode_num)?;
        let mut size_remaining = inode.size();
        let mut data = Vec::with_capacity(size_remaining.try_into().unwrap());

        // Check for symlink with inline data.
        if u16::from(inode.e2di_mode) & 0xa000 != 0 && u32::from(inode.e2di_nblock) == 0 {
            data.extend_from_slice(&inode.e2di_blocks[..inode.size().try_into().unwrap()]);
            return Ok(data);
        }

        let root_extent_tree_node = inode.extent_tree_node()?;
        let mut extents = Vec::new();

        self.iterate_extents_in_tree(&root_extent_tree_node, &mut |extent| {
            extents.push(extent.clone());
            Ok(())
        })?;

        let block_size = self.block_size()?;

        // Summarized from https://www.kernel.org/doc/ols/2007/ols2007v2-pages-21-34.pdf,
        // Section 2.2: Extent and ExtentHeader entries must be sorted by logical block number. This
        // enforces that when the extent tree is traversed depth first that a list of extents sorted
        // by logical block number is produced. This is a requirement to produce the proper ordering
        // of bytes within `data` here.
        for extent in extents {
            let buffer_offset = extent.e_blk.get() as u64 * block_size;

            // File may be sparse. Sparse files will have gaps
            // between logical blocks. Fill in any gaps with zeros.
            if buffer_offset > data.len() as u64 {
                size_remaining -= buffer_offset - data.len() as u64;
                data.resize(buffer_offset.try_into().unwrap(), 0);
            }

            self.read_extent_data(&extent, &mut data, &mut size_remaining)?;
        }

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
        &self,
        inode: Arc<INode>,
        prefix: Vec<&str>,
        receiver: &mut R,
    ) -> Result<bool, ParsingError>
    where
        R: FnMut(&Parser<T>, Vec<&str>, Arc<DirEntry2>) -> Result<bool, ParsingError>,
    {
        let entries = self.entries_from_inode(&inode)?;
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
    pub fn build_fuchsia_tree(&self) -> Result<Arc<immutable::Simple>, ParsingError> {
        let root_inode = self.root_inode()?;
        let mut tree = TreeBuilder::empty_dir();

        self.index(root_inode, Vec::new(), &mut |my_self, path, entry| {
            let entry_type = EntryType::from_u8(entry.e2d_type)?;
            match entry_type {
                EntryType::RegularFile => {
                    let data = my_self.read_data(entry.e2d_ino.into())?;
                    let bytes = data.as_slice();
                    tree.add_entry(path.clone(), read_only_const(bytes))
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
        crate::{parser::Parser, readers::VecReader, structs::EntryType},
        maplit::hashmap,
        sha2::{Digest, Sha256},
        std::{
            collections::{HashMap, HashSet},
            fs,
            path::Path,
            str,
        },
        test_case::test_case,
    };

    #[test]
    fn list_root_1_file() {
        let data = fs::read("/pkg/data/1file.img").expect("Unable to read file");
        let parser = Parser::new(VecReader::new(data));
        assert!(parser.super_block().expect("Super Block").check_magic().is_ok());
        let root_inode = parser.root_inode().expect("Parse INode");
        let entries = parser.entries_from_inode(&root_inode).expect("List entries");
        let mut expected_entries = vec!["file1", "lost+found", "..", "."];

        for de in &entries {
            assert_eq!(expected_entries.pop().unwrap(), de.name().unwrap());
        }
        assert_eq!(expected_entries.len(), 0);
    }

    #[test_case(
        "/pkg/data/nest.img",
        vec!["inner", "file1", "lost+found", "..", "."];
        "fs with a single directory")]
    #[test_case(
        "/pkg/data/extents.img",
        vec!["a", "smallfile", "largefile", "sparsefile", "lost+found", "..", "."];
        "fs with multiple files with multiple extents")]
    fn list_root(ext4_path: &str, mut expected_entries: Vec<&str>) {
        let data = fs::read(ext4_path).expect("Unable to read file");
        let parser = Parser::new(VecReader::new(data));
        assert!(parser.super_block().expect("Super Block").check_magic().is_ok());
        let root_inode = parser.root_inode().expect("Parse INode");
        let entries = parser.entries_from_inode(&root_inode).expect("List entries");

        for de in &entries {
            assert_eq!(expected_entries.pop().unwrap(), de.name().unwrap());
        }
        assert_eq!(expected_entries.len(), 0);
    }

    #[test]
    fn get_from_path() {
        let data = fs::read("/pkg/data/nest.img").expect("Unable to read file");
        let parser = Parser::new(VecReader::new(data));
        assert!(parser.super_block().expect("Super Block").check_magic().is_ok());

        let entry = parser.entry_at_path(Path::new("/inner")).expect("Entry at path");
        assert_eq!(entry.e2d_ino.get(), 12);
        assert_eq!(entry.name().unwrap(), "inner");

        let entry = parser.entry_at_path(Path::new("/inner/file2")).expect("Entry at path");
        assert_eq!(entry.e2d_ino.get(), 17);
        assert_eq!(entry.name().unwrap(), "file2");
    }

    #[test]
    fn read_data() {
        let data = fs::read("/pkg/data/1file.img").expect("Unable to read file");
        let parser = Parser::new(VecReader::new(data));
        assert!(parser.super_block().expect("Super Block").check_magic().is_ok());

        let entry = parser.entry_at_path(Path::new("file1")).expect("Entry at path");
        assert_eq!(entry.e2d_ino.get(), 15);
        assert_eq!(entry.name().unwrap(), "file1");

        let data = parser.read_data(entry.e2d_ino.into()).expect("File data");
        let compare = "file1 contents.\n";
        assert_eq!(data.len(), compare.len());
        assert_eq!(str::from_utf8(data.as_slice()).expect("File data"), compare);
    }

    #[test]
    fn fail_inode_zero() {
        let data = fs::read("/pkg/data/1file.img").expect("Unable to read file");
        let parser = Parser::new(VecReader::new(data));
        assert!(parser.inode(0).is_err());
    }

    #[test]
    fn index() {
        let data = fs::read("/pkg/data/nest.img").expect("Unable to read file");
        let parser = Parser::new(VecReader::new(data));
        assert!(parser.super_block().expect("Super Block").check_magic().is_ok());

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

    #[test_case(
        "/pkg/data/extents.img",
        hashmap!{
            "largefile".to_string() => "de2cf635ae4e0e727f1e412f978001d6a70d2386dc798d4327ec8c77a8e4895d".to_string(),
            "smallfile".to_string() => "5891b5b522d5df086d0ff0b110fbd9d21bb4fc7163af34d08286a2e846f6be03".to_string(),
            "sparsefile".to_string() => "3f411e42c1417cd8845d7144679812be3e120318d843c8c6e66d8b2c47a700e9".to_string(),
            "a/multi/dir/path/within/this/crowded/extents/test/img/empty".to_string() => "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855".to_string(),
        },
        vec!["a/multi/dir/path/within/this/crowded/extents/test/img", "lost+found"];
        "fs with multiple files with multiple extents")]
    #[test_case(
        "/pkg/data/1file.img",
        hashmap!{
            "file1".to_string() => "6bc35bfb2ca96c75a1fecde205693c19a827d4b04e90ace330048f3e031487dd".to_string(),
        },
        vec!["lost+found"];
        "fs with one small file")]
    #[test_case(
        "/pkg/data/nest.img",
        hashmap!{
            "file1".to_string() => "6bc35bfb2ca96c75a1fecde205693c19a827d4b04e90ace330048f3e031487dd".to_string(),
            "inner/file2".to_string() => "215ca145cbac95c9e2a6f5ff91ca1887c837b18e5f58fd2a7a16e2e5a3901e10".to_string(),
        },
        vec!["inner", "lost+found"];
        "fs with a single directory")]
    #[test_case(
        "/pkg/data/longdir.img",
        {
            let mut hash = HashMap::new();
            for i in 1...1000 {
                hash.insert(i.to_string(), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855".to_string());
            }
            hash
        },
        vec!["lost+found"];
        "fs with many entries in a directory")]
    fn check_data(
        ext4_path: &str,
        mut file_hashes: HashMap<String, String>,
        expected_dirs: Vec<&str>,
    ) {
        let data = fs::read(ext4_path).expect("Unable to read file");
        let parser = Parser::new(VecReader::new(data));
        assert!(parser.super_block().expect("Super Block").check_magic().is_ok());

        let root_inode = parser.root_inode().expect("Root inode");

        parser
            .index(root_inode, Vec::new(), &mut |my_self, path, entry| {
                let entry_type = EntryType::from_u8(entry.e2d_type).expect("Entry Type");
                let file_path = path.join("/");

                match entry_type {
                    EntryType::RegularFile => {
                        let data = my_self.read_data(entry.e2d_ino.into()).expect("File data");

                        let mut hasher = Sha256::new();
                        hasher.update(&data);
                        assert_eq!(
                            file_hashes.remove(&file_path).unwrap(),
                            hex::encode(hasher.finalize())
                        );
                    }
                    EntryType::Directory => {
                        let mut found = false;

                        // These should be the only possible directories.
                        for expected_dir in expected_dirs.iter() {
                            if expected_dir.starts_with(&file_path) {
                                found = true;
                                break;
                            }
                        }
                        assert!(found, "Unexpected path {}", file_path);
                    }
                    _ => {
                        assert!(false, "No other types should exist in this image.");
                    }
                }
                Ok(true)
            })
            .expect("Index");
        assert!(file_hashes.is_empty(), "Expected files were not found {:?}", file_hashes);
    }
}
