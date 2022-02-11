// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::{
        DirectoryMarker, NodeMarker, DIRENT_TYPE_DIRECTORY, DIRENT_TYPE_FILE, INO_UNKNOWN,
    },
    fuchsia_zircon as zx,
    std::{collections::HashSet, convert::TryInto as _, sync::Arc},
    vfs::{
        common::send_on_open_with_error,
        directory::{
            dirents_sink::AppendResult,
            entry::{DirectoryEntry, EntryInfo},
            traversal_position::TraversalPosition,
        },
        path::Path as VfsPath,
    },
};

mod meta_as_dir;
mod meta_as_file;
mod meta_file;
mod meta_subdir;
mod non_meta_subdir;
mod root_dir;

pub use root_dir::{ReadFileError, RootDir};
pub use vfs::execution_scope::ExecutionScope;

#[derive(thiserror::Error, Debug)]
pub enum Error {
    #[error("while opening the meta.far")]
    OpenMetaFar(#[source] io_util::node::OpenError),

    #[error("while instantiating a fuchsia archive reader")]
    ArchiveReader(#[source] fuchsia_archive::Error),

    #[error("while reading meta/contents")]
    ReadMetaContents(#[source] fuchsia_archive::Error),

    #[error("while deserializing meta/contents")]
    DeserializeMetaContents(#[source] fuchsia_pkg::MetaContentsError),

    #[error("collision between a file and a directory at path: '{:?}'", path)]
    FileDirectoryCollision { path: String },
}

impl Error {
    fn to_zx_status(&self) -> zx::Status {
        use io_util::node::OpenError;

        // TODO(fxbug.dev/86995) Align this mapping with pkgfs.
        match self {
            Error::OpenMetaFar(OpenError::OpenError(s)) => *s,
            Error::OpenMetaFar(_) => zx::Status::INTERNAL,
            Error::ArchiveReader(fuchsia_archive::Error::Read(_)) => zx::Status::NOT_FOUND,
            Error::ArchiveReader(_)
            | Error::ReadMetaContents(_)
            | Error::DeserializeMetaContents(_) => zx::Status::INVALID_ARGS,
            Error::FileDirectoryCollision { .. } => zx::Status::INVALID_ARGS,
        }
    }
}

/// Serves a package directory for the package with hash `meta_far` on `server_end`.
/// The connection rights are set by `flags`, used the same as the `flags` parameter of
///   fuchsia.io/Directory.Open.
pub fn serve(
    scope: vfs::execution_scope::ExecutionScope,
    blobfs: blobfs::Client,
    meta_far: fuchsia_hash::Hash,
    flags: u32,
    server_end: ServerEnd<DirectoryMarker>,
) -> impl futures::Future<Output = Result<(), Error>> {
    serve_path(scope, blobfs, meta_far, flags, 0, VfsPath::dot(), server_end.into_channel().into())
}

/// Serves a sub-`path` of a package directory for the package with hash `meta_far` on `server_end`.
/// The connection rights are set by `flags`, used the same as the `flags` parameter of
///   fuchsia.io/Directory.Open.
/// On error while loading the package metadata, closes the provided server end, sending an OnOpen
///   response with an error status if requested.
pub async fn serve_path(
    scope: vfs::execution_scope::ExecutionScope,
    blobfs: blobfs::Client,
    meta_far: fuchsia_hash::Hash,
    flags: u32,
    mode: u32,
    path: VfsPath,
    server_end: ServerEnd<NodeMarker>,
) -> Result<(), Error> {
    let root_dir = match RootDir::new(blobfs, meta_far).await {
        Ok(d) => d,
        Err(e) => {
            let () = send_on_open_with_error(flags, server_end, e.to_zx_status());
            return Err(e);
        }
    };

    Ok(Arc::new(root_dir).open(scope, flags, mode, path, server_end))
}

fn usize_to_u64_safe(u: usize) -> u64 {
    let ret: u64 = u.try_into().unwrap();
    static_assertions::assert_eq_size_val!(u, ret);
    ret
}

fn u64_to_usize_safe(u: u64) -> usize {
    let ret: usize = u.try_into().unwrap();
    static_assertions::assert_eq_size_val!(u, ret);
    ret
}

/// Takes a directory hierarchy and a directory in the hierarchy and returns all the directory's
/// children in alphabetical order.
///   `materialized_tree`: object relative path expressions of every file in a directory hierarchy
///   `dir`: the empty string (signifies the root dir) or a path to a subdir (must be an object
///          relative path expression plus a trailing slash)
/// Returns an empty vec if `dir` isn't in `materialized_tree`.
fn get_dir_children<'a>(
    materialized_tree: impl IntoIterator<Item = &'a str>,
    dir: &str,
) -> Vec<(EntryInfo, String)> {
    let mut added_entries = HashSet::new();
    let mut res = vec![];

    for path in materialized_tree {
        if let Some(path) = path.strip_prefix(&dir) {
            match path.split_once("/") {
                None => {
                    // TODO(fxbug.dev/81370) Replace .contains/.insert with .get_or_insert_owned when non-experimental.
                    if !added_entries.contains(path) {
                        res.push((EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_FILE), path.to_string()));
                        added_entries.insert(path.to_string());
                    }
                }
                Some((first, _)) => {
                    if !added_entries.contains(first) {
                        res.push((
                            EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY),
                            first.to_string(),
                        ));
                        added_entries.insert(first.to_string());
                    }
                }
            }
        }
    }

    // TODO(fxbug.dev/82290) Remove this sort
    res.sort_by(|a, b| a.1.cmp(&b.1));
    res
}

// Implements vfs::directory::entry_container::Directory::read_dirents given `entries`, a sorted
// list of all the Directory's entries.
async fn read_dirents<'a>(
    entries: &'a [(EntryInfo, String)],
    pos: &'a TraversalPosition,
    mut sink: Box<(dyn vfs::directory::dirents_sink::Sink + 'static)>,
) -> Result<
    (TraversalPosition, Box<(dyn vfs::directory::dirents_sink::Sealed + 'static)>),
    zx::Status,
> {
    let starting_position = match pos {
        TraversalPosition::End => {
            return Ok((TraversalPosition::End, sink.seal()));
        }
        TraversalPosition::Name(_) => {
            // The VFS should never send this to us, since we never return it here.
            unreachable!();
        }
        TraversalPosition::Start => {
            match sink.append(&EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY), ".") {
                AppendResult::Ok(new_sink) => sink = new_sink,
                AppendResult::Sealed(sealed) => {
                    return Ok((TraversalPosition::Start, sealed));
                }
            };
            0 as usize
        }
        TraversalPosition::Index(i) => crate::u64_to_usize_safe(*i),
    };

    for i in starting_position..entries.len() {
        let (info, name) = &entries[i];
        match sink.append(info, name) {
            AppendResult::Ok(new_sink) => sink = new_sink,
            AppendResult::Sealed(sealed) => {
                return Ok((TraversalPosition::Index(crate::usize_to_u64_safe(i)), sealed));
            }
        }
    }
    Ok((TraversalPosition::End, sink.seal()))
}

#[cfg(test)]
async fn verify_open_adjusts_flags(
    entry: &Arc<dyn DirectoryEntry>,
    in_flags: u32,
    expected_flags: u32,
) {
    let (proxy, server_end) =
        fidl::endpoints::create_proxy::<fidl_fuchsia_io::NodeMarker>().unwrap();

    DirectoryEntry::open(
        Arc::clone(&entry),
        ExecutionScope::new(),
        in_flags,
        0,
        VfsPath::dot(),
        server_end,
    );

    let (status, flags) = proxy.get_flags().await.unwrap();
    let () = zx::Status::ok(status).unwrap();
    assert_eq!(flags, expected_flags);
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        assert_matches::assert_matches,
        fidl_fuchsia_io::{FileMarker, NodeEvent, NodeProxy},
        fuchsia_hash::Hash,
        fuchsia_pkg_testing::{blobfs::Fake as FakeBlobfs, PackageBuilder},
        futures::StreamExt,
        std::any::Any,
        vfs::directory::dirents_sink::{self, Sealed, Sink},
    };

    #[fuchsia_async::run_singlethreaded(test)]
    async fn serve() {
        let (proxy, server_end) = fidl::endpoints::create_proxy().unwrap();
        let package = PackageBuilder::new("just-meta-far").build().await.expect("created pkg");
        let (metafar_blob, _) = package.contents();
        let (blobfs_fake, blobfs_client) = FakeBlobfs::new();
        blobfs_fake.add_blob(metafar_blob.merkle, metafar_blob.contents);

        crate::serve(
            vfs::execution_scope::ExecutionScope::new(),
            blobfs_client,
            metafar_blob.merkle,
            fidl_fuchsia_io::OPEN_RIGHT_READABLE,
            server_end,
        )
        .await
        .unwrap();

        assert_eq!(
            files_async::readdir(&proxy).await.unwrap(),
            vec![files_async::DirEntry {
                name: "meta".to_string(),
                kind: files_async::DirentKind::Directory
            }]
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn serve_path_open_root() {
        let (proxy, server_end) = fidl::endpoints::create_proxy::<DirectoryMarker>().unwrap();
        let package = PackageBuilder::new("just-meta-far").build().await.expect("created pkg");
        let (metafar_blob, _) = package.contents();
        let (blobfs_fake, blobfs_client) = FakeBlobfs::new();
        blobfs_fake.add_blob(metafar_blob.merkle, metafar_blob.contents);

        crate::serve_path(
            vfs::execution_scope::ExecutionScope::new(),
            blobfs_client,
            metafar_blob.merkle,
            fidl_fuchsia_io::OPEN_RIGHT_READABLE,
            0,
            VfsPath::validate_and_split(".").unwrap(),
            server_end.into_channel().into(),
        )
        .await
        .unwrap();

        assert_eq!(
            files_async::readdir(&proxy).await.unwrap(),
            vec![files_async::DirEntry {
                name: "meta".to_string(),
                kind: files_async::DirentKind::Directory
            }]
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn serve_path_open_meta() {
        let (proxy, server_end) = fidl::endpoints::create_proxy::<FileMarker>().unwrap();
        let package = PackageBuilder::new("just-meta-far").build().await.expect("created pkg");
        let (metafar_blob, _) = package.contents();
        let (blobfs_fake, blobfs_client) = FakeBlobfs::new();
        blobfs_fake.add_blob(metafar_blob.merkle, metafar_blob.contents);

        crate::serve_path(
            vfs::execution_scope::ExecutionScope::new(),
            blobfs_client,
            metafar_blob.merkle,
            fidl_fuchsia_io::OPEN_RIGHT_READABLE | fidl_fuchsia_io::OPEN_FLAG_NOT_DIRECTORY,
            0,
            VfsPath::validate_and_split("meta").unwrap(),
            server_end.into_channel().into(),
        )
        .await
        .unwrap();

        assert_eq!(
            io_util::file::read_to_string(&proxy).await.unwrap(),
            metafar_blob.merkle.to_string(),
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn serve_path_open_missing_path_in_package() {
        let (proxy, server_end) = fidl::endpoints::create_proxy::<NodeMarker>().unwrap();
        let package = PackageBuilder::new("just-meta-far").build().await.expect("created pkg");
        let (metafar_blob, _) = package.contents();
        let (blobfs_fake, blobfs_client) = FakeBlobfs::new();
        blobfs_fake.add_blob(metafar_blob.merkle, metafar_blob.contents);

        assert_matches!(
            crate::serve_path(
                vfs::execution_scope::ExecutionScope::new(),
                blobfs_client,
                metafar_blob.merkle,
                fidl_fuchsia_io::OPEN_RIGHT_READABLE | fidl_fuchsia_io::OPEN_FLAG_DESCRIBE,
                0,
                VfsPath::validate_and_split("not-present").unwrap(),
                server_end.into_channel().into(),
            )
            .await,
            // serve_path succeeds in opening the package, but the forwarded open will discover
            // that the requested path does not exist.
            Ok(())
        );

        assert_eq!(node_into_on_open_status(proxy).await, Some(zx::Status::NOT_FOUND));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn serve_path_open_missing_package() {
        let (proxy, server_end) = fidl::endpoints::create_proxy::<NodeMarker>().unwrap();
        let (_blobfs_fake, blobfs_client) = FakeBlobfs::new();

        assert_matches!(
            crate::serve_path(
                vfs::execution_scope::ExecutionScope::new(),
                blobfs_client,
                Hash::from([0u8; 32]),
                fidl_fuchsia_io::OPEN_RIGHT_READABLE | fidl_fuchsia_io::OPEN_FLAG_DESCRIBE,
                0,
                VfsPath::validate_and_split(".").unwrap(),
                server_end.into_channel().into(),
            )
            .await,
            // RootDir opens the meta.far without requesting an OnOpen event, which improves
            // latency, but results in a less-than-ideal error (a PEER_CLOSED while reading from
            // the meta.far).
            Err(Error::ArchiveReader(_))
        );

        assert_eq!(node_into_on_open_status(proxy).await, Some(zx::Status::NOT_FOUND));
    }

    async fn node_into_on_open_status(node: NodeProxy) -> Option<zx::Status> {
        // Handle either an io1 OnOpen Status or an io2 epitaph status, though only one will be
        // sent, determined by the open() API used.
        let mut events = node.take_event_stream();
        match events.next().await? {
            Ok(NodeEvent::OnOpen_ { s: status, .. }) => {
                return Some(zx::Status::from_raw(status));
            }
            Ok(NodeEvent::OnConnectionInfo { .. }) => return Some(zx::Status::OK),
            Err(fidl::Error::ClientChannelClosed { status, .. }) => return Some(status),
            other => panic!("unexpected stream event or error: {:?}", other),
        }
    }

    fn file() -> EntryInfo {
        EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_FILE)
    }

    fn dir() -> EntryInfo {
        EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)
    }

    #[test]
    fn get_dir_children_root() {
        assert_eq!(get_dir_children([], ""), vec![]);
        assert_eq!(get_dir_children(["a"], ""), vec![(file(), "a".to_string())]);
        assert_eq!(
            get_dir_children(["a", "b"], ""),
            vec![(file(), "a".to_string()), (file(), "b".to_string())]
        );
        assert_eq!(
            get_dir_children(["b", "a"], ""),
            vec![(file(), "a".to_string()), (file(), "b".to_string())]
        );
        assert_eq!(get_dir_children(["a", "a"], ""), vec![(file(), "a".to_string())]);
        assert_eq!(get_dir_children(["a/b"], ""), vec![(dir(), "a".to_string())]);
        assert_eq!(
            get_dir_children(["a/b", "c"], ""),
            vec![(dir(), "a".to_string()), (file(), "c".to_string())]
        );
        assert_eq!(get_dir_children(["a/b/c"], ""), vec![(dir(), "a".to_string())]);
    }

    #[test]
    fn get_dir_children_subdir() {
        assert_eq!(get_dir_children([], "a/"), vec![]);
        assert_eq!(get_dir_children(["a"], "a/"), vec![]);
        assert_eq!(get_dir_children(["a", "b"], "a/"), vec![]);
        assert_eq!(get_dir_children(["a/b"], "a/"), vec![(file(), "b".to_string())]);
        assert_eq!(
            get_dir_children(["a/b", "a/c"], "a/"),
            vec![(file(), "b".to_string()), (file(), "c".to_string())]
        );
        assert_eq!(
            get_dir_children(["a/c", "a/b"], "a/"),
            vec![(file(), "b".to_string()), (file(), "c".to_string())]
        );
        assert_eq!(get_dir_children(["a/b", "a/b"], "a/"), vec![(file(), "b".to_string())]);
        assert_eq!(get_dir_children(["a/b/c"], "a/"), vec![(dir(), "b".to_string())]);
        assert_eq!(
            get_dir_children(["a/b/c", "a/d"], "a/"),
            vec![(dir(), "b".to_string()), (file(), "d".to_string())]
        );
        assert_eq!(get_dir_children(["a/b/c/d"], "a/"), vec![(dir(), "b".to_string())]);
    }

    /// Implementation of vfs::directory::dirents_sink::Sink.
    /// Sink::append begins to fail (returns Sealed) after `max_entries` entries have been appended.
    #[derive(Clone)]
    pub(crate) struct FakeSink {
        max_entries: usize,
        pub(crate) entries: Vec<(String, EntryInfo)>,
        sealed: bool,
    }

    impl FakeSink {
        pub(crate) fn new(max_entries: usize) -> Self {
            FakeSink { max_entries, entries: Vec::with_capacity(max_entries), sealed: false }
        }

        pub(crate) fn from_sealed(sealed: Box<dyn dirents_sink::Sealed>) -> Box<FakeSink> {
            sealed.into()
        }
    }

    impl From<Box<dyn dirents_sink::Sealed>> for Box<FakeSink> {
        fn from(sealed: Box<dyn dirents_sink::Sealed>) -> Self {
            sealed.open().downcast::<FakeSink>().unwrap()
        }
    }

    impl Sink for FakeSink {
        fn append(mut self: Box<Self>, entry: &EntryInfo, name: &str) -> AppendResult {
            assert!(!self.sealed);
            if self.entries.len() == self.max_entries {
                AppendResult::Sealed(self.seal())
            } else {
                self.entries.push((name.to_owned(), entry.clone()));
                AppendResult::Ok(self)
            }
        }

        fn seal(mut self: Box<Self>) -> Box<dyn Sealed> {
            self.sealed = true;
            self
        }
    }

    impl Sealed for FakeSink {
        fn open(self: Box<Self>) -> Box<dyn Any> {
            self
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn read_dirents_start() {
        let entries = get_dir_children(["resource", "meta/file"], "");

        let (start_pos, sealed) =
            read_dirents(&entries, &TraversalPosition::Start, Box::new(FakeSink::new(0)))
                .await
                .expect("read_dirents failed");
        assert_eq!(FakeSink::from_sealed(sealed).entries, vec![]);
        assert_eq!(start_pos, TraversalPosition::Start);

        let (end_pos, sealed) =
            read_dirents(&entries, &TraversalPosition::Start, Box::new(FakeSink::new(3)))
                .await
                .expect("read_dirents failed");
        assert_eq!(
            FakeSink::from_sealed(sealed).entries,
            vec![
                (".".to_string(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)),
                ("meta".to_string(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)),
                ("resource".to_string(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_FILE))
            ]
        );
        assert_eq!(end_pos, TraversalPosition::End);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn read_dirents_end() {
        let entries = get_dir_children(["resource", "meta/file"], "");

        let (pos, sealed) =
            read_dirents(&entries, &TraversalPosition::End, Box::new(FakeSink::new(3)))
                .await
                .expect("read_dirents failed");
        assert_eq!(FakeSink::from_sealed(sealed).entries, vec![]);
        assert_eq!(pos, TraversalPosition::End);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn read_dirents_index() {
        let entries = get_dir_children(["resource", "meta/file"], "");

        let (pos, sealed) =
            read_dirents(&entries, &TraversalPosition::Start, Box::new(FakeSink::new(2)))
                .await
                .expect("read_dirents failed");
        assert_eq!(
            FakeSink::from_sealed(sealed).entries,
            vec![
                (".".to_string(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)),
                ("meta".to_string(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)),
            ]
        );
        assert_eq!(pos, TraversalPosition::Index(1));

        let (end_pos, sealed) = read_dirents(&entries, &pos, Box::new(FakeSink::new(2)))
            .await
            .expect("read_dirents failed");
        assert_eq!(
            FakeSink::from_sealed(sealed).entries,
            vec![("resource".to_string(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_FILE))]
        );
        assert_eq!(end_pos, TraversalPosition::End);
    }
}
