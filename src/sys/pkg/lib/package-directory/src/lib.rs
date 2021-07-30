// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::anyhow,
    async_trait::async_trait,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::NodeMarker,
    fidl_fuchsia_io::{
        FileProxy, NodeAttributes, OPEN_FLAG_APPEND, OPEN_FLAG_CREATE, OPEN_FLAG_CREATE_IF_ABSENT,
        OPEN_FLAG_TRUNCATE, OPEN_RIGHT_ADMIN, OPEN_RIGHT_WRITABLE,
    },
    fuchsia_archive::AsyncReader,
    fuchsia_pkg::MetaContents,
    fuchsia_syslog::fx_log_err,
    fuchsia_zircon as zx,
    std::convert::TryInto,
    std::{
        collections::{HashMap, HashSet},
        sync::Arc,
    },
    thiserror::Error,
    vfs::{
        common::send_on_open_with_error,
        directory::{
            connection::{io1::DerivedConnection, util::OpenDirectory},
            dirents_sink::AppendResult,
            entry::EntryInfo,
            entry_container::AsyncGetEntry,
            immutable::connection::io1::{ImmutableConnection, ImmutableConnectionClient},
            test_utils::reexport::Status as vfs_status,
            traversal_position::TraversalPosition,
        },
        execution_scope::ExecutionScope,
        path::Path as VfsPath,
        test_utils::assertions::reexport::{DIRENT_TYPE_DIRECTORY, DIRENT_TYPE_FILE, INO_UNKNOWN},
    },
};

#[derive(Clone, Debug, PartialEq)]
struct MetaFileLocation {
    offset: u64,
    length: u64,
}

#[allow(dead_code)]
struct RootDir {
    blobfs: blobfs::Client,
    hash: fuchsia_hash::Hash,
    meta_far: FileProxy,
    meta_files: HashMap<String, MetaFileLocation>,
    non_meta_files: HashMap<String, fuchsia_hash::Hash>,
    // Once populated, this option must never be cleared.
    meta_far_vmo: parking_lot::RwLock<Option<fidl_fuchsia_mem::Buffer>>,
}

