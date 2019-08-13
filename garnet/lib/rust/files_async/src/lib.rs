// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await)]

use failure::Error;
use fidl_fuchsia_io::{DirectoryProxy, MAX_BUF};
use fuchsia_zircon as zx;
use std::collections::VecDeque;
use std::mem;

#[derive(Eq, Ord, PartialOrd, PartialEq, Clone, Copy, Debug)]
pub enum DirentType {
    Unknown,
    Directory,
    BlockDevice,
    File,
    Socket,
    Service,
}

impl From<u8> for DirentType {
    fn from(dir_type: u8) -> Self {
        match dir_type {
            fidl_fuchsia_io::DIRENT_TYPE_DIRECTORY => DirentType::Directory,
            fidl_fuchsia_io::DIRENT_TYPE_BLOCK_DEVICE => DirentType::BlockDevice,
            fidl_fuchsia_io::DIRENT_TYPE_FILE => DirentType::File,
            fidl_fuchsia_io::DIRENT_TYPE_SOCKET => DirentType::Socket,
            fidl_fuchsia_io::DIRENT_TYPE_SERVICE => DirentType::Service,
            _ => DirentType::Unknown,
        }
    }
}

#[derive(Eq, Ord, PartialOrd, PartialEq, Debug)]
pub struct DirEntry {
    pub name: String,
    pub dir_type: DirentType,
}

impl DirEntry {
    fn is_dir(&self) -> bool {
        self.dir_type == DirentType::Directory
    }

    fn chain(&self, subentry: &DirEntry) -> DirEntry {
        DirEntry { name: format!("{}/{}", self.name, subentry.name), dir_type: subentry.dir_type }
    }
}

pub async fn readdir_recursive(dir: &DirectoryProxy) -> Result<Vec<DirEntry>, Error> {
    let mut directories: VecDeque<DirEntry> = VecDeque::new();
    let mut entries: Vec<DirEntry> = Vec::new();

    // Prime directory queue with immediate descendants.
    {
        for entry in readdir(dir).await?.into_iter() {
            if entry.is_dir() {
                directories.push_back(entry)
            } else {
                entries.push(entry)
            }
        }
    }

    // Handle a single directory at a time, emitting leaf nodes and queueing up subdirectories for
    // later iterations.
    while let Some(entry) = directories.pop_front() {
        let (subdir, subdir_server_end) = fidl::endpoints::create_proxy()?;
        let flags = fidl_fuchsia_io::OPEN_FLAG_DIRECTORY | fidl_fuchsia_io::OPEN_RIGHT_READABLE;
        dir.open(flags, 0, &entry.name, subdir_server_end)?;
        let subdir = DirectoryProxy::new(subdir.into_channel().unwrap());

        let subentries = readdir(&subdir).await?;

        // Emit empty directories as a single entry.
        if subentries.is_empty() {
            entries.push(entry);
            continue;
        }

        for subentry in subentries.into_iter() {
            let subentry = entry.chain(&subentry);
            if subentry.is_dir() {
                directories.push_back(subentry)
            } else {
                entries.push(subentry)
            }
        }
    }

    Ok(entries)
}

pub async fn readdir(dir: &DirectoryProxy) -> Result<Vec<DirEntry>, Error> {
    #[repr(packed)]
    struct Dirent {
        _ino: u64,
        size: u8,
        _type: u8,
    }

    let mut entries = vec![];
    loop {
        let (status, buf) = dir.read_dirents(MAX_BUF).await?;
        zx::Status::ok(status)?;

        if buf.is_empty() {
            break;
        }

        // The buffer contains an arbitrary number of dirents.
        let mut slice = buf.as_slice();
        while !slice.is_empty() {
            // Read the dirent, and figure out how long the name is.
            let (head, rest) = slice.split_at(mem::size_of::<Dirent>());

            let entry = {
                // Cast the dirent bytes into a `Dirent`, and extract out the size of the name and
                // the entry type.
                let (size, _type) = unsafe {
                    let dirent: &Dirent = mem::transmute(head.as_ptr());
                    (dirent.size as usize, dirent._type)
                };

                // Advance to the next entry.
                slice = &rest[size..];

                DirEntry {
                    // Package resolver paths are always utf8.
                    name: String::from_utf8(rest[..size].to_vec())?,
                    dir_type: _type.into(),
                }
            };

            if entry.name != "." {
                entries.push(entry);
            }
        }
    }

    entries.sort_unstable();

    Ok(entries)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn build_direntry(name: &str, dir_type: DirentType) -> DirEntry {
        DirEntry { name: name.to_string(), dir_type }
    }

    #[test]
    fn test_direntry() {
        assert!(build_direntry("foo", DirentType::Directory).is_dir());

        // Negative test
        assert!(!build_direntry("foo", DirentType::File).is_dir());
        assert!(!build_direntry("foo", DirentType::Unknown).is_dir());
    }

    #[test]
    fn test_direntry_chaining() {
        let parent = build_direntry("foo", DirentType::Directory);

        let child1 = build_direntry("bar", DirentType::Directory);
        let chained1 = parent.chain(&child1);
        assert_eq!(&chained1.name, "foo/bar");
        assert_eq!(chained1.dir_type, DirentType::Directory);

        let child2 = build_direntry("baz", DirentType::File);
        let chained2 = parent.chain(&child2);
        assert_eq!(&chained2.name, "foo/baz");
        assert_eq!(chained2.dir_type, DirentType::File);
    }
}
