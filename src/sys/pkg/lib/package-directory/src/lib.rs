// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io as fio, fuchsia_zircon as zx,
    std::{collections::HashSet, convert::TryInto as _, sync::Arc},
    vfs::{
        common::send_on_open_with_error,
        directory::entry::{DirectoryEntry, EntryInfo},
        path::Path as VfsPath,
    },
};

mod meta_as_dir;
mod meta_as_file;
mod meta_file;
mod meta_subdir;
mod non_meta_subdir;
mod root_dir;

pub use root_dir::{PathError, ReadFileError, RootDir, SubpackagesError};
pub use vfs::execution_scope::ExecutionScope;

#[derive(thiserror::Error, Debug)]
pub enum Error {
    #[error("the meta.far was not found")]
    MissingMetaFar,

    #[error("while opening the meta.far")]
    OpenMetaFar(#[source] fuchsia_fs::node::OpenError),

    #[error("while instantiating a fuchsia archive reader")]
    ArchiveReader(#[source] fuchsia_archive::Error),

    #[error("meta.far has a path that is not valid utf-8: {path:?}")]
    NonUtf8MetaEntry {
        #[source]
        source: std::str::Utf8Error,
        path: Vec<u8>,
    },

    #[error("while reading meta/contents")]
    ReadMetaContents(#[source] fuchsia_archive::Error),

    #[error("while deserializing meta/contents")]
    DeserializeMetaContents(#[source] fuchsia_pkg::MetaContentsError),

    #[error("collision between a file and a directory at path: '{:?}'", path)]
    FileDirectoryCollision { path: String },
}

impl Error {
    fn to_zx_status(&self) -> zx::Status {
        use {fuchsia_fs::node::OpenError, Error::*};
        match self {
            MissingMetaFar => zx::Status::NOT_FOUND,
            OpenMetaFar(OpenError::OpenError(s)) => *s,
            OpenMetaFar(_) => zx::Status::INTERNAL,
            ArchiveReader(fuchsia_archive::Error::Read(_)) => zx::Status::IO,
            ArchiveReader(_) | ReadMetaContents(_) | DeserializeMetaContents(_) => {
                zx::Status::INVALID_ARGS
            }
            FileDirectoryCollision { .. } | NonUtf8MetaEntry { .. } => zx::Status::INVALID_ARGS,
        }
    }
}

/// The storage that provides the non-meta files (accessed by hash) of a package-directory (e.g.
/// blobfs).
pub trait NonMetaStorage: Send + Sync + 'static {
    /// Open a non-meta file by hash.
    fn open(
        &self,
        blob: &fuchsia_hash::Hash,
        flags: fio::OpenFlags,
        mode: u32,
        server_end: ServerEnd<fio::NodeMarker>,
    ) -> Result<(), fuchsia_fs::node::OpenError>;
}

impl NonMetaStorage for blobfs::Client {
    fn open(
        &self,
        blob: &fuchsia_hash::Hash,
        flags: fio::OpenFlags,
        mode: u32,
        server_end: ServerEnd<fio::NodeMarker>,
    ) -> Result<(), fuchsia_fs::node::OpenError> {
        self.forward_open(blob, flags, mode, server_end)
            .map_err(fuchsia_fs::node::OpenError::SendOpenRequest)
    }
}

/// Assumes the directory is a flat container and the files are named after their hashes.
impl NonMetaStorage for fio::DirectoryProxy {
    fn open(
        &self,
        blob: &fuchsia_hash::Hash,
        flags: fio::OpenFlags,
        mode: u32,
        server_end: ServerEnd<fio::NodeMarker>,
    ) -> Result<(), fuchsia_fs::node::OpenError> {
        self.open(flags, mode, blob.to_string().as_str(), server_end)
            .map_err(fuchsia_fs::node::OpenError::SendOpenRequest)
    }
}

/// Serves a package directory for the package with hash `meta_far` on `server_end`.
/// The connection rights are set by `flags`, used the same as the `flags` parameter of
///   fuchsia.io/Directory.Open.
pub fn serve(
    scope: vfs::execution_scope::ExecutionScope,
    non_meta_storage: impl NonMetaStorage,
    meta_far: fuchsia_hash::Hash,
    flags: fio::OpenFlags,
    server_end: ServerEnd<fio::DirectoryMarker>,
) -> impl futures::Future<Output = Result<(), Error>> {
    serve_path(
        scope,
        non_meta_storage,
        meta_far,
        flags,
        0,
        VfsPath::dot(),
        server_end.into_channel().into(),
    )
}

/// Serves a sub-`path` of a package directory for the package with hash `meta_far` on `server_end`.
/// The connection rights are set by `flags`, used the same as the `flags` parameter of
///   fuchsia.io/Directory.Open.
/// On error while loading the package metadata, closes the provided server end, sending an OnOpen
///   response with an error status if requested.
pub async fn serve_path(
    scope: vfs::execution_scope::ExecutionScope,
    non_meta_storage: impl NonMetaStorage,
    meta_far: fuchsia_hash::Hash,
    flags: fio::OpenFlags,
    mode: u32,
    path: VfsPath,
    server_end: ServerEnd<fio::NodeMarker>,
) -> Result<(), Error> {
    let root_dir = match RootDir::new(non_meta_storage, meta_far).await {
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
                        res.push((
                            EntryInfo::new(fio::INO_UNKNOWN, fio::DirentType::File),
                            path.to_string(),
                        ));
                        added_entries.insert(path.to_string());
                    }
                }
                Some((first, _)) => {
                    if !added_entries.contains(first) {
                        res.push((
                            EntryInfo::new(fio::INO_UNKNOWN, fio::DirentType::Directory),
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

#[cfg(test)]
async fn verify_open_adjusts_flags(
    entry: &Arc<dyn DirectoryEntry>,
    in_flags: fio::OpenFlags,
    expected_flags: fio::OpenFlags,
) {
    let (proxy, server_end) = fidl::endpoints::create_proxy::<fio::NodeMarker>().unwrap();

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
        fuchsia_hash::Hash,
        fuchsia_pkg_testing::{blobfs::Fake as FakeBlobfs, PackageBuilder},
        futures::StreamExt,
        std::any::Any,
        vfs::directory::dirents_sink::{self, AppendResult, Sealed, Sink},
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
            fio::OpenFlags::RIGHT_READABLE,
            server_end,
        )
        .await
        .unwrap();

        assert_eq!(
            fuchsia_fs::directory::readdir(&proxy).await.unwrap(),
            vec![fuchsia_fs::directory::DirEntry {
                name: "meta".to_string(),
                kind: fuchsia_fs::directory::DirentKind::Directory
            }]
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn serve_path_open_root() {
        let (proxy, server_end) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
        let package = PackageBuilder::new("just-meta-far").build().await.expect("created pkg");
        let (metafar_blob, _) = package.contents();
        let (blobfs_fake, blobfs_client) = FakeBlobfs::new();
        blobfs_fake.add_blob(metafar_blob.merkle, metafar_blob.contents);

        crate::serve_path(
            vfs::execution_scope::ExecutionScope::new(),
            blobfs_client,
            metafar_blob.merkle,
            fio::OpenFlags::RIGHT_READABLE,
            0,
            VfsPath::validate_and_split(".").unwrap(),
            server_end.into_channel().into(),
        )
        .await
        .unwrap();

        assert_eq!(
            fuchsia_fs::directory::readdir(&proxy).await.unwrap(),
            vec![fuchsia_fs::directory::DirEntry {
                name: "meta".to_string(),
                kind: fuchsia_fs::directory::DirentKind::Directory
            }]
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn serve_path_open_meta() {
        let (proxy, server_end) = fidl::endpoints::create_proxy::<fio::FileMarker>().unwrap();
        let package = PackageBuilder::new("just-meta-far").build().await.expect("created pkg");
        let (metafar_blob, _) = package.contents();
        let (blobfs_fake, blobfs_client) = FakeBlobfs::new();
        blobfs_fake.add_blob(metafar_blob.merkle, metafar_blob.contents);

        crate::serve_path(
            vfs::execution_scope::ExecutionScope::new(),
            blobfs_client,
            metafar_blob.merkle,
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::NOT_DIRECTORY,
            0,
            VfsPath::validate_and_split("meta").unwrap(),
            server_end.into_channel().into(),
        )
        .await
        .unwrap();

        assert_eq!(
            fuchsia_fs::file::read_to_string(&proxy).await.unwrap(),
            metafar_blob.merkle.to_string(),
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn serve_path_open_missing_path_in_package() {
        let (proxy, server_end) = fidl::endpoints::create_proxy::<fio::NodeMarker>().unwrap();
        let package = PackageBuilder::new("just-meta-far").build().await.expect("created pkg");
        let (metafar_blob, _) = package.contents();
        let (blobfs_fake, blobfs_client) = FakeBlobfs::new();
        blobfs_fake.add_blob(metafar_blob.merkle, metafar_blob.contents);

        assert_matches!(
            crate::serve_path(
                vfs::execution_scope::ExecutionScope::new(),
                blobfs_client,
                metafar_blob.merkle,
                fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::DESCRIBE,
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
        let (proxy, server_end) = fidl::endpoints::create_proxy::<fio::NodeMarker>().unwrap();
        let (_blobfs_fake, blobfs_client) = FakeBlobfs::new();

        assert_matches!(
            crate::serve_path(
                vfs::execution_scope::ExecutionScope::new(),
                blobfs_client,
                Hash::from([0u8; 32]),
                fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::DESCRIBE,
                0,
                VfsPath::validate_and_split(".").unwrap(),
                server_end.into_channel().into(),
            )
            .await,
            Err(Error::MissingMetaFar)
        );

        assert_eq!(node_into_on_open_status(proxy).await, Some(zx::Status::NOT_FOUND));
    }

    async fn node_into_on_open_status(node: fio::NodeProxy) -> Option<zx::Status> {
        // Handle either an io1 OnOpen Status or an io2 epitaph status, though only one will be
        // sent, determined by the open() API used.
        let mut events = node.take_event_stream();
        match events.next().await? {
            Ok(fio::NodeEvent::OnOpen_ { s: status, .. }) => {
                return Some(zx::Status::from_raw(status));
            }
            Ok(fio::NodeEvent::OnRepresentation { .. }) => return Some(zx::Status::OK),
            Err(fidl::Error::ClientChannelClosed { status, .. }) => return Some(status),
            other => panic!("unexpected stream event or error: {:?}", other),
        }
    }

    fn file() -> EntryInfo {
        EntryInfo::new(fio::INO_UNKNOWN, fio::DirentType::File)
    }

    fn dir() -> EntryInfo {
        EntryInfo::new(fio::INO_UNKNOWN, fio::DirentType::Directory)
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
}
