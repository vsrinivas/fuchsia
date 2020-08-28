// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![recursion_limit = "256"]

///! Safe wrappers for enumerating `fuchsia.io.Directory` contents.
use {
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::{self as fio, DirectoryMarker, DirectoryProxy, MAX_BUF, MODE_TYPE_DIRECTORY},
    fuchsia_async::{DurationExt, TimeoutExt},
    fuchsia_zircon as zx,
    futures::{
        future::BoxFuture,
        stream::{self, BoxStream, StreamExt},
    },
    std::{collections::VecDeque, mem, str::Utf8Error},
    thiserror::{self, Error},
};

/// Error returned by files_async library.
#[derive(Debug, Error)]
pub enum Error {
    #[error("a directory entry could not be decoded: {:?}", _0)]
    DecodeDirent(DecodeDirentError),

    #[error("fidl error during {}: {:?}", _0, _1)]
    Fidl(&'static str, fidl::Error),

    #[error("`read_dirents` failed with status {:?}", _0)]
    ReadDirents(zx::Status),

    #[error("`unlink` failed with status {:?}", _0)]
    Unlink(zx::Status),

    #[error("timeout")]
    Timeout,

    #[error("`rewind` failed with status {:?}", _0)]
    Rewind(zx::Status),

    #[error("Failed to read directory {}: {:?}", name, err)]
    ReadDir { name: String, err: anyhow::Error },
}

/// An error encountered while decoding a single directory entry.
#[derive(Debug, PartialEq, Eq, Error)]
pub enum DecodeDirentError {
    #[error("an entry extended past the end of the buffer")]
    BufferOverrun,

    #[error("name is not valid utf-8")]
    InvalidUtf8(Utf8Error),
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
            fio::DIRENT_TYPE_DIRECTORY => DirentKind::Directory,
            fio::DIRENT_TYPE_BLOCK_DEVICE => DirentKind::BlockDevice,
            fio::DIRENT_TYPE_FILE => DirentKind::File,
            fio::DIRENT_TYPE_SOCKET => DirentKind::Socket,
            fio::DIRENT_TYPE_SERVICE => DirentKind::Service,
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
    fn root() -> Self {
        Self { name: "".to_string(), kind: DirentKind::Directory }
    }

    fn is_dir(&self) -> bool {
        self.kind == DirentKind::Directory
    }

    fn is_root(&self) -> bool {
        self.is_dir() && self.name.is_empty()
    }

    fn chain(&self, subentry: &DirEntry) -> DirEntry {
        if self.name.is_empty() {
            DirEntry { name: subentry.name.clone(), kind: subentry.kind }
        } else {
            DirEntry { name: format!("{}/{}", self.name, subentry.name), kind: subentry.kind }
        }
    }
}

/// Returns a Vec of all non-directory nodes and all empty directory nodes in the given directory
/// proxy. The returned entries will not include ".".
/// |timeout| can be provided optionally to specify the maximum time to wait for a directory to be
/// read.
pub fn readdir_recursive(
    dir: &DirectoryProxy,
    timeout: Option<zx::Duration>,
) -> BoxStream<'_, Result<DirEntry, Error>> {
    let mut pending = VecDeque::new();
    pending.push_back(DirEntry::root());
    let results: VecDeque<DirEntry> = VecDeque::new();

