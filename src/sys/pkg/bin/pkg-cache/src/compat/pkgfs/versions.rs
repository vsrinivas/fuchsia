// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{index::PackageIndex, ExecutabilityRestrictions},
    anyhow::anyhow,
    async_trait::async_trait,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::{
        NodeAttributes, NodeMarker, DIRENT_TYPE_DIRECTORY, INO_UNKNOWN, MODE_TYPE_DIRECTORY,
        OPEN_FLAG_APPEND, OPEN_FLAG_CREATE, OPEN_FLAG_CREATE_IF_ABSENT, OPEN_FLAG_TRUNCATE,
        OPEN_RIGHT_ADMIN, OPEN_RIGHT_EXECUTABLE, OPEN_RIGHT_WRITABLE,
    },
    fuchsia_hash::Hash,
    fuchsia_syslog::fx_log_err,
    fuchsia_zircon as zx,
    futures::lock::Mutex,
    std::{collections::BTreeSet, str::FromStr, sync::Arc},
    system_image::NonStaticAllowList,
    vfs::{
        common::send_on_open_with_error,
        directory::{
            connection::{io1::DerivedConnection, util::OpenDirectory},
            dirents_sink,
            entry::EntryInfo,
            immutable::connection::io1::{ImmutableConnection, ImmutableConnectionClient},
            traversal_position::TraversalPosition,
        },
        execution_scope::ExecutionScope,
        path::Path as VfsPath,
    },
};

#[derive(Debug)]
struct PkgfsVersions {
    static_packages: system_image::StaticPackages,
    non_static_packages: Arc<Mutex<PackageIndex>>,
    non_static_allow_list: NonStaticAllowList,
    executability_restrictions: ExecutabilityRestrictions,
    blobfs: blobfs::Client,
}

enum PackageValidationStatus {
    AccessDenied,
    NotFound,
    Ok,
}

impl PkgfsVersions {
    // TODO use this
    #[allow(dead_code)]
    pub fn new(
        static_packages: system_image::StaticPackages,
        non_static_packages: Arc<Mutex<PackageIndex>>,
        non_static_allow_list: NonStaticAllowList,
        executability_restrictions: ExecutabilityRestrictions,
        blobfs: blobfs::Client,
    ) -> Self {
        Self {
            static_packages,
            non_static_packages,
            non_static_allow_list,
            executability_restrictions,
            blobfs,
        }
    }

    fn is_package_static(&self, hash: &Hash) -> bool {
        self.static_packages.contents().any(|(_path, static_hash)| hash == static_hash)
    }

    async fn is_active_and_allowlisted(&self, hash: &Hash) -> (bool, bool) {
        if let Some(name) = self.non_static_packages.lock().await.get_name_if_active(&hash) {
            (true, self.non_static_allow_list.allows(name))
        } else {
            (false, false)
        }
    }

    async fn package_hashes(&self) -> BTreeSet<Hash> {
        let active_packages = self.non_static_packages.lock().await.active_packages();
        self.static_packages
            .contents()
            .map(|(_path, hash)| hash.clone())
            .chain(active_packages.into_iter().map(|(_path, hash)| hash))
            .collect()
    }

    async fn validate_package(&self, hash: &Hash, flags: u32) -> PackageValidationStatus {
        if self.is_package_static(&hash) {
            // Static packages are not a subject to executability enforcement.
            PackageValidationStatus::Ok
        } else {
            let needs_executability_enforcement = flags & OPEN_RIGHT_EXECUTABLE != 0
                && self.executability_restrictions == ExecutabilityRestrictions::Enforce;

            let (is_active, is_allowlisted) = self.is_active_and_allowlisted(&hash).await;
            if !is_active {
                // Non-static package isn't active.
                PackageValidationStatus::NotFound
            } else if needs_executability_enforcement && !is_allowlisted {
                // Non-static package isn't allowlisted and executability is enforced.
                PackageValidationStatus::AccessDenied
            } else {
                // Non-static package is allowlisted or executability is not enforced.
                PackageValidationStatus::Ok
            }
        }
    }
}

