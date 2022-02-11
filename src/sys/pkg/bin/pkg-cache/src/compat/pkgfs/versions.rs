// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::BitFlags as _,
    crate::{base_packages::BasePackages, index::PackageIndex},
    anyhow::anyhow,
    async_trait::async_trait,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::{
        NodeAttributes, NodeMarker, DIRENT_TYPE_DIRECTORY, INO_UNKNOWN, MODE_TYPE_DIRECTORY,
        OPEN_FLAG_APPEND, OPEN_FLAG_CREATE, OPEN_FLAG_CREATE_IF_ABSENT, OPEN_FLAG_POSIX_DEPRECATED,
        OPEN_FLAG_POSIX_EXECUTABLE, OPEN_FLAG_POSIX_WRITABLE, OPEN_FLAG_TRUNCATE,
        OPEN_RIGHT_EXECUTABLE, OPEN_RIGHT_WRITABLE,
    },
    fuchsia_hash::Hash,
    fuchsia_syslog::fx_log_err,
    fuchsia_zircon as zx,
    futures::lock::Mutex,
    std::{collections::BTreeMap, str::FromStr, sync::Arc},
    system_image::{ExecutabilityRestrictions, NonStaticAllowList},
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
    non_base_packages: Arc<Mutex<PackageIndex>>,
    non_static_allow_list: Arc<NonStaticAllowList>,
    executability_restrictions: ExecutabilityRestrictions,
    blobfs: blobfs::Client,
}

