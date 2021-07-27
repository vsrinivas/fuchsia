// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(dead_code)]

use std::ops;

use crate::types::uapi;

#[derive(Copy, Clone, Debug, Default, Eq, PartialEq)]
pub struct FileMode(u32);

impl FileMode {
    pub const IFLNK: FileMode = FileMode(uapi::S_IFLNK);
    pub const IFREG: FileMode = FileMode(uapi::S_IFREG);
    pub const IFDIR: FileMode = FileMode(uapi::S_IFDIR);
    pub const IFCHR: FileMode = FileMode(uapi::S_IFCHR);
    pub const IFBLK: FileMode = FileMode(uapi::S_IFBLK);
    pub const IFIFO: FileMode = FileMode(uapi::S_IFIFO);
    pub const IFSOCK: FileMode = FileMode(uapi::S_IFSOCK);

    pub const ISUID: FileMode = FileMode(uapi::S_ISUID);
    pub const ISGID: FileMode = FileMode(uapi::S_ISGID);
    pub const ISVTX: FileMode = FileMode(uapi::S_ISVTX);
    pub const IRWXU: FileMode = FileMode(uapi::S_IRWXU);
    pub const IRUSR: FileMode = FileMode(uapi::S_IRUSR);
    pub const IWUSR: FileMode = FileMode(uapi::S_IWUSR);
    pub const IXUSR: FileMode = FileMode(uapi::S_IXUSR);
    pub const IRWXG: FileMode = FileMode(uapi::S_IRWXG);
    pub const IRGRP: FileMode = FileMode(uapi::S_IRGRP);
    pub const IWGRP: FileMode = FileMode(uapi::S_IWGRP);
    pub const IXGRP: FileMode = FileMode(uapi::S_IXGRP);
    pub const IRWXO: FileMode = FileMode(uapi::S_IRWXO);
    pub const IROTH: FileMode = FileMode(uapi::S_IROTH);
    pub const IWOTH: FileMode = FileMode(uapi::S_IWOTH);
    pub const IXOTH: FileMode = FileMode(uapi::S_IXOTH);

    pub const IFMT: FileMode = FileMode(uapi::S_IFMT);

    pub const DEFAULT_UMASK: FileMode = FileMode(0o022);
    pub const ALLOW_ALL: FileMode = FileMode(0o777);
    pub const EMPTY: FileMode = FileMode(0);

    pub fn from_bits(mask: u32) -> FileMode {
        FileMode(mask)
    }

    pub fn bits(&self) -> u32 {
        self.0
    }

    pub fn contains(&self, other: FileMode) -> bool {
        *self & other != FileMode::EMPTY
    }

    pub fn fmt(&self) -> FileMode {
        FileMode(self.bits() & uapi::S_IFMT)
    }

    pub fn is_lnk(&self) -> bool {
        (self.bits() & uapi::S_IFMT) == uapi::S_IFLNK
    }

    pub fn is_reg(&self) -> bool {
        (self.bits() & uapi::S_IFMT) == uapi::S_IFREG
    }

    pub fn is_dir(&self) -> bool {
        (self.bits() & uapi::S_IFMT) == uapi::S_IFDIR
    }

    pub fn is_chr(&self) -> bool {
        (self.bits() & uapi::S_IFMT) == uapi::S_IFCHR
    }

    pub fn is_blk(&self) -> bool {
        (self.bits() & uapi::S_IFMT) == uapi::S_IFBLK
    }

    pub fn is_fifo(&self) -> bool {
        (self.bits() & uapi::S_IFMT) == uapi::S_IFIFO
    }

    pub fn is_sock(&self) -> bool {
        (self.bits() & uapi::S_IFMT) == uapi::S_IFSOCK
    }
}

impl ops::BitOr for FileMode {
    type Output = Self;

    fn bitor(self, rhs: Self) -> Self::Output {
        Self(self.0 | rhs.0)
    }
}

impl ops::BitAnd for FileMode {
    type Output = Self;

    fn bitand(self, rhs: Self) -> Self::Output {
        Self(self.0 & rhs.0)
    }
}

impl ops::Not for FileMode {
    type Output = Self;

    fn not(self) -> Self::Output {
        Self(!self.0)
    }
}
