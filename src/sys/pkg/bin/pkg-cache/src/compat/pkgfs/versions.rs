// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{base_packages::BasePackages, index::PackageIndex},
    anyhow::anyhow,
    async_trait::async_trait,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io as fio,
    fuchsia_hash::Hash,
    fuchsia_zircon as zx,
    std::{collections::BTreeMap, str::FromStr, sync::Arc},
    system_image::{ExecutabilityRestrictions, NonStaticAllowList},
    tracing::error,
    vfs::{
        common::send_on_open_with_error,
        directory::{
            connection::io1::DerivedConnection, entry::EntryInfo,
            immutable::connection::io1::ImmutableConnection, traversal_position::TraversalPosition,
        },
        execution_scope::ExecutionScope,
        path::Path as VfsPath,
    },
};

#[derive(Debug)]
pub struct PkgfsVersions {
    base_packages: Arc<BasePackages>,
    non_base_packages: Arc<async_lock::RwLock<PackageIndex>>,
    non_static_allow_list: Arc<NonStaticAllowList>,
    executability_restrictions: ExecutabilityRestrictions,
    blobfs: blobfs::Client,
}

impl PkgfsVersions {
    pub fn new(
        base_packages: Arc<BasePackages>,
        non_base_packages: Arc<async_lock::RwLock<PackageIndex>>,
        non_static_allow_list: Arc<NonStaticAllowList>,
        executability_restrictions: ExecutabilityRestrictions,
        blobfs: blobfs::Client,
    ) -> Self {
        Self {
            base_packages,
            non_base_packages,
            non_static_allow_list,
            executability_restrictions,
            blobfs,
        }
    }

    async fn directory_entries(&self) -> BTreeMap<String, super::DirentType> {
        let active_packages = self.non_base_packages.read().await.active_packages();
        self.base_packages
            .paths_and_hashes()
            .map(|(_path, hash)| hash.to_string())
            .chain(active_packages.into_iter().map(|(_path, hash)| hash.to_string()))
            .map(|hash| (hash, super::DirentType::Directory))
            .collect()
    }
}