#[async_trait]
impl vfs::directory::entry_container::Directory for PkgfsVersions {
    async fn read_dirents<'a>(
        &'a self,
        pos: &'a TraversalPosition,
        mut sink: Box<(dyn vfs::directory::dirents_sink::Sink + 'static)>,
    ) -> Result<
        (TraversalPosition, Box<(dyn vfs::directory::dirents_sink::Sealed + 'static)>),
        zx::Status,
    > {
        use dirents_sink::AppendResult;

        let entries = self.package_hashes().await;

        let mut remaining = match pos {
            TraversalPosition::Start => {
                // Yield "." first. If even that can't fit in the response, return the same
                // traversal position so we try again next time (where the client hopefully
                // provides a bigger buffer).
                match sink.append(&EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY), ".") {
                    AppendResult::Ok(new_sink) => sink = new_sink,
                    AppendResult::Sealed(sealed) => return Ok((TraversalPosition::Start, sealed)),
                }

                entries.range::<Hash, _>(..)
            }
            TraversalPosition::Name(next) => {
                // This function only returns valid package hashes, so it will always be provided a
                // valid package hash.
                let next: Hash = next.parse().unwrap();

                // `next` is the name of the next item that still needs to be provided, so start
                // there.
                entries.range(next..)
            }
            TraversalPosition::Index(_) => {
                // This directory uses names for iteration to more gracefully handle concurrent
                // directory reads while the directory itself may change.  Index-based enumeration
                // may end up repeating elements (or panic if this allowed deleting directory
                // entries).  Name-based enumeration may give a temporally inconsistent view of the
                // directory, so neither approach is ideal.
                unreachable!()
            }
            TraversalPosition::End => return Ok((TraversalPosition::End, sink.seal().into())),
        };

        while let Some(next) = remaining.next() {
            let next = next.to_string();
            match sink.append(&EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY), &next) {
                AppendResult::Ok(new_sink) => sink = new_sink,
                AppendResult::Sealed(sealed) => {
                    // Ran out of response buffer space. Pick up on this item next time.
                    return Ok((TraversalPosition::Name(next), sealed));
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
    ) -> Result<(), zx::Status> {
        Err(zx::Status::NOT_SUPPORTED)
    }

    // `register_watcher` is unsupported so this is a no-op.
    fn unregister_watcher(self: Arc<Self>, _: usize) {}

    async fn get_attrs(&self) -> Result<NodeAttributes, zx::Status> {
        Ok(NodeAttributes {
            mode: MODE_TYPE_DIRECTORY,
            id: 1,
            content_size: 0,
            storage_size: 0,
            link_count: 1,
            creation_time: 0,
            modification_time: 0,
        })
    }

    fn close(&self) -> Result<(), zx::Status> {
        Ok(())
    }
}

impl vfs::directory::entry::DirectoryEntry for PkgfsVersions {
    fn open(
        self: Arc<Self>,
        scope: ExecutionScope,
        flags: u32,
        mode: u32,
        mut path: VfsPath,
        server_end: ServerEnd<NodeMarker>,
    ) {
        // This directory and all child nodes are read-only
        if flags
            & (OPEN_RIGHT_WRITABLE
                | OPEN_RIGHT_ADMIN
                | OPEN_FLAG_CREATE
                | OPEN_FLAG_CREATE_IF_ABSENT
                | OPEN_FLAG_TRUNCATE
                | OPEN_FLAG_APPEND)
            != 0
        {
            return send_on_open_with_error(flags, server_end, zx::Status::NOT_SUPPORTED);
        }

        scope.clone().spawn(async move {
            // The `path.next()` above pops of the next path element, but `path`
            // still holds any remaining path elements.
            match path.next().map(Hash::from_str) {
                None => ImmutableConnection::create_connection(
                    scope,
                    OpenDirectory::new(self as Arc<dyn ImmutableConnectionClient>),
                    flags,
                    server_end,
                ),
                Some(Ok(package_hash)) => match self.validate_package(&package_hash, flags).await {
                    PackageValidationStatus::NotFound => {
                        send_on_open_with_error(flags, server_end, zx::Status::NOT_FOUND)
                    }
                    PackageValidationStatus::AccessDenied => {
                        send_on_open_with_error(flags, server_end, zx::Status::ACCESS_DENIED)
                    }
                    PackageValidationStatus::Ok => {
                        if let Err(e) = package_directory::serve_path(
                            scope,
                            self.blobfs.clone(),
                            package_hash,
                            flags,
                            mode,
                            path,
                            server_end,
                        )
                        .await
                        {
                            fx_log_err!(
                                "Unable to serve package for {}: {:#}",
                                package_hash,
                                anyhow!(e)
                            );
                        }
                    }
                },
                Some(Err(_)) => {
                    // Names that are not valid hashes can't exist in this directory.
                    send_on_open_with_error(flags, server_end, zx::Status::NOT_FOUND)
                }
            }
        })
    }

    fn entry_info(&self) -> EntryInfo {
        EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{compat::pkgfs::testing::FakeSink, index::register_dynamic_package},
        fidl_fuchsia_io::OPEN_RIGHT_READABLE,
        fuchsia_pkg::{PackagePath, PackageVariant},
        fuchsia_pkg_testing::{blobfs::Fake as FakeBlobfs, PackageBuilder},
        maplit::btreeset,
        matches::assert_matches,
        vfs::directory::{entry::EntryInfo, entry_container::Directory},
    };

    impl PkgfsVersions {
        pub fn new_test(
            static_packages: system_image::StaticPackages,
            non_static_allow_list: NonStaticAllowList,
            executability_restrictions: ExecutabilityRestrictions,
        ) -> (Arc<Self>, Arc<Mutex<PackageIndex>>) {
            let (blobfs, _) = blobfs::Client::new_mock();
            let index = Arc::new(Mutex::new(PackageIndex::new_test()));

            (
                Arc::new(PkgfsVersions::new(
                    static_packages,
                    Arc::clone(&index),
                    non_static_allow_list,
                    executability_restrictions,
                    blobfs,
                )),
                index,
            )
        }

        fn proxy(self: &Arc<Self>, flags: u32) -> fidl_fuchsia_io::DirectoryProxy {
            let (proxy, server_end) =
                fidl::endpoints::create_proxy::<fidl_fuchsia_io::DirectoryMarker>().unwrap();

            let () = ImmutableConnection::create_connection(
                ExecutionScope::new(),
                OpenDirectory::new(Arc::clone(self) as Arc<dyn ImmutableConnectionClient>),
                flags,
                server_end.into_channel().into(),
            );

            proxy
        }
    }

    fn hash(n: u8) -> Hash {
        Hash::from([n; 32])
    }

    fn default_package_path(name: &str) -> PackagePath {
        PackagePath::from_name_and_variant(
            name.parse().unwrap(),
            PackageVariant::from_str("0").unwrap(),
        )
    }

    fn non_static_allow_list(names: &[&str]) -> NonStaticAllowList {
        NonStaticAllowList::parse(names.join("\n").as_bytes()).unwrap()
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn unions_static_and_dynamic() {
        let (pkgfs_versions, package_index) = PkgfsVersions::new_test(
            system_image::StaticPackages::from_entries(vec![(
                default_package_path("static_package"),
                hash(0),
            )]),
            non_static_allow_list(&[]),
            ExecutabilityRestrictions::Enforce,
        );

        register_dynamic_package(&package_index, default_package_path("dynamic_package"), hash(1))
            .await;

        assert_eq!(pkgfs_versions.package_hashes().await, btreeset! {hash(0), hash(1)},);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn readdir_empty() {
        let (pkgfs_versions, _package_index) = PkgfsVersions::new_test(
            system_image::StaticPackages::from_entries(vec![]),
            non_static_allow_list(&[]),
            ExecutabilityRestrictions::Enforce,
        );

        // An empty readdir buffer can't hold any elements, so it returns nothing and indicates we
        // are still at the start.
        let (pos, sealed) = Directory::read_dirents(
            &*pkgfs_versions,
            &TraversalPosition::Start,
            Box::new(FakeSink::new(0)),
        )
        .await
        .expect("read_dirents failed");

        assert_eq!(FakeSink::from_sealed(sealed).entries, vec![]);
        assert_eq!(pos, TraversalPosition::Start);

        // Given adequate buffer space, the only entry is itself (".").
        let (pos, sealed) = Directory::read_dirents(
            &*pkgfs_versions,
            &TraversalPosition::Start,
            Box::new(FakeSink::new(100)),
        )
        .await
        .expect("read_dirents failed");

        assert_eq!(
            FakeSink::from_sealed(sealed).entries,
            vec![(".".to_owned(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)),]
        );
        assert_eq!(pos, TraversalPosition::End);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn readdir_enumerates_all_entries() {
        let (pkgfs_versions, package_index) = PkgfsVersions::new_test(
            system_image::StaticPackages::from_entries(vec![
                (default_package_path("allowed"), hash(0)),
                (default_package_path("static"), hash(1)),
                (default_package_path("same-hash"), hash(2)),
            ]),
            non_static_allow_list(&[]),
            ExecutabilityRestrictions::Enforce,
        );

        register_dynamic_package(&package_index, default_package_path("same-hash"), hash(2)).await;
        register_dynamic_package(&package_index, default_package_path("allowed"), hash(10)).await;
        register_dynamic_package(&package_index, default_package_path("dynonly"), hash(14)).await;

        let (pos, sealed) = Directory::read_dirents(
            &*pkgfs_versions,
            &TraversalPosition::Start,
            Box::new(FakeSink::new(100)),
        )
        .await
        .expect("read_dirents failed");

        assert_eq!(
            FakeSink::from_sealed(sealed).entries,
            vec![
                (".".to_owned(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)),
                (hash(0).to_string(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)),
                (hash(1).to_string(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)),
                (hash(2).to_string(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)),
                (hash(10).to_string(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)),
                (hash(14).to_string(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)),
            ]
        );
        assert_eq!(pos, TraversalPosition::End);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn readdir_one_entry_at_a_time_yields_expected_entries() {
        let (pkgfs_versions, package_index) = PkgfsVersions::new_test(
            system_image::StaticPackages::from_entries(vec![
                (default_package_path("allowed"), hash(0)),
                (default_package_path("static"), hash(1)),
                (default_package_path("same-hash"), hash(2)),
            ]),
            non_static_allow_list(&[]),
            ExecutabilityRestrictions::Enforce,
        );

        register_dynamic_package(&package_index, default_package_path("same-hash"), hash(2)).await;
        register_dynamic_package(&package_index, default_package_path("allowed"), hash(10)).await;
        register_dynamic_package(&package_index, default_package_path("dynonly"), hash(14)).await;

        let expected_entries = vec![
            (
                ".".to_owned(),
                EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY),
                TraversalPosition::Name(hash(0).to_string()),
            ),
            (
                hash(0).to_string(),
                EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY),
                TraversalPosition::Name(hash(1).to_string()),
            ),
            (
                hash(1).to_string(),
                EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY),
                TraversalPosition::Name(hash(2).to_string()),
            ),
            (
                hash(2).to_string(),
                EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY),
                TraversalPosition::Name(hash(10).to_string()),
            ),
            (
                hash(10).to_string(),
                EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY),
                TraversalPosition::Name(hash(14).to_string()),
            ),
            (
                hash(14).to_string(),
                EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY),
                TraversalPosition::End,
            ),
        ];

        let mut pos = TraversalPosition::Start;

        for (name, info, expected_pos) in expected_entries {
            let (newpos, sealed) =
                Directory::read_dirents(&*pkgfs_versions, &pos, Box::new(FakeSink::new(1)))
                    .await
                    .expect("read_dirents failed");

            assert_eq!(FakeSink::from_sealed(sealed).entries, vec![(name, info)]);
            assert_eq!(newpos, expected_pos);

            pos = newpos;
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn executable_open_access_denied_not_allowlisted() {
        let (pkgfs_versions, package_index) = PkgfsVersions::new_test(
            system_image::StaticPackages::from_entries(vec![]),
            non_static_allow_list(&[]),
            ExecutabilityRestrictions::Enforce,
        );

        register_dynamic_package(&package_index, default_package_path("dynamic"), hash(1)).await;

        let proxy = pkgfs_versions.proxy(OPEN_RIGHT_READABLE | OPEN_RIGHT_EXECUTABLE);

        assert_matches!(
            io_util::directory::open_directory(&proxy, &hash(1).to_string(), OPEN_RIGHT_EXECUTABLE)
                .await,
            Err(io_util::node::OpenError::OpenError(zx::Status::ACCESS_DENIED))
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn executable_open_not_found() {
        let (pkgfs_versions, package_index) = PkgfsVersions::new_test(
            system_image::StaticPackages::from_entries(vec![(
                default_package_path("static"),
                hash(0),
            )]),
            non_static_allow_list(&[]),
            ExecutabilityRestrictions::Enforce,
        );

        register_dynamic_package(&package_index, default_package_path("dynamic"), hash(1)).await;

        let proxy = pkgfs_versions.proxy(OPEN_RIGHT_READABLE | OPEN_RIGHT_EXECUTABLE);

        assert_matches!(
            io_util::directory::open_directory(&proxy, &hash(2).to_string(), OPEN_RIGHT_EXECUTABLE)
                .await,
            Err(io_util::node::OpenError::OpenError(zx::Status::NOT_FOUND))
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn executable_open_not_found_no_enforcement() {
        let (pkgfs_versions, package_index) = PkgfsVersions::new_test(
            system_image::StaticPackages::from_entries(vec![(
                default_package_path("static"),
                hash(0),
            )]),
            non_static_allow_list(&[]),
            ExecutabilityRestrictions::DoNotEnforce,
        );

        register_dynamic_package(&package_index, default_package_path("dynamic"), hash(1)).await;

        let proxy = pkgfs_versions.proxy(OPEN_RIGHT_READABLE | OPEN_RIGHT_EXECUTABLE);

        assert_matches!(
            io_util::directory::open_directory(&proxy, &hash(2).to_string(), OPEN_RIGHT_EXECUTABLE)
                .await,
            Err(io_util::node::OpenError::OpenError(zx::Status::NOT_FOUND))
        );

        assert_matches!(
            io_util::directory::open_directory(&proxy, &hash(3).to_string(), OPEN_RIGHT_READABLE)
                .await,
            Err(io_util::node::OpenError::OpenError(zx::Status::NOT_FOUND))
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_entry_open_self() {
        let (pkgfs_versions, _package_index) = PkgfsVersions::new_test(
            system_image::StaticPackages::from_entries(vec![(
                default_package_path("static"),
                hash(0),
            )]),
            non_static_allow_list(&[]),
            ExecutabilityRestrictions::DoNotEnforce,
        );
        let (proxy, server_end) =
            fidl::endpoints::create_proxy::<fidl_fuchsia_io::DirectoryMarker>().unwrap();

        vfs::directory::entry::DirectoryEntry::open(
            pkgfs_versions,
            ExecutionScope::new(),
            OPEN_RIGHT_READABLE,
            0,
            VfsPath::dot(),
            server_end.into_channel().into(),
        );

        assert_eq!(
            files_async::readdir(&proxy).await.unwrap(),
            vec![files_async::DirEntry {
                name: hash(0).to_string(),
                kind: files_async::DirentKind::Directory
            },]
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_open_nested_package_and_file() {
        let pkg = PackageBuilder::new("dynamic")
            .add_resource_at("message", &b"test-content"[..])
            .build()
            .await
            .expect("created pkg");

        let (metafar_blob, content_blobs) = pkg.contents();
        let (blobfs_fake, blobfs_client) = FakeBlobfs::new();
        blobfs_fake.add_blob(metafar_blob.merkle, metafar_blob.contents);
        for content in content_blobs {
            blobfs_fake.add_blob(content.merkle, content.contents);
        }

        let package_index = Arc::new(Mutex::new(PackageIndex::new_test()));
        let pkgfs_versions = Arc::new(PkgfsVersions::new(
            system_image::StaticPackages::from_entries(vec![]),
            Arc::clone(&package_index),
            non_static_allow_list(&["dynamic"]),
            ExecutabilityRestrictions::Enforce,
            blobfs_client,
        ));

        register_dynamic_package(
            &package_index,
            default_package_path("dynamic"),
            metafar_blob.merkle,
        )
        .await;

        let (proxy, server_end) =
            fidl::endpoints::create_proxy::<fidl_fuchsia_io::DirectoryMarker>().unwrap();

        vfs::directory::entry::DirectoryEntry::open(
            Arc::clone(&pkgfs_versions),
            ExecutionScope::new(),
            OPEN_RIGHT_READABLE,
            MODE_TYPE_DIRECTORY,
            VfsPath::validate_and_split(metafar_blob.merkle.to_string()).unwrap(),
            server_end.into_channel().into(),
        );

        assert_eq!(
            files_async::readdir(&proxy).await.unwrap(),
            vec![
                files_async::DirEntry {
                    name: "message".to_string(),
                    kind: files_async::DirentKind::File
                },
                files_async::DirEntry {
                    name: "meta".to_string(),
                    kind: files_async::DirentKind::Directory
                },
            ]
        );

        let proxy = pkgfs_versions.proxy(OPEN_RIGHT_READABLE | OPEN_RIGHT_EXECUTABLE);
        let file_path = format!("{}/message", metafar_blob.merkle).to_string();
        let file =
            io_util::directory::open_file(&proxy, &file_path, OPEN_RIGHT_READABLE).await.unwrap();
        let message = io_util::file::read_to_string(&file).await.unwrap();
        assert_eq!(message, "test-content");
    }
}