#[derive(Error, Debug)]
pub enum RootDirError {
    #[error("while opening a node")]
    OpenMetaFar(#[source] io_util::node::OpenError),

    #[error("while instantiating a fuchsia archive reader")]
    ArchiveReader(#[source] fuchsia_archive::Error),

    #[error("while reading meta/contents")]
    ReadMetaContents(#[source] fuchsia_archive::Error),

    #[error("while deserializing meta/contents")]
    DeserializeMetaContents(#[source] fuchsia_pkg::MetaContentsError),
}

#[allow(dead_code)]
impl RootDir {
    pub async fn new(
        blobfs: blobfs::Client,
        hash: fuchsia_hash::Hash,
    ) -> Result<Self, RootDirError> {
        let meta_far =
            blobfs.open_blob_for_read_no_describe(&hash).map_err(RootDirError::OpenMetaFar)?;

        let reader = io_util::file::AsyncFile::from_proxy(Clone::clone(&meta_far));
        let mut async_reader =
            AsyncReader::new(reader).await.map_err(RootDirError::ArchiveReader)?;
        let reader_list = async_reader.list();

        let mut meta_files = HashMap::with_capacity(reader_list.size_hint().0);

        for entry in reader_list {
            if entry.path().starts_with("meta/") {
                meta_files.insert(
                    String::from(entry.path()),
                    MetaFileLocation { offset: entry.offset(), length: entry.length() },
                );
            }
        }

        let meta_contents_bytes = async_reader
            .read_file("meta/contents")
            .await
            .map_err(RootDirError::ReadMetaContents)?;

        let non_meta_files: HashMap<_, _> = MetaContents::deserialize(&meta_contents_bytes[..])
            .map_err(RootDirError::DeserializeMetaContents)?
            .into_contents()
            .into_iter()
            .collect();

        let meta_far_vmo = parking_lot::RwLock::new(None);

        Ok(RootDir { blobfs, hash, meta_far, meta_files, non_meta_files, meta_far_vmo })
    }

    fn get_entries(&self) -> Vec<(EntryInfo, String)> {
        let mut non_meta_keys = HashSet::new();
        let mut res = vec![];

        res.push((EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY), "meta".to_string()));

        for key in self.non_meta_files.keys() {
            match key.split_once("/") {
                None => {
                    // TODO(fxbug.dev/81370) Replace .contains/.insert with .get_or_insert_owned when non-experimental.
                    if !non_meta_keys.contains(key) {
                        res.push((EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_FILE), key.to_string()));
                        non_meta_keys.insert(key.to_string());
                    }
                }
                Some((first, _)) => {
                    if !non_meta_keys.contains(first) {
                        res.push((
                            EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY),
                            first.to_string(),
                        ));
                        non_meta_keys.insert(first.to_string());
                    }
                }
            }
        }

        res.sort_by(|a, b| a.1.cmp(&b.1));
        res
    }
}

impl vfs::directory::entry::DirectoryEntry for RootDir {
    fn open(
        self: Arc<Self>,
        scope: ExecutionScope,
        flags: u32,
        mode: u32,
        path: VfsPath,
        server_end: ServerEnd<NodeMarker>,
    ) {
        if path.is_empty() {
            if flags
                & (OPEN_RIGHT_WRITABLE
                    | OPEN_RIGHT_ADMIN
                    | OPEN_FLAG_CREATE
                    | OPEN_FLAG_CREATE_IF_ABSENT
                    | OPEN_FLAG_TRUNCATE
                    | OPEN_FLAG_APPEND)
                != 0
            {
                send_on_open_with_error(flags, server_end, zx::Status::NOT_SUPPORTED);
                return;
            }

            ImmutableConnection::create_connection(
                scope,
                OpenDirectory::new(self as Arc<dyn ImmutableConnectionClient>),
                flags,
                server_end,
            );
            return;
        }

        let canonical_path = path.as_ref().strip_suffix("/").unwrap_or_else(|| path.as_ref());

        if canonical_path == "meta" {
            Arc::new(Meta::new(self)).open(scope, flags, mode, VfsPath::dot(), server_end);
            return;
        }

        if canonical_path.starts_with("meta/") {
            if let Some(meta_file) = self.meta_files.get(canonical_path) {
                Arc::new(MetaFile::new(meta_file.offset, meta_file.length, self)).open(
                    scope,
                    flags,
                    mode,
                    VfsPath::dot(),
                    server_end,
                );
                return;
            }

            let mut directory_key = canonical_path.to_string();
            directory_key.push_str("/");
            for k in self.meta_files.keys() {
                if k.starts_with(&directory_key) {
                    Arc::new(MetaSubdir::new(self, canonical_path.to_string())).open(
                        scope,
                        flags,
                        mode,
                        VfsPath::dot(),
                        server_end,
                    );
                    return;
                }
            }

            send_on_open_with_error(flags, server_end, zx::Status::NOT_FOUND);
            return;
        } else {
            if let Some(blob) = self.non_meta_files.get(canonical_path) {
                match self.blobfs.forward_open(blob, flags, mode, server_end) {
                    Ok(()) => {}
                    Err(e) => fx_log_err!("Error opening from blobfs: {:#}", anyhow!(e)),
                };
                return;
            }
            let mut directory_key = canonical_path.to_string();
            directory_key.push_str("/");
            for k in self.non_meta_files.keys() {
                if k.starts_with(&directory_key) {
                    Arc::new(NonMetaSubdir::new(self, canonical_path.to_string())).open(
                        scope,
                        flags,
                        mode,
                        VfsPath::dot(),
                        server_end,
                    );
                    return;
                }
            }

            send_on_open_with_error(flags, server_end, zx::Status::NOT_FOUND);
            return;
        }
    }

    fn entry_info(&self) -> EntryInfo {
        EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)
    }

    fn can_hardlink(&self) -> bool {
        false
    }
}