#[async_trait]
impl vfs::directory::entry_container::Directory for PkgfsVersions {
    async fn read_dirents<'a>(
        &'a self,
        pos: &'a TraversalPosition,
        sink: Box<(dyn vfs::directory::dirents_sink::Sink + 'static)>,
    ) -> Result<
        (TraversalPosition, Box<(dyn vfs::directory::dirents_sink::Sealed + 'static)>),
        zx::Status,
    > {
        // If directory contents changes in between a client making paginated
        // fuchsia.io/Directory.ReadDirents calls, the client may not see a consistent snapshot
        // of the directory contents.
        super::read_dirents(&self.directory_entries().await, pos, sink).await
    }

    fn register_watcher(
        self: Arc<Self>,
        _: ExecutionScope,
        _: fio::WatchMask,
        _: vfs::directory::entry_container::DirectoryWatcher,
    ) -> Result<(), zx::Status> {
        Err(zx::Status::NOT_SUPPORTED)
    }

    // `register_watcher` is unsupported so this is a no-op.
    fn unregister_watcher(self: Arc<Self>, _: usize) {}

    async fn get_attrs(&self) -> Result<fio::NodeAttributes, zx::Status> {
        Ok(fio::NodeAttributes {
            mode: fio::MODE_TYPE_DIRECTORY,
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
        flags: fio::OpenFlags,
        mode: u32,
        mut path: VfsPath,
        server_end: ServerEnd<fio::NodeMarker>,
    ) {
        let flags = flags.difference(fio::OpenFlags::POSIX_WRITABLE);

        // This directory and all child nodes are read-only
        if flags.intersects(
            fio::OpenFlags::RIGHT_WRITABLE
                | fio::OpenFlags::CREATE
                | fio::OpenFlags::CREATE_IF_ABSENT
                | fio::OpenFlags::TRUNCATE
                | fio::OpenFlags::APPEND,
        ) {
            return send_on_open_with_error(flags, server_end, zx::Status::NOT_SUPPORTED);
        }

        scope.clone().spawn(async move {
            // The `path.next()` above pops of the next path element, but `path`
            // still holds any remaining path elements.
            match path.next().map(Hash::from_str) {
                None => ImmutableConnection::create_connection(scope, self, flags, server_end),
                Some(Ok(package_hash)) => {
                    let package_status = crate::cache_service::get_package_status(
                        self.base_packages.as_ref(),
                        self.non_base_packages.as_ref(),
                        &package_hash,
                    )
                    .await;
                    if let crate::cache_service::PackageStatus::Other = package_status {
                        let () = send_on_open_with_error(flags, server_end, zx::Status::NOT_FOUND);
                        return;
                    }

                    let executability_status = crate::cache_service::executability_status(
                        self.executability_restrictions,
                        &package_status,
                        self.non_static_allow_list.as_ref(),
                    );
                    let executablity_requested = flags.intersects(fio::OpenFlags::RIGHT_EXECUTABLE);
                    use crate::cache_service::ExecutabilityStatus::*;
                    let flags = match (executability_status, executablity_requested) {
                        (Forbidden, true) => {
                            let () = send_on_open_with_error(
                                flags,
                                server_end,
                                zx::Status::ACCESS_DENIED,
                            );
                            return;
                        }
                        (Forbidden, false) => flags.difference(fio::OpenFlags::POSIX_EXECUTABLE),
                        (Allowed, _) => flags,
                    };
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
                        error!("Unable to serve package for {}: {:#}", package_hash, anyhow!(e));
                    }
                }
                Some(Err(_)) => {
                    // Names that are not valid hashes can't exist in this directory.
                    send_on_open_with_error(flags, server_end, zx::Status::NOT_FOUND)
                }
            }
        })
    }

    fn entry_info(&self) -> EntryInfo {
        EntryInfo::new(fio::INO_UNKNOWN, fio::DirentType::Directory)
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{compat::pkgfs::testing::FakeSink, index::register_dynamic_package},
        assert_matches::assert_matches,
        blobfs_ramdisk::BlobfsRamdisk,
        fuchsia_pkg::{PackagePath, PackageVariant},
        fuchsia_pkg_testing::{Package, PackageBuilder},
        std::collections::HashSet,
        vfs::directory::{entry::EntryInfo, entry_container::Directory},
    };

    impl PkgfsVersions {
        fn proxy(self: &Arc<Self>, flags: fio::OpenFlags) -> fio::DirectoryProxy {
            let (proxy, server_end) =
                fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();

            vfs::directory::entry::DirectoryEntry::open(
                Arc::clone(&self),
                ExecutionScope::new(),
                flags,
                0,
                VfsPath::dot(),
                server_end.into_channel().into(),
            );

            proxy
        }
    }

    struct TestEnv {
        _blobfs: BlobfsRamdisk,
        package_index: Arc<async_lock::RwLock<PackageIndex>>,
    }

    impl TestEnv {
        pub fn new(
            base_packages: impl IntoIterator<Item = (PackagePath, Hash)>,
            non_static_allow_list: NonStaticAllowList,
            executability_restrictions: ExecutabilityRestrictions,
            packages_on_disk: &[&Package],
        ) -> (Self, Arc<PkgfsVersions>) {
            let blobfs = BlobfsRamdisk::start().unwrap();

            for pkg in packages_on_disk {
                pkg.write_to_blobfs_dir(&blobfs.root_dir().unwrap());
            }

            let index = Arc::new(async_lock::RwLock::new(PackageIndex::new_test()));
            let versions = PkgfsVersions::new(
                // PkgfsVersions only uses the path-hash mapping, so tests do not need to
                // populate the blob hashes.
                Arc::new(BasePackages::new_test_only(HashSet::new(), base_packages)),
                Arc::clone(&index),
                Arc::new(non_static_allow_list),
                executability_restrictions,
                blobfs.client(),
            );

            (Self { _blobfs: blobfs, package_index: index }, Arc::new(versions))
        }
    }

    fn hash(n: u8) -> Hash {
        Hash::from([n; 32])
    }

    fn create_path(name: &str) -> PackagePath {
        PackagePath::from_name_and_variant(
            name.parse().unwrap(),
            PackageVariant::from_str("0").unwrap(),
        )
    }

    fn non_static_allow_list(names: &[&str]) -> NonStaticAllowList {
        NonStaticAllowList::parse(names.join("\n").as_bytes()).unwrap()
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_entries_unions_base_and_dynamic() {
        let (env, pkgfs_versions) = TestEnv::new(
            [(create_path("base_package"), hash(0))],
            non_static_allow_list(&[]),
            ExecutabilityRestrictions::Enforce,
            &[],
        );

        register_dynamic_package(&env.package_index, create_path("dynamic_package"), hash(1)).await;

        assert_eq!(
            pkgfs_versions.directory_entries().await,
            BTreeMap::from([
                (hash(0).to_string(), super::super::DirentType::Directory),
                (hash(1).to_string(), super::super::DirentType::Directory),
            ])
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn readdir_empty() {
        let (_env, pkgfs_versions) =
            TestEnv::new([], non_static_allow_list(&[]), ExecutabilityRestrictions::Enforce, &[]);

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
            vec![(".".to_owned(), EntryInfo::new(fio::INO_UNKNOWN, fio::DirentType::Directory)),]
        );
        assert_eq!(pos, TraversalPosition::End);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn readdir_enumerates_all_entries() {
        let (env, pkgfs_versions) = TestEnv::new(
            [
                (create_path("allowed"), hash(0)),
                (create_path("base"), hash(1)),
                (create_path("same-hash"), hash(2)),
            ],
            non_static_allow_list(&[]),
            ExecutabilityRestrictions::Enforce,
            &[],
        );

        register_dynamic_package(&env.package_index, create_path("same-hash"), hash(2)).await;
        register_dynamic_package(&env.package_index, create_path("allowed"), hash(10)).await;
        register_dynamic_package(&env.package_index, create_path("dynonly"), hash(14)).await;

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
                (".".to_owned(), EntryInfo::new(fio::INO_UNKNOWN, fio::DirentType::Directory)),
                (hash(0).to_string(), EntryInfo::new(fio::INO_UNKNOWN, fio::DirentType::Directory)),
                (hash(1).to_string(), EntryInfo::new(fio::INO_UNKNOWN, fio::DirentType::Directory)),
                (hash(2).to_string(), EntryInfo::new(fio::INO_UNKNOWN, fio::DirentType::Directory)),
                (
                    hash(10).to_string(),
                    EntryInfo::new(fio::INO_UNKNOWN, fio::DirentType::Directory)
                ),
                (
                    hash(14).to_string(),
                    EntryInfo::new(fio::INO_UNKNOWN, fio::DirentType::Directory)
                ),
            ]
        );
        assert_eq!(pos, TraversalPosition::End);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn executable_open_access_denied_not_allowlisted() {
        let (env, pkgfs_versions) =
            TestEnv::new([], non_static_allow_list(&[]), ExecutabilityRestrictions::Enforce, &[]);

        register_dynamic_package(&env.package_index, create_path("dynamic"), hash(1)).await;

        let proxy =
            pkgfs_versions.proxy(fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_EXECUTABLE);

        assert_matches!(
            fuchsia_fs::directory::open_directory(
                &proxy,
                &hash(1).to_string(),
                fio::OpenFlags::RIGHT_EXECUTABLE
            )
            .await,
            Err(fuchsia_fs::node::OpenError::OpenError(zx::Status::ACCESS_DENIED))
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_entry_open_strips_posix_exec() {
        let pkg = PackageBuilder::new("dynamic").build().await.unwrap();

        let (env, pkgfs_versions) = TestEnv::new(
            [],
            non_static_allow_list(&[]),
            ExecutabilityRestrictions::Enforce,
            &[&pkg],
        );

        register_dynamic_package(
            &env.package_index,
            create_path("dynamic"),
            *pkg.meta_far_merkle_root(),
        )
        .await;

        let proxy =
            pkgfs_versions.proxy(fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_EXECUTABLE);

        // Open a package directory with OPEN_FLAG_POSIX_EXECUTABLE set
        let pkg_dir = fuchsia_fs::directory::open_directory(
            &proxy,
            &pkg.meta_far_merkle_root().to_string(),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::POSIX_EXECUTABLE,
        )
        .await
        .unwrap();

        // DirectoryEntry::open should have unset OPEN_FLAG_POSIX_EXECUTABLE (instead of allowing
        // the upgrade to fio::OpenFlags::RIGHT_EXECUTABLE), so re-opening self with fio::OpenFlags::RIGHT_EXECUTABLE
        // should be rejected.
        assert_matches!(
            fuchsia_fs::directory::open_directory(
                &pkg_dir,
                ".",
                fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_EXECUTABLE
            )
            .await,
            Err(fuchsia_fs::node::OpenError::OpenError(zx::Status::ACCESS_DENIED))
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_entry_open_strips_posix_write() {
        let (_env, pkgfs_versions) =
            TestEnv::new([], non_static_allow_list(&[]), ExecutabilityRestrictions::Enforce, &[]);

        let proxy =
            pkgfs_versions.proxy(fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::POSIX_WRITABLE);

        let (status, flags) = proxy.get_flags().await.unwrap();
        let () = zx::Status::ok(status).unwrap();
        assert_eq!(flags, fio::OpenFlags::RIGHT_READABLE);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_entry_open_not_found_takes_precedence_over_access_denied() {
        let (_env, pkgfs_versions) =
            TestEnv::new([], non_static_allow_list(&[]), ExecutabilityRestrictions::Enforce, &[]);

        let proxy =
            pkgfs_versions.proxy(fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_EXECUTABLE);

        assert_matches!(
            fuchsia_fs::directory::open_directory(
                &proxy,
                &hash(0).to_string(),
                fio::OpenFlags::RIGHT_EXECUTABLE
            )
            .await,
            Err(fuchsia_fs::node::OpenError::OpenError(zx::Status::NOT_FOUND))
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_entry_open_executable_no_enforcement_not_found() {
        let (_env, pkgfs_versions) = TestEnv::new(
            [],
            non_static_allow_list(&[]),
            ExecutabilityRestrictions::DoNotEnforce,
            &[],
        );

        let proxy =
            pkgfs_versions.proxy(fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_EXECUTABLE);

        assert_matches!(
            fuchsia_fs::directory::open_directory(
                &proxy,
                &hash(0).to_string(),
                fio::OpenFlags::RIGHT_EXECUTABLE
            )
            .await,
            Err(fuchsia_fs::node::OpenError::OpenError(zx::Status::NOT_FOUND))
        );

        assert_matches!(
            fuchsia_fs::directory::open_directory(
                &proxy,
                &hash(1).to_string(),
                fio::OpenFlags::RIGHT_READABLE
            )
            .await,
            Err(fuchsia_fs::node::OpenError::OpenError(zx::Status::NOT_FOUND))
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_entry_open_self() {
        let (_env, pkgfs_versions) = TestEnv::new(
            [(create_path("base"), hash(0))],
            non_static_allow_list(&[]),
            ExecutabilityRestrictions::DoNotEnforce,
            &[],
        );
        let (proxy, server_end) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();

        vfs::directory::entry::DirectoryEntry::open(
            pkgfs_versions,
            ExecutionScope::new(),
            fio::OpenFlags::RIGHT_READABLE,
            0,
            VfsPath::dot(),
            server_end.into_channel().into(),
        );

        assert_eq!(
            fuchsia_fs::directory::readdir(&proxy).await.unwrap(),
            vec![fuchsia_fs::directory::DirEntry {
                name: hash(0).to_string(),
                kind: fuchsia_fs::directory::DirentKind::Directory
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

        let (env, pkgfs_versions) = TestEnv::new(
            [],
            non_static_allow_list(&["dynamic"]),
            ExecutabilityRestrictions::Enforce,
            &[&pkg],
        );

        register_dynamic_package(
            &env.package_index,
            create_path("dynamic"),
            *pkg.meta_far_merkle_root(),
        )
        .await;

        let proxy = fuchsia_fs::directory::open_directory(
            &pkgfs_versions
                .proxy(fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_EXECUTABLE),
            &pkg.meta_far_merkle_root().to_string(),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_EXECUTABLE,
        )
        .await
        .unwrap();

        assert_eq!(
            fuchsia_fs::directory::readdir(&proxy).await.unwrap(),
            vec![
                fuchsia_fs::directory::DirEntry {
                    name: "message".to_string(),
                    kind: fuchsia_fs::directory::DirentKind::File
                },
                fuchsia_fs::directory::DirEntry {
                    name: "meta".to_string(),
                    kind: fuchsia_fs::directory::DirentKind::Directory
                },
            ]
        );

        let file = fuchsia_fs::directory::open_file(
            &proxy,
            "message",
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_EXECUTABLE,
        )
        .await
        .unwrap();
        let message = fuchsia_fs::file::read_to_string(&file).await.unwrap();
        assert_eq!(message, "test-content");
    }
}
