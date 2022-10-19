// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Utility functions for fuchsia.io directories.

use {
    crate::node::{self, CloneError, CloseError, OpenError, RenameError},
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io as fio,
    fuchsia_async::{Duration, DurationExt, TimeoutExt},
    fuchsia_zircon_status as zx_status,
    futures::future::BoxFuture,
    futures::stream::{self, BoxStream, StreamExt},
    std::{collections::VecDeque, mem, str::Utf8Error},
    thiserror::Error,
};

/// Error returned by fuchsia_fs::directory library.
#[derive(Debug, Error)]
pub enum Error {
    #[error("a directory entry could not be decoded: {:?}", _0)]
    DecodeDirent(DecodeDirentError),

    #[error("fidl error during {}: {:?}", _0, _1)]
    Fidl(&'static str, fidl::Error),

    #[error("`read_dirents` failed with status {:?}", _0)]
    ReadDirents(zx_status::Status),

    #[error("`unlink` failed with status {:?}", _0)]
    Unlink(zx_status::Status),

    #[error("timeout")]
    Timeout,

    #[error("`rewind` failed with status {:?}", _0)]
    Rewind(zx_status::Status),

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

/// Opens the given `path` from the current namespace as a [`DirectoryProxy`].
///
/// The target is assumed to implement fuchsia.io.Directory but this isn't verified. To connect to
/// a filesystem node which doesn't implement fuchsia.io.Directory, use the functions in
/// [`fuchsia_component::client`] instead.
///
/// If the namespace path doesn't exist, or we fail to make the channel pair, this returns an
/// error. However, if incorrect flags are sent, or if the rest of the path sent to the filesystem
/// server doesn't exist, this will still return success. Instead, the returned DirectoryProxy
/// channel pair will be closed with an epitaph.
#[cfg(target_os = "fuchsia")]
pub fn open_in_namespace(
    path: &str,
    flags: fio::OpenFlags,
) -> Result<fio::DirectoryProxy, OpenError> {
    let (node, request) = fidl::endpoints::create_proxy().map_err(OpenError::CreateProxy)?;
    open_channel_in_namespace(path, flags, request)?;
    Ok(node)
}

/// Asynchronously opens the given [`path`] in the current namespace, serving the connection over
/// [`request`]. Once the channel is connected, any calls made prior are serviced.
///
/// The target is assumed to implement fuchsia.io.Directory but this isn't verified. To connect to
/// a filesystem node which doesn't implement fuchsia.io.Directory, use the functions in
/// [`fuchsia_component::client`] instead.
///
/// If the namespace path doesn't exist, this returns an error. However, if incorrect flags are
/// sent, or if the rest of the path sent to the filesystem server doesn't exist, this will still
/// return success. Instead, the [`request`] channel will be closed with an epitaph.
#[cfg(target_os = "fuchsia")]
pub fn open_channel_in_namespace(
    path: &str,
    flags: fio::OpenFlags,
    request: fidl::endpoints::ServerEnd<fio::DirectoryMarker>,
) -> Result<(), OpenError> {
    let flags = flags | fio::OpenFlags::DIRECTORY;
    let namespace = fdio::Namespace::installed().map_err(OpenError::Namespace)?;
    namespace.open(path, flags, request.into_channel()).map_err(OpenError::Namespace)
}

/// Opens the given `path` from the given `parent` directory as a [`DirectoryProxy`]. The target is
/// not verified to be any particular type and may not implement the fuchsia.io.Directory protocol.
pub fn open_directory_no_describe(
    parent: &fio::DirectoryProxy,
    path: &str,
    flags: fio::OpenFlags,
) -> Result<fio::DirectoryProxy, OpenError> {
    let (dir, server_end) =
        fidl::endpoints::create_proxy::<fio::DirectoryMarker>().map_err(OpenError::CreateProxy)?;

    let flags = flags | fio::OpenFlags::DIRECTORY;
    let mode = fio::MODE_TYPE_DIRECTORY;

    parent
        .open(flags, mode, path, ServerEnd::new(server_end.into_channel()))
        .map_err(OpenError::SendOpenRequest)?;

    Ok(dir)
}

/// Opens the given `path` from given `parent` directory as a [`DirectoryProxy`], verifying that
/// the target implements the fuchsia.io.Directory protocol.
pub async fn open_directory(
    parent: &fio::DirectoryProxy,
    path: &str,
    flags: fio::OpenFlags,
) -> Result<fio::DirectoryProxy, OpenError> {
    let (dir, server_end) =
        fidl::endpoints::create_proxy::<fio::DirectoryMarker>().map_err(OpenError::CreateProxy)?;

    let flags = flags | fio::OpenFlags::DIRECTORY | fio::OpenFlags::DESCRIBE;
    let mode = fio::MODE_TYPE_DIRECTORY;

    parent
        .open(flags, mode, path, ServerEnd::new(server_end.into_channel()))
        .map_err(OpenError::SendOpenRequest)?;

    // wait for the directory to open and report success.
    node::verify_directory_describe_event(dir).await
}

/// Creates a directory named `path` within the `parent` directory.
pub async fn create_directory(
    parent: &fio::DirectoryProxy,
    path: &str,
    flags: fio::OpenFlags,
) -> Result<fio::DirectoryProxy, OpenError> {
    let (dir, server_end) =
        fidl::endpoints::create_proxy::<fio::DirectoryMarker>().map_err(OpenError::CreateProxy)?;

    // NB: POSIX does not allow open(2) to create dirs, but fuchsia.io does not have an equivalent
    // of mkdir(2), so on Fuchsia we're expected to call open on a DirectoryMarker with (flags &
    // OPEN_FLAG_CREATE) set.
    // (mode & MODE_TYPE_DIRECTORY) is also required, although it is redundant (the fact that we
    // opened a DirectoryMarker is the main way that the underlying filesystem understands our
    // intention.)
    let flags =
        flags | fio::OpenFlags::CREATE | fio::OpenFlags::DIRECTORY | fio::OpenFlags::DESCRIBE;
    let mode = fio::MODE_TYPE_DIRECTORY;

    parent
        .open(flags, mode, path, ServerEnd::new(server_end.into_channel()))
        .map_err(OpenError::SendOpenRequest)?;

    // wait for the directory to open and report success.
    node::verify_directory_describe_event(dir).await
}

/// Creates a directory named `path` (including all segments leading up to the terminal segment)
/// within the `parent` directory.  Returns a connection to the terminal directory.
pub async fn create_directory_recursive(
    parent: &fio::DirectoryProxy,
    path: &str,
    flags: fio::OpenFlags,
) -> Result<fio::DirectoryProxy, OpenError> {
    let components = path.split('/');
    let mut dir = None;
    for part in components {
        dir = Some({
            let dir_ref = match dir.as_ref() {
                Some(r) => r,
                None => parent,
            };
            create_directory(dir_ref, part, flags).await?
        })
    }
    dir.ok_or(OpenError::OpenError(zx_status::Status::INVALID_ARGS))
}

/// Opens the given `path` from the given `parent` directory as a [`FileProxy`]. The target is not
/// verified to be any particular type and may not implement the fuchsia.io.File protocol.
pub fn open_file_no_describe(
    parent: &fio::DirectoryProxy,
    path: &str,
    flags: fio::OpenFlags,
) -> Result<fio::FileProxy, OpenError> {
    let (file, server_end) =
        fidl::endpoints::create_proxy::<fio::FileMarker>().map_err(OpenError::CreateProxy)?;

    let mode = fio::MODE_TYPE_FILE;

    parent
        .open(flags, mode, path, ServerEnd::new(server_end.into_channel()))
        .map_err(OpenError::SendOpenRequest)?;

    Ok(file)
}

/// Opens the given `path` from given `parent` directory as a [`FileProxy`], verifying that the
/// target implements the fuchsia.io.File protocol.
pub async fn open_file(
    parent: &fio::DirectoryProxy,
    path: &str,
    flags: fio::OpenFlags,
) -> Result<fio::FileProxy, OpenError> {
    let (file, server_end) =
        fidl::endpoints::create_proxy::<fio::FileMarker>().map_err(OpenError::CreateProxy)?;

    let flags = flags | fio::OpenFlags::DESCRIBE;
    let mode = fio::MODE_TYPE_FILE;

    parent
        .open(flags, mode, path, ServerEnd::new(server_end.into_channel()))
        .map_err(OpenError::SendOpenRequest)?;

    // wait for the file to open and report success.
    node::verify_file_describe_event(file).await
}

/// Opens the given `path` from the given `parent` directory as a [`NodeProxy`], verifying that the
/// target implements the fuchsia.io.Node protocol.
pub async fn open_node(
    parent: &fio::DirectoryProxy,
    path: &str,
    flags: fio::OpenFlags,
    mode: u32,
) -> Result<fio::NodeProxy, OpenError> {
    let (file, server_end) =
        fidl::endpoints::create_proxy::<fio::NodeMarker>().map_err(OpenError::CreateProxy)?;

    let flags = flags | fio::OpenFlags::DESCRIBE;

    parent
        .open(flags, mode, path, ServerEnd::new(server_end.into_channel()))
        .map_err(OpenError::SendOpenRequest)?;

    // wait for the file to open and report success.
    node::verify_node_describe_event(file).await
}

/// Opens the given `path` from the given `parent` directory as a [`NodeProxy`]. The target is not
/// verified to be any particular type and may not implement the fuchsia.io.Node protocol.
pub fn open_node_no_describe(
    parent: &fio::DirectoryProxy,
    path: &str,
    flags: fio::OpenFlags,
    mode: u32,
) -> Result<fio::NodeProxy, OpenError> {
    let (file, server_end) =
        fidl::endpoints::create_proxy::<fio::NodeMarker>().map_err(OpenError::CreateProxy)?;

    parent
        .open(flags, mode, path, ServerEnd::new(server_end.into_channel()))
        .map_err(OpenError::SendOpenRequest)?;

    Ok(file)
}

/// Opens a new connection to the given directory using `flags` if provided, or
/// `fidl_fuchsia_io::OpenFlags::SAME_RIGHTS` otherwise.
pub fn clone_no_describe(
    dir: &fio::DirectoryProxy,
    flags: Option<fio::OpenFlags>,
) -> Result<fio::DirectoryProxy, CloneError> {
    let (clone, server_end) = fidl::endpoints::create_proxy().map_err(CloneError::CreateProxy)?;
    clone_onto_no_describe(dir, flags, server_end)?;
    Ok(clone)
}

/// Opens a new connection to the given directory onto the given server end using `flags` if
/// provided, or `fidl_fuchsia_io::OpenFlags::SAME_RIGHTS` otherwise.
pub fn clone_onto_no_describe(
    dir: &fio::DirectoryProxy,
    flags: Option<fio::OpenFlags>,
    request: ServerEnd<fio::DirectoryMarker>,
) -> Result<(), CloneError> {
    let node_request = ServerEnd::new(request.into_channel());
    let flags = flags.unwrap_or(fio::OpenFlags::CLONE_SAME_RIGHTS);

    dir.clone(flags, node_request).map_err(CloneError::SendCloneRequest)?;
    Ok(())
}

/// Gracefully closes the directory proxy from the remote end.
pub async fn close(dir: fio::DirectoryProxy) -> Result<(), CloseError> {
    let result = dir.close().await.map_err(CloseError::SendCloseRequest)?;
    result.map_err(|s| CloseError::CloseError(zx_status::Status::from_raw(s)))
}

/// Create a randomly named file in the given directory with the given prefix, and return its path
/// and `FileProxy`. `prefix` may contain "/".
pub async fn create_randomly_named_file(
    dir: &fio::DirectoryProxy,
    prefix: &str,
    flags: fio::OpenFlags,
) -> Result<(String, fio::FileProxy), OpenError> {
    use rand::{
        distributions::{Alphanumeric, DistString as _},
        SeedableRng as _,
    };
    let mut rng = rand::rngs::SmallRng::from_entropy();

    let flags = flags | fio::OpenFlags::CREATE | fio::OpenFlags::CREATE_IF_ABSENT;

    loop {
        let random_string = Alphanumeric.sample_string(&mut rng, 6);
        let path = prefix.to_string() + &random_string;

        match open_file(dir, &path, flags).await {
            Ok(file) => return Ok((path, file)),
            Err(OpenError::OpenError(zx_status::Status::ALREADY_EXISTS)) => {}
            Err(err) => return Err(err),
        }
    }
}

// Split the given path under the directory into parent and file name, and open the parent directory
// if the path contains "/".
async fn split_path<'a>(
    dir: &fio::DirectoryProxy,
    path: &'a str,
) -> Result<(Option<fio::DirectoryProxy>, &'a str), OpenError> {
    match path.rsplit_once('/') {
        Some((parent, name)) => {
            let proxy = open_directory(dir, parent, fio::OpenFlags::RIGHT_WRITABLE).await?;
            Ok((Some(proxy), name))
        }
        None => Ok((None, path)),
    }
}

/// Rename `src` to `dst` under the given directory, `src` and `dst` may contain "/".
pub async fn rename(dir: &fio::DirectoryProxy, src: &str, dst: &str) -> Result<(), RenameError> {
    let (src_parent, src_filename) = split_path(dir, src).await?;
    let src_parent = src_parent.as_ref().unwrap_or(dir);
    let (dst_parent, dst_filename) = split_path(dir, dst).await?;
    let dst_parent = dst_parent.as_ref().unwrap_or(dir);
    let (status, dst_parent_dir_token) =
        dst_parent.get_token().await.map_err(RenameError::SendGetTokenRequest)?;
    zx_status::Status::ok(status).map_err(RenameError::GetTokenError)?;
    let event = fidl::Event::from(dst_parent_dir_token.ok_or(RenameError::NoHandleError)?);
    src_parent
        .rename(src_filename, event, dst_filename)
        .await
        .map_err(RenameError::SendRenameRequest)?
        .map_err(|s| RenameError::RenameError(zx_status::Status::from_raw(s)))
}

pub use fio::DirentType as DirentKind;

/// A directory entry.
#[derive(Clone, Eq, Ord, PartialOrd, PartialEq, Debug)]
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
    dir: &fio::DirectoryProxy,
    timeout: Option<Duration>,
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
                    match fidl::endpoints::create_proxy::<fio::DirectoryMarker>() {
                        Ok((subdir, server)) => (subdir, server),
                        Err(e) => {
                            return Some((Err(Error::Fidl("create_proxy", e)), (results, pending)))
                        }
                    };
                let dir_ref = if dir_entry.is_root() {
                    dir
                } else {
                    let open_dir_result = dir.open(
                        fio::OpenFlags::DIRECTORY | fio::OpenFlags::RIGHT_READABLE,
                        fio::MODE_TYPE_DIRECTORY,
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
pub async fn readdir(dir: &fio::DirectoryProxy) -> Result<Vec<DirEntry>, Error> {
    let status = dir.rewind().await.map_err(|e| Error::Fidl("rewind", e))?;
    zx_status::Status::ok(status).map_err(Error::Rewind)?;

    let mut entries = vec![];

    loop {
        let (status, buf) =
            dir.read_dirents(fio::MAX_BUF).await.map_err(|e| Error::Fidl("read_dirents", e))?;
        zx_status::Status::ok(status).map_err(Error::ReadDirents)?;

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
    dir: &fio::DirectoryProxy,
    timeout: Duration,
) -> Result<Vec<DirEntry>, Error> {
    readdir(&dir).on_timeout(timeout.after_now(), || Err(Error::Timeout)).await
}

/// Returns `true` if an entry with the specified name exists in the given directory.
pub async fn dir_contains(dir: &fio::DirectoryProxy, name: &str) -> Result<bool, Error> {
    Ok(readdir(&dir).await?.iter().any(|e| e.name == name))
}

/// Returns `true` if an entry with the specified name exists in the given directory.
///
/// Timesout if reading the directory's entries takes longer than the given `timeout`
/// duration.
pub async fn dir_contains_with_timeout(
    dir: &fio::DirectoryProxy,
    name: &str,
    timeout: Duration,
) -> Result<bool, Error> {
    Ok(readdir_with_timeout(&dir, timeout).await?.iter().any(|e| e.name == name))
}

/// Parses the buffer returned by a read_dirents FIDL call.
///
/// Returns either an error or a parsed entry for each entry in the supplied buffer (see
/// read_dirents for the format of this buffer).
pub fn parse_dir_entries(mut buf: &[u8]) -> Vec<Result<DirEntry, DecodeDirentError>> {
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
                Ok(name) => Ok(DirEntry {
                    name,
                    kind: DirentKind::from_primitive(kind).unwrap_or(DirentKind::Unknown),
                }),
                Err(err) => Err(DecodeDirentError::InvalidUtf8(err.utf8_error())),
            }
        };

        entries.push(entry);
    }

    entries
}

const DIR_FLAGS: fio::OpenFlags = fio::OpenFlags::empty()
    .union(fio::OpenFlags::DIRECTORY)
    .union(fio::OpenFlags::RIGHT_READABLE)
    .union(fio::OpenFlags::RIGHT_WRITABLE);

/// Removes a directory and all of its children. `name` must be a subdirectory of `root_dir`.
///
/// The async analogue of `std::fs::remove_dir_all`.
pub async fn remove_dir_recursive(root_dir: &fio::DirectoryProxy, name: &str) -> Result<(), Error> {
    let (dir, dir_server) =
        fidl::endpoints::create_proxy::<fio::DirectoryMarker>().expect("failed to create proxy");
    root_dir
        .open(DIR_FLAGS, fio::MODE_TYPE_DIRECTORY, name, ServerEnd::new(dir_server.into_channel()))
        .map_err(|e| Error::Fidl("open", e))?;
    remove_dir_contents(dir).await?;
    root_dir
        .unlink(
            name,
            fio::UnlinkOptions {
                flags: Some(fio::UnlinkFlags::MUST_BE_DIRECTORY),
                ..fio::UnlinkOptions::EMPTY
            },
        )
        .await
        .map_err(|e| Error::Fidl("unlink", e))?
        .map_err(|s| Error::Unlink(zx_status::Status::from_raw(s)))
}

// Returns a `BoxFuture` instead of being async because async doesn't support recursion.
fn remove_dir_contents(dir: fio::DirectoryProxy) -> BoxFuture<'static, Result<(), Error>> {
    let fut = async move {
        for dirent in readdir(&dir).await? {
            match dirent.kind {
                DirentKind::Directory => {
                    let (subdir, subdir_server) =
                        fidl::endpoints::create_proxy::<fio::DirectoryMarker>()
                            .expect("failed to create proxy");
                    dir.open(
                        DIR_FLAGS,
                        fio::MODE_TYPE_DIRECTORY,
                        &dirent.name,
                        ServerEnd::new(subdir_server.into_channel()),
                    )
                    .map_err(|e| Error::Fidl("open", e))?;
                    remove_dir_contents(subdir).await?;
                }
                _ => {}
            }
            dir.unlink(&dirent.name, fio::UnlinkOptions::EMPTY)
                .await
                .map_err(|e| Error::Fidl("unlink", e))?
                .map_err(|s| Error::Unlink(zx_status::Status::from_raw(s)))?;
        }
        Ok(())
    };
    Box::pin(fut)
}
#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::write_file,
        anyhow::Context as _,
        assert_matches::assert_matches,
        fuchsia_async as fasync,
        futures::{channel::oneshot, stream::StreamExt},
        proptest::prelude::*,
        tempfile::TempDir,
        vfs::{
            directory::entry::DirectoryEntry,
            execution_scope::ExecutionScope,
            file::vmo::{read_only_static, read_write, simple_init_vmo_with_capacity},
            pseudo_directory,
        },
    };

    const DATA_FILE_CONTENTS: &str = "Hello World!\n";

    #[cfg(target_os = "fuchsia")]
    use fuchsia_zircon::DurationNum;

    #[cfg(target_os = "fuchsia")]
    const LONG_DURATION: Duration = Duration::from_seconds(30);

    #[cfg(not(target_os = "fuchsia"))]
    const LONG_DURATION: Duration = Duration::from_secs(30);

    proptest! {
        #[test]
        fn test_parse_dir_entries_does_not_crash(buf in prop::collection::vec(any::<u8>(), 0..200)) {
            parse_dir_entries(&buf);
        }
    }

    fn open_pkg() -> fio::DirectoryProxy {
        open_in_namespace("/pkg", fio::OpenFlags::RIGHT_READABLE).unwrap()
    }

    fn open_tmp() -> (TempDir, fio::DirectoryProxy) {
        let tempdir = TempDir::new().expect("failed to create tmp dir");
        let proxy = open_in_namespace(
            tempdir.path().to_str().unwrap(),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )
        .unwrap();
        (tempdir, proxy)
    }

    // open_in_namespace

    #[fasync::run_singlethreaded(test)]
    async fn open_in_namespace_opens_real_dir() {
        let exists = open_in_namespace("/pkg", fio::OpenFlags::RIGHT_READABLE).unwrap();
        assert_matches!(close(exists).await, Ok(()));
    }

    #[fasync::run_singlethreaded(test)]
    async fn open_in_namespace_opens_fake_subdir_of_root_namespace_entry() {
        let notfound = open_in_namespace("/pkg/fake", fio::OpenFlags::RIGHT_READABLE).unwrap();
        // The open error is not detected until the proxy is interacted with.
        assert_matches!(close(notfound).await, Err(_));
    }

    #[fasync::run_singlethreaded(test)]
    async fn open_in_namespace_rejects_fake_root_namespace_entry() {
        assert_matches!(
            open_in_namespace("/fake", fio::OpenFlags::RIGHT_READABLE),
            Err(OpenError::Namespace(zx_status::Status::NOT_FOUND))
        );
    }

    // open_directory_no_describe

    #[fasync::run_singlethreaded(test)]
    async fn open_directory_no_describe_opens_real_dir() {
        let pkg = open_pkg();
        let data =
            open_directory_no_describe(&pkg, "data", fio::OpenFlags::RIGHT_READABLE).unwrap();
        close(data).await.unwrap();
    }

    #[fasync::run_singlethreaded(test)]
    async fn open_directory_no_describe_opens_fake_dir() {
        let pkg = open_pkg();
        let fake =
            open_directory_no_describe(&pkg, "fake", fio::OpenFlags::RIGHT_READABLE).unwrap();
        // The open error is not detected until the proxy is interacted with.
        assert_matches!(close(fake).await, Err(_));
    }

    // open_directory

    #[fasync::run_singlethreaded(test)]
    async fn open_directory_opens_real_dir() {
        let pkg = open_pkg();
        let data = open_directory(&pkg, "data", fio::OpenFlags::RIGHT_READABLE).await.unwrap();
        close(data).await.unwrap();
    }

    #[fasync::run_singlethreaded(test)]
    async fn open_directory_rejects_fake_dir() {
        let pkg = open_pkg();

        assert_matches!(
            open_directory(&pkg, "fake", fio::OpenFlags::RIGHT_READABLE).await,
            Err(OpenError::OpenError(zx_status::Status::NOT_FOUND))
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn open_directory_rejects_file() {
        let pkg = open_pkg();

        assert_matches!(
            open_directory(&pkg, "data/file", fio::OpenFlags::RIGHT_READABLE).await,
            Err(OpenError::OpenError(zx_status::Status::NOT_DIR))
        );
    }

    // create_directory

    #[fasync::run_singlethreaded(test)]
    async fn create_directory_simple() {
        let (_tmp, proxy) = open_tmp();
        let dir = create_directory(&proxy, "dir", fio::OpenFlags::RIGHT_READABLE).await.unwrap();
        crate::directory::close(dir).await.unwrap();
    }

    #[fasync::run_singlethreaded(test)]
    async fn create_directory_add_file() {
        let (_tmp, proxy) = open_tmp();
        let dir = create_directory(
            &proxy,
            "dir",
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )
        .await
        .unwrap();
        let file = open_file(
            &dir,
            "data",
            fio::OpenFlags::CREATE
                | fio::OpenFlags::CREATE_IF_ABSENT
                | fio::OpenFlags::RIGHT_READABLE,
        )
        .await
        .unwrap();
        crate::file::close(file).await.unwrap();
    }

    #[fasync::run_singlethreaded(test)]
    async fn create_directory_existing_dir_opens() {
        let (_tmp, proxy) = open_tmp();
        let dir = create_directory(&proxy, "dir", fio::OpenFlags::RIGHT_READABLE).await.unwrap();
        crate::directory::close(dir).await.unwrap();
        create_directory(&proxy, "dir", fio::OpenFlags::RIGHT_READABLE).await.unwrap();
    }

    #[fasync::run_singlethreaded(test)]
    async fn create_directory_existing_dir_fails_if_flag_set() {
        let (_tmp, proxy) = open_tmp();
        let dir = create_directory(
            &proxy,
            "dir",
            fio::OpenFlags::CREATE_IF_ABSENT | fio::OpenFlags::RIGHT_READABLE,
        )
        .await
        .unwrap();
        crate::directory::close(dir).await.unwrap();
        assert_matches!(
            create_directory(
                &proxy,
                "dir",
                fio::OpenFlags::CREATE_IF_ABSENT | fio::OpenFlags::RIGHT_READABLE
            )
            .await,
            Err(_)
        );
    }

    // open_file_no_describe

    #[fasync::run_singlethreaded(test)]
    async fn open_file_no_describe_opens_real_file() {
        let pkg = open_pkg();
        let file =
            open_file_no_describe(&pkg, "data/file", fio::OpenFlags::RIGHT_READABLE).unwrap();
        crate::file::close(file).await.unwrap();
    }

    #[fasync::run_singlethreaded(test)]
    async fn open_file_no_describe_opens_fake_file() {
        let pkg = open_pkg();
        let fake =
            open_file_no_describe(&pkg, "data/fake", fio::OpenFlags::RIGHT_READABLE).unwrap();
        // The open error is not detected until the proxy is interacted with.
        assert_matches!(crate::file::close(fake).await, Err(_));
    }

    // open_file

    #[fasync::run_singlethreaded(test)]
    async fn open_file_opens_real_file() {
        let pkg = open_pkg();
        let file = open_file(&pkg, "data/file", fio::OpenFlags::RIGHT_READABLE).await.unwrap();
        assert_eq!(
            file.seek(fio::SeekOrigin::End, 0).await.unwrap(),
            Ok(DATA_FILE_CONTENTS.len() as u64),
        );
        crate::file::close(file).await.unwrap();
    }

    #[fasync::run_singlethreaded(test)]
    async fn open_file_rejects_fake_file() {
        let pkg = open_pkg();

        assert_matches!(
            open_file(&pkg, "data/fake", fio::OpenFlags::RIGHT_READABLE).await,
            Err(OpenError::OpenError(zx_status::Status::NOT_FOUND))
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn open_file_rejects_dir() {
        let pkg = open_pkg();

        assert_matches!(
            open_file(&pkg, "data", fio::OpenFlags::RIGHT_READABLE).await,
            Err(OpenError::UnexpectedNodeKind {
                expected: node::Kind::File,
                actual: node::Kind::Directory,
            })
        );
    }

    #[fasync::run_until_stalled(test)]
    async fn open_file_flags() {
        let example_dir = pseudo_directory! {
            "read_only" => read_only_static("read_only"),
            "read_write" => read_write(
                simple_init_vmo_with_capacity("read_write".as_bytes(), 100)
            ),
        };
        let (example_dir_proxy, example_dir_service) =
            fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
        let scope = ExecutionScope::new();
        example_dir.open(
            scope,
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
            fio::MODE_TYPE_DIRECTORY,
            vfs::path::Path::dot(),
            ServerEnd::new(example_dir_service.into_channel()),
        );

        for (file_name, flags, should_succeed) in vec![
            ("read_only", fio::OpenFlags::RIGHT_READABLE, true),
            ("read_only", fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE, false),
            ("read_only", fio::OpenFlags::RIGHT_WRITABLE, false),
            ("read_write", fio::OpenFlags::RIGHT_READABLE, true),
            ("read_write", fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE, true),
            ("read_write", fio::OpenFlags::RIGHT_WRITABLE, true),
        ] {
            // open_file_no_describe

            let file = open_file_no_describe(&example_dir_proxy, file_name, flags).unwrap();
            match (should_succeed, file.query().await) {
                (true, Ok(_)) => (),
                (false, Err(_)) => continue,
                (true, Err(e)) => {
                    panic!("failed to open when expected success, couldn't describe: {:?}", e)
                }
                (false, Ok(d)) => {
                    panic!("successfully opened when expected failure, could describe: {:?}", d)
                }
            }
            if flags.intersects(fio::OpenFlags::RIGHT_READABLE) {
                assert_eq!(crate::file::read_to_string(&file).await.unwrap(), file_name);
            }
            if flags.intersects(fio::OpenFlags::RIGHT_WRITABLE) {
                let _ = file.seek(fio::SeekOrigin::Start, 0).await.expect("Seek failed!");
                let _: u64 = file
                    .write(b"read_write")
                    .await
                    .unwrap()
                    .map_err(zx_status::Status::from_raw)
                    .unwrap();
            }
            crate::file::close(file).await.unwrap();

            // open_file

            match open_file(&example_dir_proxy, file_name, flags).await {
                Ok(file) if should_succeed => {
                    if flags.intersects(fio::OpenFlags::RIGHT_READABLE) {
                        assert_eq!(crate::file::read_to_string(&file).await.unwrap(), file_name);
                    }
                    if flags.intersects(fio::OpenFlags::RIGHT_WRITABLE) {
                        let _ = file.seek(fio::SeekOrigin::Start, 0).await.expect("Seek failed!");
                        let _: u64 = file
                            .write(b"read_write")
                            .await
                            .unwrap()
                            .map_err(zx_status::Status::from_raw)
                            .unwrap();
                    }
                    crate::file::close(file).await.unwrap();
                }
                Ok(_) => {
                    panic!("successfully opened when expected failure: {:?}", (file_name, flags))
                }
                Err(e) if should_succeed => {
                    panic!("failed to open when expected success: {:?}", (e, file_name, flags))
                }
                Err(_) => {}
            }
        }
    }

    // open_node_no_describe

    #[fasync::run_singlethreaded(test)]
    async fn open_node_no_describe_opens_real_node() {
        let pkg = open_pkg();
        let node = open_node_no_describe(&pkg, "data", fio::OpenFlags::RIGHT_READABLE, 0).unwrap();
        crate::node::close(node).await.unwrap();
    }

    #[fasync::run_singlethreaded(test)]
    async fn open_node_no_describe_opens_fake_node() {
        let pkg = open_pkg();
        let fake = open_node_no_describe(&pkg, "fake", fio::OpenFlags::RIGHT_READABLE, 0).unwrap();
        // The open error is not detected until the proxy is interacted with.
        assert_matches!(crate::node::close(fake).await, Err(_));
    }

    // open_node

    #[fasync::run_singlethreaded(test)]
    async fn open_node_opens_real_node() {
        let pkg = open_pkg();
        let node = open_node(&pkg, "data", fio::OpenFlags::RIGHT_READABLE, 0).await.unwrap();
        crate::node::close(node).await.unwrap();
    }

    #[fasync::run_singlethreaded(test)]
    async fn open_node_opens_fake_node() {
        let pkg = open_pkg();
        // The open error should be detected immediately.
        assert_matches!(open_node(&pkg, "fake", fio::OpenFlags::RIGHT_READABLE, 0).await, Err(_));
    }

    // clone_no_describe

    #[fasync::run_singlethreaded(test)]
    async fn clone_no_describe_no_flags_same_rights() {
        let (dir, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<fio::DirectoryMarker>().unwrap();

        clone_no_describe(&dir, None).unwrap();

        assert_matches!(
            stream.next().await,
            Some(Ok(fio::DirectoryRequest::Clone { flags: fio::OpenFlags::CLONE_SAME_RIGHTS, .. }))
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn clone_no_describe_flags_passed_through() {
        let (dir, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<fio::DirectoryMarker>().unwrap();

        const FLAGS: fio::OpenFlags = fio::OpenFlags::DIRECTORY;

        clone_no_describe(&dir, Some(FLAGS)).unwrap();

        assert_matches!(
            stream.next().await,
            Some(Ok(fio::DirectoryRequest::Clone { flags: FLAGS, .. }))
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn create_randomly_named_file_simple() {
        let (_tmp, proxy) = open_tmp();
        let (path, file) =
            create_randomly_named_file(&proxy, "prefix", fio::OpenFlags::RIGHT_WRITABLE)
                .await
                .unwrap();
        assert!(path.starts_with("prefix"));
        crate::file::close(file).await.unwrap();
    }

    #[fasync::run_singlethreaded(test)]
    async fn create_randomly_named_file_subdir() {
        let (_tmp, proxy) = open_tmp();
        let _subdir =
            create_directory(&proxy, "subdir", fio::OpenFlags::RIGHT_WRITABLE).await.unwrap();
        let (path, file) =
            create_randomly_named_file(&proxy, "subdir/file", fio::OpenFlags::RIGHT_WRITABLE)
                .await
                .unwrap();
        assert!(path.starts_with("subdir/file"));
        crate::file::close(file).await.unwrap();
    }

    #[fasync::run_singlethreaded(test)]
    async fn create_randomly_named_file_no_prefix() {
        let (_tmp, proxy) = open_tmp();
        let (_path, file) = create_randomly_named_file(
            &proxy,
            "",
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )
        .await
        .unwrap();
        crate::file::close(file).await.unwrap();
    }

    #[fasync::run_singlethreaded(test)]
    async fn create_randomly_named_file_error() {
        let pkg = open_pkg();
        assert_matches!(
            create_randomly_named_file(&pkg, "", fio::OpenFlags::empty()).await,
            Err(_)
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn rename_simple() {
        let (tmp, proxy) = open_tmp();
        let (path, file) =
            create_randomly_named_file(&proxy, "", fio::OpenFlags::RIGHT_WRITABLE).await.unwrap();
        crate::file::close(file).await.unwrap();
        rename(&proxy, &path, "new_path").await.unwrap();
        assert!(!tmp.path().join(path).exists());
        assert!(tmp.path().join("new_path").exists());
    }

    #[fasync::run_singlethreaded(test)]
    async fn rename_with_subdir() {
        let (tmp, proxy) = open_tmp();
        let _subdir1 =
            create_directory(&proxy, "subdir1", fio::OpenFlags::RIGHT_WRITABLE).await.unwrap();
        let _subdir2 =
            create_directory(&proxy, "subdir2", fio::OpenFlags::RIGHT_WRITABLE).await.unwrap();
        let (path, file) =
            create_randomly_named_file(&proxy, "subdir1/file", fio::OpenFlags::RIGHT_WRITABLE)
                .await
                .unwrap();
        crate::file::close(file).await.unwrap();
        rename(&proxy, &path, "subdir2/file").await.unwrap();
        assert!(!tmp.path().join(path).exists());
        assert!(tmp.path().join("subdir2/file").exists());
    }

    #[fasync::run_singlethreaded(test)]
    async fn rename_directory() {
        let (tmp, proxy) = open_tmp();
        let dir = create_directory(&proxy, "dir", fio::OpenFlags::RIGHT_WRITABLE).await.unwrap();
        close(dir).await.unwrap();
        rename(&proxy, "dir", "dir2").await.unwrap();
        assert!(!tmp.path().join("dir").exists());
        assert!(tmp.path().join("dir2").exists());
    }

    #[fasync::run_singlethreaded(test)]
    async fn rename_overwrite_existing_file() {
        let (tmp, proxy) = open_tmp();
        std::fs::write(tmp.path().join("foo"), b"foo").unwrap();
        std::fs::write(tmp.path().join("bar"), b"bar").unwrap();
        rename(&proxy, "foo", "bar").await.unwrap();
        assert!(!tmp.path().join("foo").exists());
        assert_eq!(std::fs::read_to_string(tmp.path().join("bar")).unwrap(), "foo");
    }

    #[fasync::run_singlethreaded(test)]
    async fn rename_non_existing_src_fails() {
        let (tmp, proxy) = open_tmp();
        assert_matches!(
            rename(&proxy, "foo", "bar").await,
            Err(RenameError::RenameError(zx_status::Status::NOT_FOUND))
        );
        assert!(!tmp.path().join("foo").exists());
        assert!(!tmp.path().join("bar").exists());
    }

    #[fasync::run_singlethreaded(test)]
    async fn rename_to_non_existing_subdir_fails() {
        let (tmp, proxy) = open_tmp();
        std::fs::write(tmp.path().join("foo"), b"foo").unwrap();
        assert_matches!(
            rename(&proxy, "foo", "bar/foo").await,
            Err(RenameError::OpenError(OpenError::OpenError(zx_status::Status::NOT_FOUND)))
        );
        assert!(tmp.path().join("foo").exists());
        assert!(!tmp.path().join("bar/foo").exists());
    }

    #[fasync::run_singlethreaded(test)]
    async fn rename_root_path_fails() {
        let (tmp, proxy) = open_tmp();
        assert_matches!(
            rename(&proxy, "/foo", "bar").await,
            Err(RenameError::OpenError(OpenError::OpenError(zx_status::Status::INVALID_ARGS)))
        );
        assert!(!tmp.path().join("bar").exists());
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
            fio::DirentType::File.into_primitive(),
            // name (a lonely continuation byte)
            0x80,
            // entry 1
            // ino
            2, 0, 0, 0, 0, 0, 0, 0,
            // name length
            4,
            // type
            fio::DirentType::File.into_primitive(),
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
            fio::DirentType::File.into_primitive(),
            // name
            't' as u8, 'e' as u8, 's' as u8, 't' as u8,
        ];

        assert_eq!(parse_dir_entries(buf), vec![Err(DecodeDirentError::BufferOverrun)]);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_readdir() {
        let (dir_client, server_end) =
            fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
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
            fio::OpenFlags::DIRECTORY | fio::OpenFlags::RIGHT_READABLE,
            0,
            vfs::path::Path::dot(),
            ServerEnd::new(server_end.into_channel()),
        );

        // run twice to check that seek offset is properly reset before reading the directory
        for _ in 0..2 {
            let entries = readdir(&dir_client).await.expect("readdir failed");
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
        let (dir_client, server_end) =
            fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
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
            fio::OpenFlags::DIRECTORY | fio::OpenFlags::RIGHT_READABLE,
            0,
            vfs::path::Path::dot(),
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

    #[fasync::run_singlethreaded(test)]
    async fn test_dir_contains_with_timeout() {
        let tempdir = TempDir::new().expect("failed to create tmp dir");
        let dir = create_nested_dir(&tempdir).await;
        let first = dir_contains_with_timeout(&dir, "notin", LONG_DURATION)
            .await
            .context("error checking dir contains notin");
        let second = dir_contains_with_timeout(&dir, "a", LONG_DURATION)
            .await
            .context("error checking dir contains a");
        assert_matches::assert_matches!((first, second), (Ok(false), Ok(true)));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_readdir_recursive() {
        let tempdir = TempDir::new().expect("failed to create tmp dir");
        let dir = create_nested_dir(&tempdir).await;
        // run twice to check that seek offset is properly reset before reading the directory
        for _ in 0..2 {
            let (tx, rx) = oneshot::channel();
            let clone_dir = clone_no_describe(&dir, None).expect("clone dir");
            fasync::Task::spawn(async move {
                let entries = readdir_recursive(&clone_dir, None)
                    .collect::<Vec<Result<DirEntry, Error>>>()
                    .await
                    .into_iter()
                    .collect::<Result<Vec<_>, _>>()
                    .expect("readdir_recursive failed");
                tx.send(entries).expect("sending entries failed");
            })
            .detach();
            let entries = rx.await.expect("receiving entries failed");
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

    #[fasync::run_singlethreaded(test)]
    async fn test_readdir_recursive_timeout_expired() {
        // This test must use a forever-pending server in order to ensure that the timeout
        // triggers before the function under test finishes, even if the timeout is
        // in the past.
        let (dir, _server) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>()
            .expect("could not create proxy");
        let result = readdir_recursive(&dir, Some(0.nanos()))
            .collect::<Vec<Result<DirEntry, Error>>>()
            .await
            .into_iter()
            .collect::<Result<Vec<_>, _>>();
        assert!(result.is_err());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_readdir_recursive_timeout() {
        let tempdir = TempDir::new().expect("failed to create tmp dir");
        let dir = create_nested_dir(&tempdir).await;
        let entries = readdir_recursive(&dir, Some(LONG_DURATION))
            .collect::<Vec<Result<DirEntry, Error>>>()
            .await
            .into_iter()
            .collect::<Result<Vec<_>, _>>()
            .expect("readdir_recursive failed");
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

    #[fasync::run_singlethreaded(test)]
    async fn test_remove_dir_recursive() {
        {
            let tempdir = TempDir::new().expect("failed to create tmp dir");
            let dir = create_nested_dir(&tempdir).await;
            remove_dir_recursive(&dir, "emptydir").await.expect("remove_dir_recursive failed");
            let entries = readdir_recursive(&dir, None)
                .collect::<Vec<Result<DirEntry, Error>>>()
                .await
                .into_iter()
                .collect::<Result<Vec<_>, _>>()
                .expect("readdir_recursive failed");
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
            remove_dir_recursive(&dir, "subdir").await.expect("remove_dir_recursive failed");
            let entries = readdir_recursive(&dir, None)
                .collect::<Vec<Result<DirEntry, Error>>>()
                .await
                .into_iter()
                .collect::<Result<Vec<_>, _>>()
                .expect("readdir_recursive failed");
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
            let subdir = open_directory(
                &dir,
                "subdir",
                fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
            )
            .await
            .expect("could not open subdir");
            remove_dir_recursive(&subdir, "subsubdir").await.expect("remove_dir_recursive failed");
            let entries = readdir_recursive(&dir, None)
                .collect::<Vec<Result<DirEntry, Error>>>()
                .await
                .into_iter()
                .collect::<Result<Vec<_>, _>>()
                .expect("readdir_recursive failed");
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
            let subsubdir = open_directory(
                &dir,
                "subdir/subsubdir",
                fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
            )
            .await
            .expect("could not open subsubdir");
            remove_dir_recursive(&subsubdir, "emptydir")
                .await
                .expect("remove_dir_recursive failed");
            let entries = readdir_recursive(&dir, None)
                .collect::<Vec<Result<DirEntry, Error>>>()
                .await
                .into_iter()
                .collect::<Result<Vec<_>, _>>()
                .expect("readdir_recursive failed");
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
            let expected: Result<(), Error> = Err(Error::Unlink(zx_status::Status::INVALID_ARGS));
            assert_eq!(format!("{:?}", res), format!("{:?}", expected));
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn create_directory_recursive_test() {
        let tempdir = TempDir::new().unwrap();

        let path = "path/to/example/dir";
        let file_name = "example_file_name";
        let data = "file contents";

        let root_dir = open_in_namespace(
            tempdir.path().to_str().unwrap(),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )
        .expect("open_in_namespace failed");

        let sub_dir = create_directory_recursive(
            &root_dir,
            &path,
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )
        .await
        .expect("create_directory_recursive failed");
        let file = open_file(
            &sub_dir,
            &file_name,
            fio::OpenFlags::RIGHT_READABLE
                | fio::OpenFlags::RIGHT_WRITABLE
                | fio::OpenFlags::CREATE,
        )
        .await
        .expect("open_file failed");

        write_file(&file, &data).await.expect("writing to the file failed");

        let contents = std::fs::read_to_string(tempdir.path().join(path).join(file_name))
            .expect("read_to_string failed");
        assert_eq!(&contents, &data, "File contents did not match");
    }

    async fn create_nested_dir(tempdir: &TempDir) -> fio::DirectoryProxy {
        let dir = open_in_namespace(
            tempdir.path().to_str().unwrap(),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )
        .expect("could not open tmp dir");
        create_directory_recursive(
            &dir,
            "emptydir",
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )
        .await
        .expect("failed to create emptydir");
        create_directory_recursive(
            &dir,
            "subdir/subsubdir/emptydir",
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )
        .await
        .expect("failed to create subdir/subsubdir/emptydir");
        create_file(&dir, "a").await;
        create_file(&dir, "b").await;
        create_file(&dir, "subdir/a").await;
        create_file(&dir, "subdir/subsubdir/a").await;
        dir
    }

    async fn create_file(dir: &fio::DirectoryProxy, path: &str) {
        open_file(
            dir,
            path,
            fio::OpenFlags::RIGHT_READABLE
                | fio::OpenFlags::RIGHT_WRITABLE
                | fio::OpenFlags::CREATE,
        )
        .await
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