#[async_trait]
impl vfs::directory::entry_container::Directory for RootDir {
    // Used for linking which is not supported.
    fn get_entry<'a>(self: Arc<Self>, _: &'a str) -> AsyncGetEntry<'a> {
        AsyncGetEntry::from(vfs_status::NOT_SUPPORTED)
    }

    async fn read_dirents<'a>(
        &'a self,
        pos: &'a TraversalPosition,
        mut sink: Box<(dyn vfs::directory::dirents_sink::Sink + 'static)>,
    ) -> Result<
        (TraversalPosition, Box<(dyn vfs::directory::dirents_sink::Sealed + 'static)>),
        vfs_status,
    > {
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
            TraversalPosition::Index(i) => u64_to_usize_safe(*i),
        };

        let entries = self.get_entries();

        for i in starting_position..entries.len() {
            let (info, name) = &entries[i];
            match sink.append(info, name) {
                AppendResult::Ok(new_sink) => sink = new_sink,
                AppendResult::Sealed(sealed) => {
                    return Ok((TraversalPosition::Index(usize_to_u64_safe(i)), sealed));
                }
            }
        }
        Ok((TraversalPosition::End, sink.seal()))
    }

    fn register_watcher(
        self: Arc<Self>,
        _: ExecutionScope,
        _: u32,
        _: fidl::AsyncChannel,
    ) -> Result<(), vfs_status> {
        Err(vfs_status::NOT_SUPPORTED)
    }

    // `register_watcher` is unsupported so no need to do anything here.
    fn unregister_watcher(self: Arc<Self>, _: usize) {}

    async fn get_attrs(&self) -> Result<NodeAttributes, vfs_status> {
        Ok(NodeAttributes {
            mode: 0, /* Populated by the VFS connection */
            id: 1,
            content_size: 0,
            storage_size: 0,
            link_count: 1,
            creation_time: 0,
            modification_time: 0,
        })
    }

    fn close(&self) -> Result<(), vfs_status> {
        Ok(())
    }
}

#[allow(dead_code)]
struct Meta {
    root_dir: Arc<RootDir>,
}

impl Meta {
    pub fn new(root_dir: Arc<RootDir>) -> Self {
        Meta { root_dir }
    }
}

// TODO(fxbug.dev/75599)
impl vfs::directory::entry::DirectoryEntry for Meta {
    fn open(
        self: Arc<Self>,
        _: ExecutionScope,
        _: u32,
        _: u32,
        _: VfsPath,
        _: ServerEnd<NodeMarker>,
    ) {
        todo!()
    }
    fn entry_info(&self) -> vfs::directory::entry::EntryInfo {
        EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)
    }
    fn can_hardlink(&self) -> bool {
        false
    }
}

#[allow(dead_code)]
struct MetaSubdir {
    root_dir: Arc<RootDir>,
    path: String,
}

impl MetaSubdir {
    pub fn new(root_dir: Arc<RootDir>, path: String) -> Self {
        MetaSubdir { root_dir, path }
    }
}

// TODO(fxbug.dev/75600)
impl vfs::directory::entry::DirectoryEntry for MetaSubdir {
    fn open(
        self: Arc<Self>,
        _: ExecutionScope,
        _: u32,
        _: u32,
        _: VfsPath,
        _: ServerEnd<NodeMarker>,
    ) {
        todo!()
    }
    fn entry_info(&self) -> vfs::directory::entry::EntryInfo {
        EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)
    }
    fn can_hardlink(&self) -> bool {
        false
    }
}

#[allow(dead_code)]
struct NonMetaSubdir {
    root_dir: Arc<RootDir>,
    path: String,
}

impl NonMetaSubdir {
    pub fn new(root_dir: Arc<RootDir>, path: String) -> Self {
        NonMetaSubdir { root_dir, path }
    }
}

// TODO(fxbug.dev/75603)
impl vfs::directory::entry::DirectoryEntry for NonMetaSubdir {
    fn open(
        self: Arc<Self>,
        _: ExecutionScope,
        _: u32,
        _: u32,
        _: VfsPath,
        _: ServerEnd<NodeMarker>,
    ) {
        todo!()
    }
    fn entry_info(&self) -> vfs::directory::entry::EntryInfo {
        EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)
    }
    fn can_hardlink(&self) -> bool {
        false
    }
}

#[allow(dead_code)]
struct MetaFile {
    offset: u64,
    length: u64,
    root_dir: Arc<RootDir>,
}

impl MetaFile {
    pub fn new(offset: u64, length: u64, root_dir: Arc<RootDir>) -> Self {
        MetaFile { offset, length, root_dir }
    }
}