    stream::unfold((results, pending), move |(mut results, mut pending)| {
        async move {
            loop {
                // Pending results to stream from the last read directory.
                if !results.is_empty() {
                    let result = results.pop_front().unwrap();
                    return Some((Ok(result), (results, pending)));
                }

                // No pending directories to read and per the last condition no pending results to
                // stream so finish the stream.
                if pending.is_empty() {
                    return None;
                }

                // The directory that will be read now.
                let dir_entry = pending.pop_front().unwrap();

                let (subdir, subdir_server) =
                    match fidl::endpoints::create_proxy::<DirectoryMarker>() {
                        Ok((subdir, server)) => (subdir, server),
                        Err(e) => {
                            return Some((Err(Error::Fidl("create_proxy", e)), (results, pending)))
                        }
                    };
                let dir_ref = if dir_entry.is_root() {
                    dir
                } else {
                    let open_dir_result = dir.open(
                        fio::OPEN_FLAG_DIRECTORY | fio::OPEN_RIGHT_READABLE,
                        MODE_TYPE_DIRECTORY,
                        &dir_entry.name,
                        ServerEnd::new(subdir_server.into_channel()),
                    );
                    if let Err(e) = open_dir_result {
                        let error = Err(Error::ReadDir {
                            name: dir_entry.name,
                            err: anyhow::Error::new(e),
                        });
                        return Some((error, (results, pending)));
                    } else {
                        &subdir
                    }
                };

                let readdir_result = match timeout {
                    Some(timeout_duration) => readdir_with_timeout(dir_ref, timeout_duration).await,
                    None => readdir(&dir_ref).await,
                };
                let subentries = match readdir_result {
                    Ok(subentries) => subentries,
                    Err(e) => {
                        let error = Err(Error::ReadDir {
                            name: dir_entry.name,
                            err: anyhow::Error::new(e),
                        });
                        return Some((error, (results, pending)));
                    }
                };

                // Emit empty directories as a single entry except for the root directory.
                if subentries.is_empty() && !dir_entry.name.is_empty() {
                    return Some((Ok(dir_entry), (results, pending)));
                }

                for subentry in subentries.into_iter() {
                    let subentry = dir_entry.chain(&subentry);
                    if subentry.is_dir() {
                        pending.push_back(subentry);
                    } else {
                        results.push_back(subentry);
                    }
                }
            }
        }
    })
    .boxed()
}

/// Returns a sorted Vec of directory entries contained directly in the given directory proxy. The
/// returned entries will not include "." or nodes from any subdirectories.
pub async fn readdir(dir: &DirectoryProxy) -> Result<Vec<DirEntry>, Error> {
    let status = dir.rewind().await.map_err(|e| Error::Fidl("rewind", e))?;
    zx::Status::ok(status).map_err(Error::Rewind)?;

    let mut entries = vec![];

    loop {
        let (status, buf) =
            dir.read_dirents(MAX_BUF).await.map_err(|e| Error::Fidl("read_dirents", e))?;
        zx::Status::ok(status).map_err(Error::ReadDirents)?;

        if buf.is_empty() {
            break;
        }

        for entry in parse_dir_entries(&buf) {
            let entry = entry.map_err(Error::DecodeDirent)?;
            if entry.name != "." {
                entries.push(entry);
            }
        }
    }

    entries.sort_unstable();

    Ok(entries)
}

/// Returns a sorted Vec of directory entries contained directly in the given directory proxy. The
/// returned entries will not include "." or nodes from any subdirectories. Timeouts if the read
/// takes longer than the given `timeout` duration.
pub async fn readdir_with_timeout(
    dir: &DirectoryProxy,
    timeout: zx::Duration,
) -> Result<Vec<DirEntry>, Error> {
    readdir(&dir).on_timeout(timeout.after_now(), || Err(Error::Timeout)).await
}

/// Returns `true` if an entry with the specified name exists in the given directory.
pub async fn dir_contains(dir: &DirectoryProxy, name: &str) -> Result<bool, Error> {
    Ok(readdir(&dir).await?.iter().any(|e| e.name == name))
}

/// Returns `true` if an entry with the specified name exists in the given directory.
///
/// Timesout if reading the directory's entries takes longer than the given `timeout`
/// duration.
pub async fn dir_contains_with_timeout(
    dir: &DirectoryProxy,
    name: &str,
    timeout: zx::Duration,
) -> Result<bool, Error> {
    Ok(readdir_with_timeout(&dir, timeout).await?.iter().any(|e| e.name == name))
}

