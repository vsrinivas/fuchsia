// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::mem;
use zerocopy::{AsBytes, FromBytes};

use crate::fs::*;
use crate::mm::vmo::round_up_to_increment;
use crate::types::*;

#[derive(Debug, Default, Copy, Clone)]
pub struct DirectoryEntryType(u8);

// These values are defined in libc.
impl DirectoryEntryType {
    pub const UNKNOWN: DirectoryEntryType = DirectoryEntryType(0);
    pub const FIFO: DirectoryEntryType = DirectoryEntryType(1);
    pub const CHR: DirectoryEntryType = DirectoryEntryType(2);
    pub const DIR: DirectoryEntryType = DirectoryEntryType(4);
    pub const BLK: DirectoryEntryType = DirectoryEntryType(6);
    pub const REG: DirectoryEntryType = DirectoryEntryType(8);
    pub const LNK: DirectoryEntryType = DirectoryEntryType(10);
    pub const SOCK: DirectoryEntryType = DirectoryEntryType(12);

    pub fn from_mode(mode: FileMode) -> DirectoryEntryType {
        match mode.fmt() {
            FileMode::IFLNK => DirectoryEntryType::LNK,
            FileMode::IFREG => DirectoryEntryType::REG,
            FileMode::IFDIR => DirectoryEntryType::DIR,
            FileMode::IFCHR => DirectoryEntryType::CHR,
            FileMode::IFBLK => DirectoryEntryType::BLK,
            FileMode::IFIFO => DirectoryEntryType::FIFO,
            FileMode::IFSOCK => DirectoryEntryType::SOCK,
            _ => DirectoryEntryType::UNKNOWN,
        }
    }

    pub fn bits(&self) -> u8 {
        self.0
    }
}

const DIRENT64_PADDING_SIZE: usize = 5;

#[repr(C)]
#[derive(Debug, Default, Copy, Clone, AsBytes, FromBytes)]
struct DirentHeader64 {
    d_ino: u64,
    d_off: i64,
    d_reclen: u16,
    d_type: u8,
    padding: [u8; DIRENT64_PADDING_SIZE],
}

const DIRENT32_PADDING_SIZE: usize = 6;

#[repr(C)]
#[derive(Debug, Default, Copy, Clone, AsBytes, FromBytes)]
struct DirentHeader32 {
    d_ino: u64,
    d_off: i64,
    d_reclen: u16,
    padding: [u8; DIRENT32_PADDING_SIZE],
    // pad: u8, // Zero padding byte
    // d_type: u8, // File type
}

const DIRENT64_HEADER_SIZE: usize = mem::size_of::<DirentHeader64>() - DIRENT64_PADDING_SIZE;
const DIRENT32_HEADER_SIZE: usize = mem::size_of::<DirentHeader32>() - DIRENT32_PADDING_SIZE;

pub trait DirentSink {
    /// Add the given directory entry to this buffer.
    ///
    /// Returns Err(ENOSPC) if the entry does not fit.
    fn add(
        &mut self,
        inode_num: ino_t,
        offset: off_t,
        entry_type: DirectoryEntryType,
        name: &FsStr,
    ) -> Result<(), Errno>;

    /// The bytes into which the directory entries have been serialized.
    fn bytes(&self) -> &[u8];
}

pub struct DirentSink64 {
    buffer: Vec<u8>,
}

impl DirentSink64 {
    pub fn with_capacity(capacity: usize) -> Self {
        Self { buffer: Vec::with_capacity(capacity) }
    }
}

impl DirentSink for DirentSink64 {
    fn add(
        &mut self,
        inode_num: ino_t,
        offset: off_t,
        entry_type: DirectoryEntryType,
        name: &FsStr,
    ) -> Result<(), Errno> {
        let content_size = DIRENT64_HEADER_SIZE + name.len();
        let entry_size = round_up_to_increment(content_size + 1, 8); // +1 for the null terminator.
        if self.buffer.len() + entry_size > self.buffer.capacity() {
            return Err(ENOSPC);
        }
        let staring_length = self.buffer.len();
        let header = DirentHeader64 {
            d_ino: inode_num,
            d_off: offset,
            d_reclen: entry_size as u16,
            d_type: entry_type.bits(),
            ..DirentHeader64::default()
        };
        let header_bytes = header.as_bytes();
        self.buffer.extend_from_slice(&header_bytes[..DIRENT64_HEADER_SIZE]);
        self.buffer.extend_from_slice(name);
        for _ in 0..(entry_size - content_size) {
            self.buffer.push(b'\0');
        }
        assert_eq!(self.buffer.len(), staring_length + entry_size);
        Ok(())
    }

    fn bytes(&self) -> &[u8] {
        self.buffer.as_slice()
    }
}

pub struct DirentSink32 {
    pub buffer: Vec<u8>,
}

impl DirentSink32 {
    pub fn with_capacity(capacity: usize) -> Self {
        Self { buffer: Vec::with_capacity(capacity) }
    }
}

impl DirentSink for DirentSink32 {
    fn add(
        &mut self,
        inode_num: ino_t,
        offset: off_t,
        entry_type: DirectoryEntryType,
        name: &FsStr,
    ) -> Result<(), Errno> {
        let content_size = DIRENT32_HEADER_SIZE + name.len();
        let entry_size = round_up_to_increment(content_size + 2, 8); // +1 for the null terminator, +1 for the type.
        if self.buffer.len() + entry_size > self.buffer.capacity() {
            return Err(ENOSPC);
        }
        let staring_length = self.buffer.len();
        let header = DirentHeader32 {
            d_ino: inode_num,
            d_off: offset,
            d_reclen: entry_size as u16,
            ..DirentHeader32::default()
        };
        let header_bytes = header.as_bytes();
        self.buffer.extend_from_slice(&header_bytes[..DIRENT32_HEADER_SIZE]);
        self.buffer.extend_from_slice(name);
        for _ in 0..(entry_size - content_size - 1) {
            self.buffer.push(b'\0');
        }
        self.buffer.push(entry_type.bits()); // Include the type.
        assert_eq!(self.buffer.len(), staring_length + entry_size);
        Ok(())
    }

    fn bytes(&self) -> &[u8] {
        self.buffer.as_slice()
    }
}