// TODO(fxbug.dev/75601)
impl vfs::directory::entry::DirectoryEntry for MetaFile {
    fn open(
        self: Arc<Self>,
        _: ExecutionScope,
        _: u32,
        _: u32,
        _: VfsPath,
        _: ServerEnd<NodeMarker>,
    ) {
        todo!()
    }
    fn entry_info(&self) -> vfs::directory::entry::EntryInfo {
        EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_FILE)
    }
    fn can_hardlink(&self) -> bool {
        false
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl::{AsyncChannel, Channel},
        fidl_fuchsia_io::DirectoryMarker,
        fuchsia_pkg_testing::PackageBuilder,
        matches::assert_matches,
        std::any::Any,
        vfs::directory::{
            dirents_sink::{self, Sealed, Sink},
            entry::DirectoryEntry,
            entry_container::Directory,
        },
    };

    #[fuchsia_async::run_singlethreaded(test)]
    async fn lifecycle() {
        let (proxy, server_end) = fidl::endpoints::create_proxy::<DirectoryMarker>().unwrap();
        let blobid = fuchsia_hash::Hash::from([0u8; 32]);
        let blobfs_client = blobfs::Client::new(proxy);

        drop(server_end);

        let _ = RootDir::new(blobfs_client, blobid).await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn check_fields_meta_far_only() {
        let package = PackageBuilder::new("just-meta-far").build().await.expect("created pkg");

        let (metafar_blob, _) = package.contents();

        let (blobfs_fake, blobfs_client) = fuchsia_pkg_testing::blobfs::Fake::new();
        blobfs_fake.add_blob(metafar_blob.merkle, metafar_blob.contents);

        let meta_files: HashMap<String, MetaFileLocation> = [
            (String::from("meta/contents"), MetaFileLocation { offset: 4096, length: 0 }),
            (String::from("meta/package"), MetaFileLocation { offset: 4096, length: 38 }),
        ]
        .iter()
        .cloned()
        .collect();

        let root_dir = RootDir::new(blobfs_client, metafar_blob.merkle).await.unwrap();

        assert_eq!(root_dir.meta_files, meta_files);
        assert_eq!(root_dir.non_meta_files, HashMap::new());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn check_fields() {
        let pkg = PackageBuilder::new("base-package-0")
            .add_resource_at("resource", &[][..])
            .add_resource_at("meta/file", "meta/file".as_bytes())
            .build()
            .await
            .unwrap();
        let (metafar_blob, content_blobs) = pkg.contents();

        let (blobfs_fake, blobfs_client) = fuchsia_pkg_testing::blobfs::Fake::new();
        blobfs_fake.add_blob(metafar_blob.merkle, metafar_blob.contents);
        for content in content_blobs {
            blobfs_fake.add_blob(content.merkle, content.contents);
        }

        let meta_files: HashMap<String, MetaFileLocation> = [
            (String::from("meta/contents"), MetaFileLocation { offset: 4096, length: 74 }),
            (String::from("meta/package"), MetaFileLocation { offset: 12288, length: 39 }),
            (String::from("meta/file"), MetaFileLocation { offset: 8192, length: 9 }),
        ]
        .iter()
        .cloned()
        .collect();

        let non_meta_files: HashMap<String, fuchsia_hash::Hash> = [(
            String::from("resource"),
            "15ec7bf0b50732b49f8228e07d24365338f9e3ab994b00af08e5a3bffe55fd8b"
                .parse::<fuchsia_hash::Hash>()
                .unwrap(),
        )]
        .iter()
        .cloned()
        .collect();

        let root_dir = RootDir::new(blobfs_client, metafar_blob.merkle).await.unwrap();

        assert_eq!(root_dir.meta_files, meta_files);
        assert_eq!(root_dir.non_meta_files, non_meta_files);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn root_dir_hardlink() {
        let package = PackageBuilder::new("just-meta-far").build().await.expect("created pkg");
        let (metafar_blob, _) = package.contents();

        let (blobfs_fake, blobfs_client) = fuchsia_pkg_testing::blobfs::Fake::new();
        blobfs_fake.add_blob(metafar_blob.merkle, metafar_blob.contents);

        let root_dir = RootDir::new(blobfs_client, metafar_blob.merkle).await.unwrap();
        assert_eq!(root_dir.can_hardlink(), false);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn root_dir_get_attrs() {
        let package = PackageBuilder::new("just-meta-far").build().await.expect("created pkg");
        let (metafar_blob, _) = package.contents();
        let (blobfs_fake, blobfs_client) = fuchsia_pkg_testing::blobfs::Fake::new();
        blobfs_fake.add_blob(metafar_blob.merkle, metafar_blob.contents);

        let root_dir = RootDir::new(blobfs_client, metafar_blob.merkle).await.unwrap();
        assert_eq!(
            root_dir.get_attrs().await.unwrap(),
            NodeAttributes {
                mode: 0,
                id: 1,
                content_size: 0,
                storage_size: 0,
                link_count: 1,
                creation_time: 0,
                modification_time: 0,
            }
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn root_dir_entry_info() {
        let package = PackageBuilder::new("just-meta-far").build().await.expect("created pkg");

        let (metafar_blob, _) = package.contents();

        let (blobfs_fake, blobfs_client) = fuchsia_pkg_testing::blobfs::Fake::new();
        blobfs_fake.add_blob(metafar_blob.merkle, metafar_blob.contents);

        let root_dir = RootDir::new(blobfs_client, metafar_blob.merkle).await.unwrap();
        assert_eq!(root_dir.entry_info(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn root_dir_get_entry_not_supported() {
        let package = PackageBuilder::new("just-meta-far").build().await.expect("created pkg");

        let (metafar_blob, _) = package.contents();

        let (blobfs_fake, blobfs_client) = fuchsia_pkg_testing::blobfs::Fake::new();
        blobfs_fake.add_blob(metafar_blob.merkle, metafar_blob.contents);

        let root_dir = Arc::new(RootDir::new(blobfs_client, metafar_blob.merkle).await.unwrap());

        match root_dir.get_entry("") {
            AsyncGetEntry::Future(_fut) => panic!("should have given the other thing"),
            AsyncGetEntry::Immediate(res) => {
                assert_eq!(res.map(|_| ()).unwrap_err(), vfs_status::NOT_SUPPORTED)
            }
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn root_dir_register_watcher_not_supported() {
        let package = PackageBuilder::new("just-meta-far").build().await.expect("created pkg");

        let (metafar_blob, _) = package.contents();

        let (blobfs_fake, blobfs_client) = fuchsia_pkg_testing::blobfs::Fake::new();
        blobfs_fake.add_blob(metafar_blob.merkle, metafar_blob.contents);

        let root_dir = Arc::new(RootDir::new(blobfs_client, metafar_blob.merkle).await.unwrap());
        let (a, _) = Channel::create().unwrap();
        let async_a = AsyncChannel::from_channel(a).unwrap();
        assert_matches!(
            root_dir.register_watcher(ExecutionScope::new(), 0, async_a),
            Err(vfs_status::NOT_SUPPORTED)
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn root_dir_close() {
        let package = PackageBuilder::new("just-meta-far").build().await.expect("created pkg");

        let (metafar_blob, _) = package.contents();

        let (blobfs_fake, blobfs_client) = fuchsia_pkg_testing::blobfs::Fake::new();
        blobfs_fake.add_blob(metafar_blob.merkle, metafar_blob.contents);

        let root_dir = RootDir::new(blobfs_client, metafar_blob.merkle).await.unwrap();
        assert_eq!(root_dir.close(), Ok(()));
    }

    #[derive(Clone)]
    struct DummySink {
        max_size: usize,
        entries: Vec<(String, EntryInfo)>,
        sealed: bool,
    }

    impl DummySink {
        pub fn new(max_size: usize) -> Self {
            DummySink { max_size, entries: Vec::with_capacity(max_size), sealed: false }
        }

        fn from_sealed(sealed: Box<dyn dirents_sink::Sealed>) -> Box<DummySink> {
            sealed.into()
        }
    }

    impl From<Box<dyn dirents_sink::Sealed>> for Box<DummySink> {
        fn from(sealed: Box<dyn dirents_sink::Sealed>) -> Self {
            sealed.open().downcast::<DummySink>().unwrap()
        }
    }

    impl Sink for DummySink {
        fn append(mut self: Box<Self>, entry: &EntryInfo, name: &str) -> AppendResult {
            assert!(!self.sealed);
            if self.entries.len() == self.max_size {
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

    impl Sealed for DummySink {
        fn open(self: Box<Self>) -> Box<dyn Any> {
            self
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn root_dir_read_dirents_start() {
        let pkg = PackageBuilder::new("base-package-0")
            .add_resource_at("resource", &[][..])
            .add_resource_at("meta/file", "meta/file".as_bytes())
            .build()
            .await
            .unwrap();
        let (metafar_blob, content_blobs) = pkg.contents();

        let (blobfs_fake, blobfs_client) = fuchsia_pkg_testing::blobfs::Fake::new();
        blobfs_fake.add_blob(metafar_blob.merkle, metafar_blob.contents);
        for content in content_blobs {
            blobfs_fake.add_blob(content.merkle, content.contents);
        }

        let root_dir = RootDir::new(blobfs_client, metafar_blob.merkle).await.unwrap();

        let (start_pos, sealed) = futures::executor::block_on(
            root_dir.read_dirents(&TraversalPosition::Start, Box::new(DummySink::new(0))),
        )
        .expect("read_dirents failed");
        assert_eq!(DummySink::from_sealed(sealed).entries, vec![]);
        assert_eq!(start_pos, TraversalPosition::Start);

        let (end_pos, sealed) = futures::executor::block_on(
            root_dir.read_dirents(&TraversalPosition::Start, Box::new(DummySink::new(3))),
        )
        .expect("read_dirents failed");
        assert_eq!(
            DummySink::from_sealed(sealed).entries,
            vec![
                (".".to_string(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)),
                ("meta".to_string(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)),
                ("resource".to_string(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_FILE))
            ]
        );
        assert_eq!(end_pos, TraversalPosition::End);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn root_dir_read_dirents_end() {
        let pkg = PackageBuilder::new("base-package-0")
            .add_resource_at("resource", &[][..])
            .add_resource_at("meta/file", "meta/file".as_bytes())
            .build()
            .await
            .unwrap();
        let (metafar_blob, content_blobs) = pkg.contents();

        let (blobfs_fake, blobfs_client) = fuchsia_pkg_testing::blobfs::Fake::new();
        blobfs_fake.add_blob(metafar_blob.merkle, metafar_blob.contents);
        for content in content_blobs {
            blobfs_fake.add_blob(content.merkle, content.contents);
        }

        let root_dir = RootDir::new(blobfs_client, metafar_blob.merkle).await.unwrap();
        let (pos, sealed) = futures::executor::block_on(
            root_dir.read_dirents(&TraversalPosition::End, Box::new(DummySink::new(3))),
        )
        .expect("read_dirents failed");
        assert_eq!(DummySink::from_sealed(sealed).entries, vec![]);
        assert_eq!(pos, TraversalPosition::End);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn root_dir_read_dirents_index() {
        let pkg = PackageBuilder::new("base-package-0")
            .add_resource_at("resource", &[][..])
            .add_resource_at("meta/file", "meta/file".as_bytes())
            .build()
            .await
            .unwrap();
        let (metafar_blob, content_blobs) = pkg.contents();

        let (blobfs_fake, blobfs_client) = fuchsia_pkg_testing::blobfs::Fake::new();
        blobfs_fake.add_blob(metafar_blob.merkle, metafar_blob.contents);
        for content in content_blobs {
            blobfs_fake.add_blob(content.merkle, content.contents);
        }

        let root_dir = RootDir::new(blobfs_client, metafar_blob.merkle).await.unwrap();
        let (pos, sealed) = futures::executor::block_on(
            root_dir.read_dirents(&TraversalPosition::Start, Box::new(DummySink::new(2))),
        )
        .expect("read_dirents failed");
        assert_eq!(
            DummySink::from_sealed(sealed).entries,
            vec![
                (".".to_string(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)),
                ("meta".to_string(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)),
            ]
        );
        let (end_pos, sealed) =
            futures::executor::block_on(root_dir.read_dirents(&pos, Box::new(DummySink::new(2))))
                .expect("read_dirents failed");
        assert_eq!(
            DummySink::from_sealed(sealed).entries,
            vec![("resource".to_string(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_FILE))]
        );
        assert_eq!(end_pos, TraversalPosition::End);
    }
}