fn parse_dir_entries(mut buf: &[u8]) -> Vec<Result<DirEntry, DecodeDirentError>> {
    #[repr(C, packed)]
    struct Dirent {
        /// The inode number of the entry.
        _ino: u64,
        /// The length of the filename located after this entry.
        size: u8,
        /// The type of the entry. One of the `fio::DIRENT_TYPE_*` constants.
        kind: u8,
        // The unterminated name of the entry.  Length is the `size` field above.
        // char name[0],
    }
    const DIRENT_SIZE: usize = mem::size_of::<Dirent>();

    let mut entries = vec![];

    while !buf.is_empty() {
        // Don't read past the end of the buffer.
        if DIRENT_SIZE > buf.len() {
            entries.push(Err(DecodeDirentError::BufferOverrun));
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
                entries.push(Err(DecodeDirentError::BufferOverrun));
                return entries;
            }

            // Advance to the next entry.
            buf = &rest[size..];
            match String::from_utf8(rest[..size].to_vec()) {
                Ok(name) => Ok(DirEntry { name, kind: kind.into() }),
                Err(err) => Err(DecodeDirentError::InvalidUtf8(err.utf8_error())),
            }
        };

        entries.push(entry);
    }

    entries
}

const DIR_FLAGS: u32 =
    fio::OPEN_FLAG_DIRECTORY | fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE;

/// Removes a directory and all of its children. `name` must be a subdirectory of `root_dir`.
///
/// The async analogue of `std::fs::remove_dir_all`.
pub async fn remove_dir_recursive(root_dir: &DirectoryProxy, name: &str) -> Result<(), Error> {
    let (dir, dir_server) =
        fidl::endpoints::create_proxy::<DirectoryMarker>().expect("failed to create proxy");
    root_dir
        .open(DIR_FLAGS, MODE_TYPE_DIRECTORY, name, ServerEnd::new(dir_server.into_channel()))
        .map_err(|e| Error::Fidl("open", e))?;
    remove_dir_contents(dir).await?;
    let s = root_dir.unlink(name).await.map_err(|e| Error::Fidl("unlink", e))?;
    zx::Status::ok(s).map_err(Error::Unlink)?;
    Ok(())
}