impl PkgfsVersions {
    pub fn new(
        base_packages: Arc<BasePackages>,
        non_base_packages: Arc<Mutex<PackageIndex>>,
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
        let active_packages = self.non_base_packages.lock().await.active_packages();
        self.base_packages
            .paths_to_hashes()
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
        let flags = flags.unset(OPEN_FLAG_POSIX_WRITABLE);
        let flags = if flags.is_any_set(OPEN_FLAG_POSIX_DEPRECATED) {
            flags.unset(OPEN_FLAG_POSIX_DEPRECATED).set(OPEN_FLAG_POSIX_EXECUTABLE)
        } else {
            flags
        };

        // This directory and all child nodes are read-only
        if flags.is_any_set(
            OPEN_RIGHT_WRITABLE
                | OPEN_FLAG_CREATE
                | OPEN_FLAG_CREATE_IF_ABSENT
                | OPEN_FLAG_TRUNCATE
                | OPEN_FLAG_APPEND,
        ) {
            return send_on_open_with_error(flags, server_end, zx::Status::NOT_SUPPORTED);
        }

        scope.clone().spawn(async move {
            // The `path.next()` above pops of the next path element, but `path`
            // still holds any remaining path elements.
            match path.next().map(Hash::from_str) {
                None => ImmutableConnection::create_connection(scope, self, flags, server_end),
                Some(Ok(package_hash)) => {
                    let package_status = get_package_status(
                        self.base_packages.as_ref(),
                        self.non_base_packages.as_ref(),
                        &package_hash,
                    )
                    .await;
                    if let PackageStatus::Other = package_status {
                        let () = send_on_open_with_error(flags, server_end, zx::Status::NOT_FOUND);
                        return;
                    }

                    let executability_status = executability_status(
                        self.executability_restrictions,
                        &package_status,
                        self.non_static_allow_list.as_ref(),
                    );
                    let executablity_requested = flags.is_any_set(OPEN_RIGHT_EXECUTABLE);
                    let flags = match (executability_status, executablity_requested) {
                        (ExecutabilityStatus::Forbidden, true) => {
                            let () = send_on_open_with_error(
                                flags,
                                server_end,
                                zx::Status::ACCESS_DENIED,
                            );
                            return;
                        }
                        (ExecutabilityStatus::Forbidden, false) => {
                            flags.unset(OPEN_FLAG_POSIX_EXECUTABLE)
                        }
                        (ExecutabilityStatus::Allowed, _) => flags,
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
                        fx_log_err!(
                            "Unable to serve package for {}: {:#}",
                            package_hash,
                            anyhow!(e)
                        );
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
        EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)
    }
}

enum PackageStatus {
    Base,
    Active(fuchsia_pkg::PackageName),
    Other,
}

async fn get_package_status(
    base_packages: &BasePackages,
    package_index: &Mutex<PackageIndex>,
    package: &fuchsia_hash::Hash,
) -> PackageStatus {
    if base_packages.paths_to_hashes().any(|(_, hash)| hash == package) {
        return PackageStatus::Base;
    }

    match package_index.lock().await.get_name_if_active(package) {
        Some(name) => PackageStatus::Active(name.clone()),
        None => PackageStatus::Other,
    }
}

enum ExecutabilityStatus {
    Allowed,
    Forbidden,
}

fn executability_status(
    executability_restrictions: system_image::ExecutabilityRestrictions,
    package_status: &PackageStatus,
    non_static_allow_list: &system_image::NonStaticAllowList,
) -> ExecutabilityStatus {
    use {system_image::ExecutabilityRestrictions::*, ExecutabilityStatus::*, PackageStatus::*};
    match (executability_restrictions, package_status) {
        (Enforce, Base) => Allowed,
        (Enforce, Active(name)) => {
            if non_static_allow_list.allows(name) {
                Allowed
            } else {
                Forbidden
            }
        }
        (Enforce, Other) => Forbidden,
        (DoNotEnforce, _) => Allowed,
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{compat::pkgfs::testing::FakeSink, index::register_dynamic_package},
        assert_matches::assert_matches,
        blobfs_ramdisk::BlobfsRamdisk,
        fidl_fuchsia_io::{OPEN_FLAG_POSIX_EXECUTABLE, OPEN_RIGHT_READABLE},
        fuchsia_pkg::{PackagePath, PackageVariant},
        fuchsia_pkg_testing::{Package, PackageBuilder},
        std::collections::HashSet,
        vfs::directory::{entry::EntryInfo, entry_container::Directory},
    };

    impl PkgfsVersions {
        fn proxy(self: &Arc<Self>, flags: u32) -> fidl_fuchsia_io::DirectoryProxy {
            let (proxy, server_end) =
                fidl::endpoints::create_proxy::<fidl_fuchsia_io::DirectoryMarker>().unwrap();

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
        package_index: Arc<Mutex<PackageIndex>>,
    }

    impl TestEnv {
        pub fn new(
            base_packages: Vec<(PackagePath, Hash)>,
            non_static_allow_list: NonStaticAllowList,
            executability_restrictions: ExecutabilityRestrictions,
            packages_on_disk: &[&Package],
        ) -> (Self, Arc<PkgfsVersions>) {
            let blobfs = BlobfsRamdisk::start().unwrap();

            for pkg in packages_on_disk {
                pkg.write_to_blobfs_dir(&blobfs.root_dir().unwrap());
            }

            let index = Arc::new(Mutex::new(PackageIndex::new_test()));
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
            vec![(create_path("base_package"), hash(0))],
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
        let (_env, pkgfs_versions) = TestEnv::new(
            vec![],
            non_static_allow_list(&[]),
            ExecutabilityRestrictions::Enforce,
            &[],
        );

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
        let (env, pkgfs_versions) = TestEnv::new(
            vec![
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
    async fn executable_open_access_denied_not_allowlisted() {
        let (env, pkgfs_versions) = TestEnv::new(
            vec![],
            non_static_allow_list(&[]),
            ExecutabilityRestrictions::Enforce,
            &[],
        );

        register_dynamic_package(&env.package_index, create_path("dynamic"), hash(1)).await;

        let proxy = pkgfs_versions.proxy(OPEN_RIGHT_READABLE | OPEN_RIGHT_EXECUTABLE);

        assert_matches!(
            io_util::directory::open_directory(&proxy, &hash(1).to_string(), OPEN_RIGHT_EXECUTABLE)
                .await,
            Err(io_util::node::OpenError::OpenError(zx::Status::ACCESS_DENIED))
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_entry_open_strips_posix_exec() {
        let pkg = PackageBuilder::new("dynamic").build().await.unwrap();

        let (env, pkgfs_versions) = TestEnv::new(
            vec![],
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

        let proxy = pkgfs_versions.proxy(OPEN_RIGHT_READABLE | OPEN_RIGHT_EXECUTABLE);

        // Open a package directory with OPEN_FLAG_POSIX_EXECUTABLE set
        let pkg_dir = io_util::directory::open_directory(
            &proxy,
            &pkg.meta_far_merkle_root().to_string(),
            OPEN_RIGHT_READABLE | OPEN_FLAG_POSIX_EXECUTABLE,
        )
        .await
        .unwrap();

        // DirectoryEntry::open should have unset OPEN_FLAG_POSIX_EXECUTABLE (instead of allowing
        // the upgrade to OPEN_RIGHT_EXECUTABLE), so re-opening self with OPEN_RIGHT_EXECUTABLE
        // should be rejected.
        assert_matches!(
            io_util::directory::open_directory(
                &pkg_dir,
                ".",
                OPEN_RIGHT_READABLE | OPEN_RIGHT_EXECUTABLE
            )
            .await,
            Err(io_util::node::OpenError::OpenError(zx::Status::ACCESS_DENIED))
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_entry_open_strips_posix_write() {
        let (_env, pkgfs_versions) = TestEnv::new(
            vec![],
            non_static_allow_list(&[]),
            ExecutabilityRestrictions::Enforce,
            &[],
        );

        let proxy = pkgfs_versions.proxy(OPEN_RIGHT_READABLE | OPEN_FLAG_POSIX_WRITABLE);

        let (status, flags) = proxy.get_flags().await.unwrap();
        let () = zx::Status::ok(status).unwrap();
        assert_eq!(flags, OPEN_RIGHT_READABLE);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_entry_open_converts_posix_deprecated_to_posix_exec() {
        let (_env, pkgfs_versions) = TestEnv::new(
            vec![],
            non_static_allow_list(&[]),
            ExecutabilityRestrictions::Enforce,
            &[],
        );

        let proxy = pkgfs_versions.proxy(OPEN_RIGHT_READABLE | OPEN_FLAG_POSIX_DEPRECATED);

        let (status, flags) = proxy.get_flags().await.unwrap();
        let () = zx::Status::ok(status).unwrap();
        assert_eq!(flags, OPEN_RIGHT_READABLE | OPEN_RIGHT_EXECUTABLE);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_entry_open_not_found_takes_precedence_over_access_denied() {
        let (_env, pkgfs_versions) = TestEnv::new(
            vec![],
            non_static_allow_list(&[]),
            ExecutabilityRestrictions::Enforce,
            &[],
        );

        let proxy = pkgfs_versions.proxy(OPEN_RIGHT_READABLE | OPEN_RIGHT_EXECUTABLE);

        assert_matches!(
            io_util::directory::open_directory(&proxy, &hash(0).to_string(), OPEN_RIGHT_EXECUTABLE)
                .await,
            Err(io_util::node::OpenError::OpenError(zx::Status::NOT_FOUND))
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_entry_open_executable_no_enforcement_not_found() {
        let (_env, pkgfs_versions) = TestEnv::new(
            vec![],
            non_static_allow_list(&[]),
            ExecutabilityRestrictions::DoNotEnforce,
            &[],
        );

        let proxy = pkgfs_versions.proxy(OPEN_RIGHT_READABLE | OPEN_RIGHT_EXECUTABLE);

        assert_matches!(
            io_util::directory::open_directory(&proxy, &hash(0).to_string(), OPEN_RIGHT_EXECUTABLE)
                .await,
            Err(io_util::node::OpenError::OpenError(zx::Status::NOT_FOUND))
        );

        assert_matches!(
            io_util::directory::open_directory(&proxy, &hash(1).to_string(), OPEN_RIGHT_READABLE)
                .await,
            Err(io_util::node::OpenError::OpenError(zx::Status::NOT_FOUND))
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_entry_open_self() {
        let (_env, pkgfs_versions) = TestEnv::new(
            vec![(create_path("base"), hash(0))],
            non_static_allow_list(&[]),
            ExecutabilityRestrictions::DoNotEnforce,
            &[],
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

        let (env, pkgfs_versions) = TestEnv::new(
            vec![],
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

        let proxy = io_util::directory::open_directory(
            &pkgfs_versions.proxy(OPEN_RIGHT_READABLE | OPEN_RIGHT_EXECUTABLE),
            &pkg.meta_far_merkle_root().to_string(),
            OPEN_RIGHT_READABLE | OPEN_RIGHT_EXECUTABLE,
        )
        .await
        .unwrap();

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

        let file = io_util::directory::open_file(
            &proxy,
            "message",
            OPEN_RIGHT_READABLE | OPEN_RIGHT_EXECUTABLE,
        )
        .await
        .unwrap();
        let message = io_util::file::read_to_string(&file).await.unwrap();
        assert_eq!(message, "test-content");
    }
}
