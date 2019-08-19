// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await)]

///! Safe wrappers for enumerating `fuchsia.io.Directory` contents.
use {
    failure::{Error, Fail},
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::{DirectoryMarker, DirectoryProxy, MAX_BUF},
    fuchsia_zircon as zx,
    std::{collections::VecDeque, mem, str::Utf8Error},
};

/// An error encountered while querying and parsing directory entries from a directory proxy.
#[derive(Debug, Fail)]
pub enum ReadDirError {
    #[fail(display = "a directory entry could not be decoded: {:?}", _0)]
    Decode(#[cause] DirentDecodeError),

    #[fail(display = "fidl error during read_dirents: {:?}", _0)]
    Fidl(#[cause] fidl::Error),

    #[fail(display = "read_dirents failed with status {:?}", _0)]
    ReadDir(#[cause] zx::Status),
}

/// An error encountered while decoding a single directory entry.
#[derive(Debug, PartialEq, Eq, Fail)]
pub enum DirentDecodeError {
    #[fail(display = "an entry extended past the end of the buffer")]
    BufferOverrun,

    #[fail(display = "name is not valid utf-8")]
    InvalidUtf8(#[cause] Utf8Error),
}

/// The type of a node.
#[derive(Eq, Ord, PartialOrd, PartialEq, Clone, Copy, Debug)]
pub enum DirentKind {
    Unknown,
    Directory,
    BlockDevice,
    File,
    Socket,
    Service,
}

impl From<u8> for DirentKind {
    fn from(kind: u8) -> Self {
        match kind {
            fidl_fuchsia_io::DIRENT_TYPE_DIRECTORY => DirentKind::Directory,
            fidl_fuchsia_io::DIRENT_TYPE_BLOCK_DEVICE => DirentKind::BlockDevice,
            fidl_fuchsia_io::DIRENT_TYPE_FILE => DirentKind::File,
            fidl_fuchsia_io::DIRENT_TYPE_SOCKET => DirentKind::Socket,
            fidl_fuchsia_io::DIRENT_TYPE_SERVICE => DirentKind::Service,
            _ => DirentKind::Unknown,
        }
    }
}

/// A directory entry.
#[derive(Eq, Ord, PartialOrd, PartialEq, Debug)]
pub struct DirEntry {
    /// The name of this node.
    pub name: String,

    /// The type of this node, or [`DirentKind::Unknown`] if not known.
    pub kind: DirentKind,
}

impl DirEntry {
    fn is_dir(&self) -> bool {
        self.kind == DirentKind::Directory
    }

    fn chain(&self, subentry: &DirEntry) -> DirEntry {
        DirEntry { name: format!("{}/{}", self.name, subentry.name), kind: subentry.kind }
    }
}

/// Returns a Vec of all non-directory nodes and all empty directory nodes in the given directory
/// proxy. The returned entries will not include ".".
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
        let (subdir, subdir_server_end) = fidl::endpoints::create_proxy::<DirectoryMarker>()?;
        let flags = fidl_fuchsia_io::OPEN_FLAG_DIRECTORY | fidl_fuchsia_io::OPEN_RIGHT_READABLE;
        dir.open(flags, 0, &entry.name, ServerEnd::new(subdir_server_end.into_channel()))?;

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

/// Returns a sorted Vec of directory entries contained directly in the given directory proxy. The
/// returned entries will not include "." or nodes from any subdirectories.
pub async fn readdir(dir: &DirectoryProxy) -> Result<Vec<DirEntry>, ReadDirError> {
    let mut entries = vec![];

    loop {
        let (status, buf) = dir.read_dirents(MAX_BUF).await.map_err(ReadDirError::Fidl)?;
        zx::Status::ok(status).map_err(ReadDirError::ReadDir)?;

        if buf.is_empty() {
            break;
        }

        for entry in parse_dir_entries(&buf) {
            let entry = entry.map_err(ReadDirError::Decode)?;
            if entry.name != "." {
                entries.push(entry);
            }
        }
    }

    entries.sort_unstable();

    Ok(entries)
}

fn parse_dir_entries(mut buf: &[u8]) -> Vec<Result<DirEntry, DirentDecodeError>> {
    #[repr(C, packed)]
    struct Dirent {
        /// The inode number of the entry.
        _ino: u64,
        /// The length of the filename located after this entry.
        size: u8,
        /// The type of the entry. One of the `fidl_fuchsia_io::DIRENT_TYPE_*` constants.
        kind: u8,
        // The unterminated name of the entry.  Length is the `size` field above.
        // char name[0],
    }
    const DIRENT_SIZE: usize = mem::size_of::<Dirent>();

    let mut entries = vec![];

    while !buf.is_empty() {
        // Don't read past the end of the buffer.
        if DIRENT_SIZE > buf.len() {
            entries.push(Err(DirentDecodeError::BufferOverrun));
            return entries;
        }

        // Read the dirent, and figure out how long the name is.
        let (head, rest) = buf.split_at(DIRENT_SIZE);

        let entry = {
            // Cast the dirent bytes into a `Dirent`, and extract out the size of the name and the
            // entry type.
            let (size, kind) = unsafe {
                let dirent: &Dirent = mem::transmute(head.as_ptr());
                (dirent.size as usize, dirent.kind)
            };

            // Don't read past the end of the buffer.
            if size > rest.len() {
                entries.push(Err(DirentDecodeError::BufferOverrun));
                return entries;
            }

            // Advance to the next entry.
            buf = &rest[size..];
            match String::from_utf8(rest[..size].to_vec()) {
                Ok(name) => Ok(DirEntry { name, kind: kind.into() }),
                Err(err) => Err(DirentDecodeError::InvalidUtf8(err.utf8_error())),
            }
        };

        entries.push(entry);
    }

    entries
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fuchsia_async as fasync,
        fuchsia_vfs_pseudo_fs::{
            directory::entry::DirectoryEntry, file::simple::read_only_str, pseudo_directory,
        },
        proptest::prelude::*,
    };

    proptest! {
        #[test]
        fn test_parse_dir_entries_does_not_crash(buf in prop::collection::vec(any::<u8>(), 0..200)) {
            parse_dir_entries(&buf);
        }
    }

    #[test]
    fn test_parse_dir_entries() {
        #[rustfmt::skip]
        let buf = &[
            // ino
            42, 0, 0, 0, 0, 0, 0, 0,
            // name length
            4,
            // type
            fidl_fuchsia_io::DIRENT_TYPE_FILE,
            // name
            't' as u8, 'e' as u8, 's' as u8, 't' as u8,
        ];

        assert_eq!(
            parse_dir_entries(buf),
            vec![Ok(DirEntry { name: "test".to_string(), kind: DirentKind::File })]
        );
    }

    #[test]
    fn test_parse_dir_entries_rejects_invalid_utf8() {
        #[rustfmt::skip]
        let buf = &[
            // entry 0
            // ino
            1, 0, 0, 0, 0, 0, 0, 0,
            // name length
            1,
            // type
            fidl_fuchsia_io::DIRENT_TYPE_FILE,
            // name (a lonely continuation byte)
            0x80,
            // entry 1
            // ino
            2, 0, 0, 0, 0, 0, 0, 0,
            // name length
            4,
            // type
            fidl_fuchsia_io::DIRENT_TYPE_FILE,
            // name
            'o' as u8, 'k' as u8, 'a' as u8, 'y' as u8,
        ];

        let expected_err = std::str::from_utf8(&[0x80]).unwrap_err();

        assert_eq!(
            parse_dir_entries(buf),
            vec![
                Err(DirentDecodeError::InvalidUtf8(expected_err)),
                Ok(DirEntry { name: "okay".to_string(), kind: DirentKind::File })
            ]
        );
    }

    #[test]
    fn test_parse_dir_entries_overrun() {
        #[rustfmt::skip]
        let buf = &[
            // ino
            0, 0, 0, 0, 0, 0, 0, 0,
            // name length
            5,
            // type
            fidl_fuchsia_io::DIRENT_TYPE_FILE,
            // name
            't' as u8, 'e' as u8, 's' as u8, 't' as u8,
        ];

        assert_eq!(parse_dir_entries(buf), vec![Err(DirentDecodeError::BufferOverrun)]);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_readdir() {
        let (dir, server_end) = fidl::endpoints::create_proxy::<DirectoryMarker>().unwrap();
        fasync::spawn(async move {
            let mut dir = pseudo_directory! {
                "afile" => read_only_str(|| Ok("".into())),
                "zzz" => read_only_str(|| Ok("".into())),
                "subdir" => pseudo_directory! {
                    "ignored" => read_only_str(|| Ok("".into())),
                },
            };
            dir.open(
                fidl_fuchsia_io::OPEN_FLAG_DIRECTORY | fidl_fuchsia_io::OPEN_RIGHT_READABLE,
                0,
                &mut std::iter::empty(),
                ServerEnd::new(server_end.into_channel()),
            );
            dir.await;
            unreachable!();
        });

        let entries = readdir(&dir).await.expect("readdir to succeed");
        assert_eq!(
            entries,
            vec![
                build_direntry("afile", DirentKind::File),
                build_direntry("subdir", DirentKind::Directory),
                build_direntry("zzz", DirentKind::File),
            ]
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_readdir_recursive() {
        let (dir, server_end) = fidl::endpoints::create_proxy::<DirectoryMarker>().unwrap();
        fasync::spawn(async move {
            let mut dir = pseudo_directory! {
                "a" => read_only_str(|| Ok("".into())),
                "b" => read_only_str(|| Ok("".into())),
                "emptydir" => pseudo_directory! { },
                "subdir" => pseudo_directory! {
                    "subsubdir" => pseudo_directory! {
                        "a" => read_only_str(|| Ok("".into())),
                        "emptydir" => pseudo_directory! { },
                    },
                },
            };
            dir.open(
                fidl_fuchsia_io::OPEN_FLAG_DIRECTORY | fidl_fuchsia_io::OPEN_RIGHT_READABLE,
                0,
                &mut std::iter::empty(),
                ServerEnd::new(server_end.into_channel()),
            );
            dir.await;
            unreachable!();
        });

        let entries = readdir_recursive(&dir).await.expect("readdir_recursive to succeed");
        assert_eq!(
            entries,
            vec![
                build_direntry("a", DirentKind::File),
                build_direntry("b", DirentKind::File),
                build_direntry("emptydir", DirentKind::Directory),
                build_direntry("subdir/subsubdir/a", DirentKind::File),
                build_direntry("subdir/subsubdir/emptydir", DirentKind::Directory),
            ]
        );
    }

    fn build_direntry(name: &str, kind: DirentKind) -> DirEntry {
        DirEntry { name: name.to_string(), kind }
    }

    #[test]
    fn test_direntry_is_dir() {
        assert!(build_direntry("foo", DirentKind::Directory).is_dir());

        // Negative test
        assert!(!build_direntry("foo", DirentKind::File).is_dir());
        assert!(!build_direntry("foo", DirentKind::Unknown).is_dir());
    }

    #[test]
    fn test_direntry_chaining() {
        let parent = build_direntry("foo", DirentKind::Directory);

        let child1 = build_direntry("bar", DirentKind::Directory);
        let chained1 = parent.chain(&child1);
        assert_eq!(&chained1.name, "foo/bar");
        assert_eq!(chained1.kind, DirentKind::Directory);

        let child2 = build_direntry("baz", DirentKind::File);
        let chained2 = parent.chain(&child2);
        assert_eq!(&chained2.name, "foo/baz");
        assert_eq!(chained2.kind, DirentKind::File);
    }
}