// Returns a `BoxFuture` instead of being async because async doesn't support recursion.
fn remove_dir_contents(dir: DirectoryProxy) -> BoxFuture<'static, Result<(), Error>> {
    let fut = async move {
        for dirent in readdir(&dir).await? {
            match dirent.kind {
                DirentKind::Directory => {
                    let (subdir, subdir_server) =
                        fidl::endpoints::create_proxy::<DirectoryMarker>()
                            .expect("failed to create proxy");
                    dir.open(
                        DIR_FLAGS,
                        MODE_TYPE_DIRECTORY,
                        &dirent.name,
                        ServerEnd::new(subdir_server.into_channel()),
                    )
                    .map_err(|e| Error::Fidl("open", e))?;
                    remove_dir_contents(subdir).await?;
                }
                _ => {}
            }
            let s = dir.unlink(&dirent.name).await.map_err(|e| Error::Fidl("unlink", e))?;
            zx::Status::ok(s).map_err(Error::Unlink)?;
        }
        Ok(())
    };
    Box::pin(fut)
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        anyhow::Context as _,
        fuchsia_async as fasync,
        fuchsia_zircon::DurationNum,
        futures::{channel::oneshot, stream::StreamExt},
        io_util, pin_utils,
        proptest::prelude::*,
        std::{path::Path, task::Poll},
        tempfile::TempDir,
        vfs::{
            directory::entry::DirectoryEntry, execution_scope::ExecutionScope,
            file::pcb::read_only_static, pseudo_directory,
        },
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
            fio::DIRENT_TYPE_FILE,
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
            fio::DIRENT_TYPE_FILE,
            // name (a lonely continuation byte)
            0x80,
            // entry 1
            // ino
            2, 0, 0, 0, 0, 0, 0, 0,
            // name length
            4,
            // type
            fio::DIRENT_TYPE_FILE,
            // name
            'o' as u8, 'k' as u8, 'a' as u8, 'y' as u8,
        ];

        let expected_err = std::str::from_utf8(&[0x80]).unwrap_err();

        assert_eq!(
            parse_dir_entries(buf),
            vec![
                Err(DecodeDirentError::InvalidUtf8(expected_err)),
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
            fio::DIRENT_TYPE_FILE,
            // name
            't' as u8, 'e' as u8, 's' as u8, 't' as u8,
        ];

        assert_eq!(parse_dir_entries(buf), vec![Err(DecodeDirentError::BufferOverrun)]);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_readdir() {
        let (dir_client, server_end) = fidl::endpoints::create_proxy::<DirectoryMarker>().unwrap();
        let dir = pseudo_directory! {
            "afile" => read_only_static(""),
            "zzz" => read_only_static(""),
            "subdir" => pseudo_directory! {
                "ignored" => read_only_static(""),
            },
        };
        let scope = ExecutionScope::new();
        dir.open(
            scope,
            fio::OPEN_FLAG_DIRECTORY | fio::OPEN_RIGHT_READABLE,
            0,
            vfs::path::Path::empty(),
            ServerEnd::new(server_end.into_channel()),
        );

        // run twice to check that seek offset is properly reset before reading the directory
        for _ in 0..2 {
            let entries = readdir(&dir_client).await.expect("readdir to succeed");
            assert_eq!(
                entries,
                vec![
                    build_direntry("afile", DirentKind::File),
                    build_direntry("subdir", DirentKind::Directory),
                    build_direntry("zzz", DirentKind::File),
                ]
            );
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_dir_contains() -> Result<(), anyhow::Error> {
        let (dir_client, server_end) = fidl::endpoints::create_proxy::<DirectoryMarker>().unwrap();
        let dir = pseudo_directory! {
            "afile" => read_only_static(""),
            "zzz" => read_only_static(""),
            "subdir" => pseudo_directory! {
                "ignored" => read_only_static(""),
            },
        };
        let scope = ExecutionScope::new();
        let () = dir.open(
            scope,
            fio::OPEN_FLAG_DIRECTORY | fio::OPEN_RIGHT_READABLE,
            0,
            vfs::path::Path::empty(),
            ServerEnd::new(server_end.into_channel()),
        );

        for file in &["afile", "zzz", "subdir"] {
            assert!(dir_contains(&dir_client, file)
                .await
                .with_context(|| format!("error checking if dir contains {}", file))?);
        }

        assert!(!dir_contains(&dir_client, "notin")
            .await
            .context("error checking if dir contains notin")?);

        Ok(())
    }

    #[test]
    fn test_dir_contains_with_timeout_err() {
        let mut executor = fasync::Executor::new_with_fake_time().unwrap();
        executor.set_fake_time(fasync::Time::from_nanos(0));

        let fut = async move {
            let tempdir = TempDir::new().expect("failed to create tmp dir");
            let dir = create_nested_dir(&tempdir).await;
            dir_contains_with_timeout(&dir, "notin", 0.nanos()).await
        };

        pin_utils::pin_mut!(fut);
        let mut i = 1;
        let result = loop {
            executor.wake_main_future();
            match executor.run_one_step(&mut fut) {
                Some(Poll::Ready(x)) => break x,
                None => panic!("Executor stalled"),
                Some(Poll::Pending) => {
                    executor.set_fake_time(fasync::Time::from_nanos(10 * i));
                    i += 1;
                }
            }
        };

        matches::assert_matches!(result, Err(Error::Timeout));
    }

    #[test]
    fn test_dir_contains_with_timeout_ok() {
        let mut executor = fasync::Executor::new_with_fake_time().unwrap();
        executor.set_fake_time(fasync::Time::from_nanos(0));

        let fut = async move {
            let tempdir = TempDir::new().expect("failed to create tmp dir");
            let dir = create_nested_dir(&tempdir).await;
            let first = dir_contains_with_timeout(&dir, "notin", 1.nanos())
                .await
                .context("error checking dir contains notin");
            let second = dir_contains_with_timeout(&dir, "a", 1.nanos())
                .await
                .context("error checking dir contains a");
            (first, second)
        };

        pin_utils::pin_mut!(fut);
        let result = loop {
            executor.wake_main_future();
            match executor.run_one_step(&mut fut) {
                Some(Poll::Ready(x)) => break x,
                None => panic!("Executor stalled"),
                Some(Poll::Pending) => {}
            }
        };

        matches::assert_matches!(result, (Ok(false), Ok(true)));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_readdir_recursive() {
        let tempdir = TempDir::new().expect("failed to create tmp dir");
        let dir = create_nested_dir(&tempdir).await;
        // run twice to check that seek offset is properly reset before reading the directory
        for _ in 0..2 {
            let (tx, rx) = oneshot::channel();
            let clone_dir =
                io_util::clone_directory(&dir, fio::CLONE_FLAG_SAME_RIGHTS).expect("clone dir");
            fasync::Task::spawn(async move {
                let entries = readdir_recursive(&clone_dir, None)
                    .collect::<Vec<Result<DirEntry, Error>>>()
                    .await
                    .into_iter()
                    .collect::<Result<Vec<_>, _>>()
                    .expect("readdir_recursive to succeed");
                tx.send(entries).expect("Unable to send entries");
            })
            .detach();
            let entries = rx.await.expect("Receive entrie");
            assert_eq!(
                entries,
                vec![
                    build_direntry("a", DirentKind::File),
                    build_direntry("b", DirentKind::File),
                    build_direntry("emptydir", DirentKind::Directory),
                    build_direntry("subdir/a", DirentKind::File),
                    build_direntry("subdir/subsubdir/a", DirentKind::File),
                    build_direntry("subdir/subsubdir/emptydir", DirentKind::Directory),
                ]
            );
        }
    }

    #[test]
    fn test_readdir_recursive_timeout() {
        let mut executor = fasync::Executor::new_with_fake_time().unwrap();
        executor.set_fake_time(fasync::Time::from_nanos(0));

        let fut = async move {
            let tempdir = TempDir::new().expect("failed to create tmp dir");
            let dir = create_nested_dir(&tempdir).await;
            readdir_recursive(&dir, Some(0.nanos()))
                .collect::<Vec<Result<DirEntry, Error>>>()
                .await
                .into_iter()
                .collect::<Result<Vec<_>, _>>()
        };

        pin_utils::pin_mut!(fut);
        let mut i = 1;
        let result = loop {
            executor.wake_main_future();
            match executor.run_one_step(&mut fut) {
                Some(Poll::Ready(x)) => break x,
                None => panic!("Executor stalled"),
                Some(Poll::Pending) => {
                    executor.set_fake_time(fasync::Time::from_nanos(10 * i));
                    i += 1;
                }
            }
        };

        assert!(result.is_err());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_remove_dir_recursive() {
        {
            let tempdir = TempDir::new().expect("failed to create tmp dir");
            let dir = create_nested_dir(&tempdir).await;
            remove_dir_recursive(&dir, "emptydir").await.expect("remove_dir_recursive to succeed");
            let entries = readdir_recursive(&dir, None)
                .collect::<Vec<Result<DirEntry, Error>>>()
                .await
                .into_iter()
                .collect::<Result<Vec<_>, _>>()
                .expect("readdir_recursive to succeed");
            assert_eq!(
                entries,
                vec![
                    build_direntry("a", DirentKind::File),
                    build_direntry("b", DirentKind::File),
                    build_direntry("subdir/a", DirentKind::File),
                    build_direntry("subdir/subsubdir/a", DirentKind::File),
                    build_direntry("subdir/subsubdir/emptydir", DirentKind::Directory),
                ]
            );
        }
        {
            let tempdir = TempDir::new().expect("failed to create tmp dir");
            let dir = create_nested_dir(&tempdir).await;
            remove_dir_recursive(&dir, "subdir").await.expect("remove_dir_recursive to succeed");
            let entries = readdir_recursive(&dir, None)
                .collect::<Vec<Result<DirEntry, Error>>>()
                .await
                .into_iter()
                .collect::<Result<Vec<_>, _>>()
                .expect("readdir_recursive to succeed");
            assert_eq!(
                entries,
                vec![
                    build_direntry("a", DirentKind::File),
                    build_direntry("b", DirentKind::File),
                    build_direntry("emptydir", DirentKind::Directory),
                ]
            );
        }
        {
            let tempdir = TempDir::new().expect("failed to create tmp dir");
            let dir = create_nested_dir(&tempdir).await;
            let subdir = io_util::open_directory(
                &dir,
                &Path::new("subdir"),
                fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE,
            )
            .expect("could not open subdir");
            remove_dir_recursive(&subdir, "subsubdir")
                .await
                .expect("remove_dir_recursive to succeed");
            let entries = readdir_recursive(&dir, None)
                .collect::<Vec<Result<DirEntry, Error>>>()
                .await
                .into_iter()
                .collect::<Result<Vec<_>, _>>()
                .expect("readdir_recursive to succeed");
            assert_eq!(
                entries,
                vec![
                    build_direntry("a", DirentKind::File),
                    build_direntry("b", DirentKind::File),
                    build_direntry("emptydir", DirentKind::Directory),
                    build_direntry("subdir/a", DirentKind::File),
                ]
            );
        }
        {
            let tempdir = TempDir::new().expect("failed to create tmp dir");
            let dir = create_nested_dir(&tempdir).await;
            let subsubdir = io_util::open_directory(
                &dir,
                &Path::new("subdir/subsubdir"),
                fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE,
            )
            .expect("could not open subsubdir");
            remove_dir_recursive(&subsubdir, "emptydir")
                .await
                .expect("remove_dir_recursive to succeed");
            let entries = readdir_recursive(&dir, None)
                .collect::<Vec<Result<DirEntry, Error>>>()
                .await
                .into_iter()
                .collect::<Result<Vec<_>, _>>()
                .expect("readdir_recursive to succeed");
            assert_eq!(
                entries,
                vec![
                    build_direntry("a", DirentKind::File),
                    build_direntry("b", DirentKind::File),
                    build_direntry("emptydir", DirentKind::Directory),
                    build_direntry("subdir/a", DirentKind::File),
                    build_direntry("subdir/subsubdir/a", DirentKind::File),
                ]
            );
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_remove_dir_recursive_errors() {
        {
            let tempdir = TempDir::new().expect("failed to create tmp dir");
            let dir = create_nested_dir(&tempdir).await;
            let res = remove_dir_recursive(&dir, "baddir").await;
            let res = res.expect_err("remove_dir did not fail");
            match res {
                Error::Fidl("rewind", fidl_error) if fidl_error.is_closed() => {}
                _ => panic!("unexpected error {:?}", res),
            }
        }
        {
            let tempdir = TempDir::new().expect("failed to create tmp dir");
            let dir = create_nested_dir(&tempdir).await;
            let res = remove_dir_recursive(&dir, ".").await;
            let expected: Result<(), Error> = Err(Error::Unlink(zx::Status::UNAVAILABLE));
            assert_eq!(format!("{:?}", res), format!("{:?}", expected));
        }
    }

    async fn create_nested_dir(tempdir: &TempDir) -> DirectoryProxy {
        let dir = io_util::open_directory_in_namespace(
            tempdir.path().to_str().unwrap(),
            fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE,
        )
        .expect("could not open tmp dir");
        io_util::create_sub_directories(&dir, Path::new("emptydir"))
            .expect("failed to create emptydir");
        io_util::create_sub_directories(&dir, Path::new("subdir/subsubdir/emptydir"))
            .expect("failed to create subdir/subsubdir/emptydir");
        create_file(&dir, "a").await;
        create_file(&dir, "b").await;
        create_file(&dir, "subdir/a").await;
        create_file(&dir, "subdir/subsubdir/a").await;
        dir
    }

    async fn create_file(dir: &DirectoryProxy, path: &str) {
        io_util::open_file(
            dir,
            Path::new(path),
            fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE | fio::OPEN_FLAG_CREATE,
        )
        .expect(&format!("failed to create {}", path));
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
