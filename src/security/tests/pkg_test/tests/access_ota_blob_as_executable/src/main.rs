// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Result},
    argh::{from_env, FromArgs},
    fidl::endpoints::{create_endpoints, create_proxy, ServerEnd},
    fidl_fuchsia_io as fio,
    fidl_fuchsia_mem::Buffer,
    fidl_fuchsia_pkg::{
        BlobId, BlobInfo, BlobInfoIteratorMarker, NeededBlobsMarker, PackageCacheMarker,
        PackageResolverMarker, PackageUrl,
    },
    fidl_fuchsia_sys2::{StorageAdminMarker, StorageIteratorMarker},
    fidl_fuchsia_update_installer::{
        Initiator, InstallerMarker, MonitorMarker, MonitorRequest, Options, RebootControllerMarker,
        State,
    },
    fidl_test_security_pkg::PackageServer_Marker,
    fuchsia_async::Task,
    fuchsia_component::client::connect_to_protocol,
    fuchsia_fs::directory::readdir,
    fuchsia_fs::{
        directory::{open_file, open_in_namespace},
        file,
    },
    fuchsia_hash::Hash,
    fuchsia_merkle::MerkleTree,
    fuchsia_zircon::{AsHandleRef, Rights, Status},
    futures::{channel::oneshot::channel, join, TryFutureExt as _, TryStreamExt},
    security_pkg_test_util::load_config,
    std::{convert::TryInto, fs::File},
    tracing::info,
};

const PKGFS_PATH: &str = "/pkgfs";
const DEFAULT_DOMAIN: &str = "fuchsia.com";
const HELLO_WORLD_PACKAGE_NAME: &str = "hello_world";
const HELLO_WORLD_V0_PACKAGED_BINARY_PATH: &str = "bin/hello_world_v0";
const HELLO_WORLD_V1_PACKAGED_BINARY_PATH: &str = "bin/hello_world_v1";

/// Flags for access_ota_blob_as_executable.
#[derive(FromArgs, Debug, PartialEq)]
pub struct Args {
    /// absolute path to hello world v0 meta.far file used for designating its
    /// merkle root hash.
    #[argh(option)]
    hello_world_v0_meta_far_path: String,
    /// absolute path to hello world v1 meta.far file used for designating its
    /// merkle root hash.
    #[argh(option)]
    hello_world_v1_meta_far_path: String,
    /// absolute path to v1 update package (update.far) file used for
    /// designating its merkle root hash.
    #[argh(option)]
    v1_update_far_path: String,
    /// absolute path to shared test configuration file understood by
    /// security_pkg_test_util::load_config().
    #[argh(option)]
    test_config_path: String,

    /// switch used by rust test runner.
    #[argh(switch)]
    // TODO(fxbug.dev/84729)
    #[allow(unused)]
    nocapture: bool,
}

struct ReadableExecutableResult {
    /// Status of attempt to open-as-readable and read.
    pub readable: Result<()>,
    /// Status of attempt to open-as-executable and read.
    pub executable: Result<()>,
}

impl ReadableExecutableResult {
    /// Signals whether both readable and executable results are ok.
    pub fn is_readable_executable_ok(&self) -> bool {
        self.readable.is_ok() && self.executable.is_ok()
    }

    /// Signals whether both readable and executable results are errors.
    pub fn is_readable_executable_err(&self) -> bool {
        self.readable.is_err() && self.executable.is_err()
    }

    /// Signals whether only executable result is error.
    pub fn is_executable_err(&self) -> bool {
        self.readable.is_ok() && self.executable.is_err()
    }
}

// Result of attempting to open executable in package several different ways.
struct AccessCheckResult {
    /// Result of opening via pkgfs-versions API.
    pub pkgfs_versions: Option<ReadableExecutableResult>,
    /// Result of opening via pkgfs-packages API.
    pub pkgfs_packages: Option<ReadableExecutableResult>,
    /// Result of opening via fuchsia.pkg/PackageCache.Open API.
    pub pkg_cache_open: Option<ReadableExecutableResult>,
    /// Result of opening via fuchsia.pkg/PackageCache.Get API.
    pub pkg_cache_get: Option<ReadableExecutableResult>,
    /// Result of opening via package resolver API with
    /// fuchsia-pkg://[domain]/[package]/0?hash=[hash].
    pub pkg_resolver_with_hash: Option<ReadableExecutableResult>,
    /// Result of opening via package resolver API with
    /// fuchsia-pkg://[domain]/[package]/0.
    pub pkg_resolver_without_hash: Option<ReadableExecutableResult>,
}

struct AccessCheckSelectors {
    /// Perform access check against pkgfs-versions API.
    pub pkgfs_versions: bool,
    /// Perform access check against pkgfs-packages API.
    pub pkgfs_packages: bool,
    /// Perform access check against pkg-cache's fuchsia.pkg/PackageCache.Open API.
    pub pkg_cache_open: bool,
    /// Perform access check against pkg-cache's fuchsia.pkg/PackageCache.Get API.
    pub pkg_cache_get: bool,
    /// Perform access check against package resolver API with
    /// fuchsia-pkg://[domain]/[package]/0?hash=[hash].
    pub pkg_resolver_with_hash: bool,
    /// Perform access check against package resolver API with
    /// fuchsia-pkg://[domain]/[package]/0.
    pub pkg_resolver_without_hash: bool,
}

impl AccessCheckSelectors {
    /// Enable all access checks.
    pub fn all() -> Self {
        Self {
            pkgfs_versions: true,
            pkgfs_packages: true,
            pkg_cache_open: true,
            pkg_cache_get: true,
            pkg_resolver_with_hash: true,
            pkg_resolver_without_hash: true,
        }
    }
}

// Request to check access control against an executable in a package.
struct AccessCheckConfig {
    /// The name of the package containing the executable.
    pub package_name: String,
    /// The domain name to use for resolving the package with a hash.
    pub domain_with_hash: String,
    /// The domain name to use for resolving the package without a hash.
    pub domain_without_hash: String,
    /// The test-package-local path for side-loading the package. This mechanism
    /// is used to compute the package hash.
    pub local_package_path: String,
    /// The resolved-package-local path for opening the executable.
    pub packaged_binary_path: String,
}

struct AccessCheckRequest {
    /// Configuration for executing access checks.
    config: AccessCheckConfig,
    /// Selection of specific access checks/APIs to execute.
    selectors: AccessCheckSelectors,
}

impl AccessCheckRequest {
    /// Execute the access checks encoded in this request.
    pub async fn perform_access_check(&self) -> AccessCheckResult {
        // Determine the hash (the merkle root of the package meta.far) of the package.
        let mut package = File::open(&self.config.local_package_path).unwrap();
        let package_merkle = MerkleTree::from_reader(&mut package).unwrap().root();
        let package_blob_id = BlobId { merkle_root: package_merkle.into() };
        let package_blob_info = BlobInfo {
            blob_id: package_blob_id,
            length: package.metadata().expect("access meta.far File metadata").len(),
        };

        // Open package via pkgfs-versions API.
        let pkgfs_versions_path = format!("{}/versions/{}", PKGFS_PATH, package_merkle);
        let pkgfs_versions_rx_result = if self.selectors.pkgfs_versions {
            info!(path = %pkgfs_versions_path, "Opening package from pkgfs-versions");
            let package_directory_proxy = open_in_namespace(
                &pkgfs_versions_path,
                fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_EXECUTABLE,
            )
            .unwrap();
            Some((
                self.attempt_readable(&package_directory_proxy).await,
                self.attempt_executable(&package_directory_proxy).await,
            ))
        } else {
            info!(path = %pkgfs_versions_path, "Skipping open package from pkgfs-versions");
            None
        };

        // Open package via pkgfs-packages API.
        let pkgfs_packages_path = format!("{}/packages/{}/0", PKGFS_PATH, self.config.package_name);
        let pkgfs_packages_rx_result = if self.selectors.pkgfs_packages {
            info!(path = %pkgfs_packages_path, "Opening package from pkgfs-packages");
            let package_directory_proxy = open_in_namespace(
                &pkgfs_packages_path,
                fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_EXECUTABLE,
            )
            .unwrap();
            Some((
                self.attempt_readable(&package_directory_proxy).await,
                self.attempt_executable(&package_directory_proxy).await,
            ))
        } else {
            info!(path = %pkgfs_packages_path, "Skipping open package from pkgfs-packages");
            None
        };

        // Open package as executable via pkg-cache's fuchsia.pkg/PackageCache.Open API.
        let pkg_cache_open_rx_result = if self.selectors.pkg_cache_open {
            info!(%package_merkle, "Opening package via fuchsia.pkg/PackageCache.Open");
            let pkg_cache_proxy = connect_to_protocol::<PackageCacheMarker>().unwrap();
            let (package_directory_proxy, package_directory_server_end) =
                create_proxy::<fio::DirectoryMarker>().unwrap();
            // The updated version of the package resolved during OTA is not added to the dynamic
            // index (because the system-updater adds it to the retained index before resolving it),
            // so attempting to PackageCache.Open it should fail with NOT_FOUND. We convert
            // NOT_FOUND into not readable and not executable so we can verify that the OTA
            // process does not create more executable packages. Non NOT_FOUND errors fail the test
            // to help catch misconfigurations of the test environment.
            match pkg_cache_proxy
                .open(&mut package_blob_id.clone(), package_directory_server_end)
                .await
                .unwrap()
            {
                Ok(()) => Some((
                    self.attempt_readable(&package_directory_proxy).await,
                    self.attempt_executable(&package_directory_proxy).await,
                )),
                Err(e) if Status::from_raw(e) == Status::NOT_FOUND => Some((
                    Status::ok(e).context("PackageCache.Open failed"),
                    Result::<Box<fidl_fuchsia_mem::Buffer>, _>::Err(Status::from_raw(e))
                        .context("PackageCache.Open failed"),
                )),
                Err(e) => panic!("unexpected PackageCache.Open error {:?}", e),
            }
        } else {
            info!(
                %package_merkle,
                "Skipping open package via fuchsia.pkg/PackageCache.Open"
            );
            None
        };

        // Open package as executable via pkg-cache's fuchsia.pkg/PackageCache.Get API.
        let pkg_cache_get_rx_result = if self.selectors.pkg_cache_get {
            info!(%package_merkle, "Opening package via fuchsia.pkg/PackageCache.Get");
            // In all of the uses of this check in these tests, the package's blobs are already
            // present in blobfs. This means we should not need to perform any of the parts of the
            // PackageCache.Get protocol (and therefore any of the parts of the
            // PackageCache.NeededBlobs protocol) that actually write blobs to blobfs.
            // If this stops being the case, one of the below assertions will trigger and the test
            // will fail (notifying a developer to update this test).
            let pkg_cache_proxy = connect_to_protocol::<PackageCacheMarker>().unwrap();
            let (needed_blobs, needed_blobs_server_end) =
                fidl::endpoints::create_proxy::<NeededBlobsMarker>().unwrap();
            let (package_directory_proxy, package_directory_server_end) =
                create_proxy::<fio::DirectoryMarker>().unwrap();

            let get_fut = pkg_cache_proxy
                .get(
                    &mut package_blob_info.clone(),
                    needed_blobs_server_end,
                    Some(package_directory_server_end),
                )
                .map_ok(|res| res.map_err(Status::from_raw));

            let (_meta_blob, meta_blob_server_end) =
                fidl::endpoints::create_proxy::<fio::FileMarker>().unwrap();

            // If the package is in base or active in the dynamic index, the server will close the
            // NeededBlobs channel with a `ZX_OK` epitaph.
            // Otherwise, the server will respond to OpenMetaBlob with Ok(false), because in these
            // tests all the package's blobs (including the meta.far) are already in blobfs.
            let do_missing_blobs = match needed_blobs.open_meta_blob(meta_blob_server_end).await {
                Err(fidl::Error::ClientChannelClosed { status: Status::OK, .. }) => false,
                Ok(Ok(false)) => true,
                Ok(r) => {
                    panic!("meta.far blob not cached: unexpected response {:?}", r)
                }
                Err(e) => {
                    panic!("meta.far blob not cached: unexpected FIDL error {:?}", e)
                }
            };

            if do_missing_blobs {
                let (blob_iterator, blob_iterator_server_end) =
                    fidl::endpoints::create_proxy::<BlobInfoIteratorMarker>().unwrap();
                let () = needed_blobs.get_missing_blobs(blob_iterator_server_end).unwrap();
                // Iterator should be empty because all content blobs should be in blobfs.
                assert!(blob_iterator.next().await.unwrap().is_empty());
            }

            // The initial PackageCache.Get request should complete now that the
            // PackageCache.NeededBlobs procedure has been performed.
            let () = get_fut.await.unwrap().unwrap();

            Some((
                self.attempt_readable(&package_directory_proxy).await,
                self.attempt_executable(&package_directory_proxy).await,
            ))
        } else {
            info!(
                %package_merkle,
                "Skipping open package via fuchsia.pkg/PackageCache.Get"
            );
            None
        };

        // Resolve package as executable via pkg-resolver API.
        let url_with_hash = format!(
            "fuchsia-pkg://{}/{}/0?hash={}",
            &self.config.domain_with_hash, &self.config.package_name, &package_merkle
        );
        let pkg_resolver_with_hash_rx_result = if self.selectors.pkg_resolver_with_hash {
            info!(%url_with_hash, "Opening package via pkg-resolver");
            let package_directory_proxy = self.resolve_package(&url_with_hash).await;
            Some((
                self.attempt_readable(&package_directory_proxy).await,
                self.attempt_executable(&package_directory_proxy).await,
            ))
        } else {
            info!(%url_with_hash, "Skipping open package via pkg-resolver");
            None
        };
        let url_without_hash = format!(
            "fuchsia-pkg://{}/{}/0",
            &self.config.domain_without_hash, &self.config.package_name
        );
        let pkg_resolver_without_hash_rx_result = if self.selectors.pkg_resolver_without_hash {
            info!(%url_without_hash, "Opening package via pkg-resolver");
            let package_directory_proxy = self.resolve_package(&url_without_hash).await;
            Some((
                self.attempt_readable(&package_directory_proxy).await,
                self.attempt_executable(&package_directory_proxy).await,
            ))
        } else {
            info!(%url_without_hash, "Skipping open package via pkg-resolver");
            None
        };

        // Check that all opened-as-executable buffers contain the same data.
        let buffers = vec![
            // ..._rx_result.1 contains Result<Box<Buffer>>.
            pkgfs_versions_rx_result.as_ref().map(|rx| &rx.1),
            pkgfs_packages_rx_result.as_ref().map(|rx| &rx.1),
            pkg_cache_open_rx_result.as_ref().map(|rx| &rx.1),
            pkg_cache_get_rx_result.as_ref().map(|rx| &rx.1),
            pkg_resolver_with_hash_rx_result.as_ref().map(|rx| &rx.1),
        ]
        .into_iter()
        .filter_map(|opt_rx| opt_rx) // Drop `None` (configured not to run).
        .map(Result::as_ref) // Do not consume values inside results.
        .filter_map(Result::ok) // Extract buffers from results.
        .collect();
        Self::check_buffer_consistency(&buffers);

        AccessCheckResult {
            pkgfs_versions: Self::pair_to_result(pkgfs_versions_rx_result),
            pkgfs_packages: Self::pair_to_result(pkgfs_packages_rx_result),
            pkg_cache_open: Self::pair_to_result(pkg_cache_open_rx_result),
            pkg_cache_get: Self::pair_to_result(pkg_cache_get_rx_result),
            pkg_resolver_with_hash: Self::pair_to_result(pkg_resolver_with_hash_rx_result),
            pkg_resolver_without_hash: Self::pair_to_result(pkg_resolver_without_hash_rx_result),
        }
    }

    async fn resolve_package(&self, package_url: &str) -> fio::DirectoryProxy {
        let (package_directory_proxy, package_directory_server_end) =
            create_proxy::<fio::DirectoryMarker>().unwrap();
        connect_to_protocol::<PackageResolverMarker>()
            .unwrap()
            .resolve(package_url, package_directory_server_end)
            .await
            .unwrap()
            .unwrap();
        package_directory_proxy
    }

    fn check_buffer_consistency(buffers: &Vec<&Box<Buffer>>) {
        if buffers.len() > 1 {
            info!("Checking consistency of {} buffers", buffers.len());
            let buffer1 = buffers[0];
            for i in 1..buffers.len() {
                let buffer2 = buffers[i];
                assert_eq!(buffer1.size, buffer2.size);
                let mut buffer1_vec = vec![0; buffer1.size.try_into().unwrap()];
                let buffer1_data = buffer1_vec.as_mut_slice();
                buffer1.vmo.read(buffer1_data, 0).unwrap();
                let mut buffer2_vec = vec![0; buffer2.size.try_into().unwrap()];
                let buffer2_data = buffer2_vec.as_mut_slice();
                buffer2.vmo.read(buffer2_data, 0).unwrap();
                assert_eq!(buffer1_data, buffer2_data);
            }
        } else {
            info!("Skipping buffer consistency check: No buffers");
        }
    }

    async fn attempt_executable(
        &self,
        package_directory_proxy: &fio::DirectoryProxy,
    ) -> Result<Box<Buffer>> {
        let bin_file = open_file(
            package_directory_proxy,
            &self.config.packaged_binary_path,
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_EXECUTABLE,
        )
        .await?;
        let vmo = bin_file
            .get_backing_memory(fio::VmoFlags::READ | fio::VmoFlags::EXECUTE)
            .await
            .unwrap()
            .map_err(Status::from_raw)?;
        let bin_info = vmo.basic_info().unwrap();
        assert_eq!(bin_info.rights & Rights::READ, Rights::READ);
        assert_eq!(bin_info.rights & Rights::EXECUTE, Rights::EXECUTE);
        let size = vmo.get_content_size()?;
        Ok(Box::new(Buffer { vmo, size }))
    }

    async fn attempt_readable(&self, package_directory_proxy: &fio::DirectoryProxy) -> Result<()> {
        let bin_file = open_file(
            package_directory_proxy,
            &self.config.packaged_binary_path,
            fio::OpenFlags::RIGHT_READABLE,
        )
        .await?;
        file::read(&bin_file).await.unwrap();
        Ok(())
    }

    fn pair_to_result(
        pair: Option<(Result<()>, Result<Box<Buffer>>)>,
    ) -> Option<ReadableExecutableResult> {
        pair.map(|rx| ReadableExecutableResult { readable: rx.0, executable: rx.1.map(|_| ()) })
    }
}

async fn get_storage_for_component_instance(moniker_prefix: &str) -> fio::DirectoryProxy {
    let storage_admin = connect_to_protocol::<StorageAdminMarker>().unwrap();
    let (storage_user_iterator, storage_user_iterator_server_end) =
        create_proxy::<StorageIteratorMarker>().unwrap();
    storage_admin
        .list_storage_in_realm(".", storage_user_iterator_server_end)
        .await
        .unwrap()
        .unwrap();
    let mut matching_storage_users = vec![];
    loop {
        let chunk = storage_user_iterator.next().await.unwrap();
        if chunk.is_empty() {
            break;
        }
        let mut matches: Vec<String> =
            chunk.into_iter().filter(|moniker| moniker.starts_with(moniker_prefix)).collect();
        matching_storage_users.append(&mut matches);
    }
    assert_eq!(1, matching_storage_users.len());
    let (proxy, server_end) = create_proxy::<fio::DirectoryMarker>().unwrap();
    storage_admin
        .open_component_storage(
            matching_storage_users.first().unwrap(),
            fio::OpenFlags::RIGHT_READABLE,
            fio::MODE_TYPE_DIRECTORY,
            ServerEnd::new(server_end.into_channel()),
        )
        .unwrap();
    proxy
}

async fn get_local_package_server_url() -> String {
    connect_to_protocol::<PackageServer_Marker>().unwrap().get_url().await.unwrap()
}

async fn get_hello_world_v1_update_merkle(v1_update_far_path: String) -> Hash {
    let (sender, receiver) = channel::<Hash>();
    Task::local(async move {
        let mut hello_world_v1_update = File::open(&v1_update_far_path).unwrap();
        let hello_world_v1_update_merkle =
            MerkleTree::from_reader(&mut hello_world_v1_update).unwrap().root();
        sender.send(hello_world_v1_update_merkle).unwrap();
    })
    .detach();
    receiver.await.unwrap()
}

async fn perform_update(update_url: &str) {
    let installer_proxy = connect_to_protocol::<InstallerMarker>().unwrap();
    let (monitor_client_end, monitor_server_end) = create_endpoints::<MonitorMarker>().unwrap();

    // Prevent reboot attempt by signalling that the client (this code) will
    // manage reboot via the provided RebootController.
    let (_reboot_controller_proxy, reboot_controller_server_end) =
        create_proxy::<RebootControllerMarker>().unwrap();
    installer_proxy
        .start_update(
            &mut PackageUrl { url: update_url.to_string() },
            Options {
                initiator: Some(Initiator::Service),
                allow_attach_to_existing_attempt: Some(false),
                should_write_recovery: Some(false),
                ..Options::EMPTY
            },
            monitor_client_end,
            Some(reboot_controller_server_end),
        )
        .await
        .unwrap()
        .unwrap();

    let mut monitor_stream = monitor_server_end.into_stream().unwrap();
    while let Some(request) = monitor_stream.try_next().await.unwrap() {
        match request {
            MonitorRequest::OnState { state, responder } => {
                info!("Update state change: {:#?}", state);
                responder.send().unwrap();
                match state {
                    State::WaitToReboot(_) => {
                        break;
                    }
                    State::Reboot(_)
                    | State::DeferReboot(_)
                    | State::Complete(_)
                    | State::FailPrepare(_)
                    | State::FailFetch(_) => {
                        panic!("Update entered unexpected terminal state: {:#?}", state);
                    }
                    _ => {}
                }
            }
        }
    }
}

#[fuchsia::test]
async fn access_ota_blob_as_executable() {
    info!("Starting access_ota_blob_as_executable test");
    let args @ Args {
        hello_world_v0_meta_far_path,
        hello_world_v1_meta_far_path,
        v1_update_far_path,
        test_config_path,
        ..
    } = &from_env();
    info!(?args, "Initalizing access_ota_blob_as_executable");

    // Load test environment configuration.
    let config = load_config(test_config_path);

    // Setup storage capabilities.
    let pkg_resolver_storage_proxy = get_storage_for_component_instance("./pkg-resolver").await;
    // TODO(fxbug.dev/88453): Need a test that confirms assumption: Production
    // configuration is an empty mutable storage directory.
    assert!(readdir(&pkg_resolver_storage_proxy).await.unwrap().is_empty());

    info!("Gathering data and connecting to package server");

    let (
        hello_world_v0_access_check,
        hello_world_v1_access_check,
        different_package_name_access_check,
    ) = (
        // Access check against base version of package: Should succeed before
        // and after update.
        AccessCheckRequest {
            config: AccessCheckConfig {
                package_name: HELLO_WORLD_PACKAGE_NAME.to_string(),
                domain_with_hash: config.update_domain.clone(),
                domain_without_hash: DEFAULT_DOMAIN.to_string(),
                local_package_path: hello_world_v0_meta_far_path.to_string(),
                packaged_binary_path: HELLO_WORLD_V0_PACKAGED_BINARY_PATH.to_string(),
            },
            selectors: AccessCheckSelectors::all(),
        },
        // Access check against updated version of package: All hash-qualified
        // checks should fail.
        AccessCheckRequest {
            config: AccessCheckConfig {
                package_name: HELLO_WORLD_PACKAGE_NAME.to_string(),
                domain_with_hash: config.update_domain.clone(),
                domain_without_hash: DEFAULT_DOMAIN.to_string(),
                local_package_path: hello_world_v1_meta_far_path.to_string(),
                packaged_binary_path: HELLO_WORLD_V1_PACKAGED_BINARY_PATH.to_string(),
            },
            selectors: AccessCheckSelectors {
                pkgfs_versions: true,
                pkg_cache_open: true,
                pkg_cache_get: true,
                pkg_resolver_with_hash: true,

                // Disable non-hash-qualified checks.
                pkgfs_packages: false,
                pkg_resolver_without_hash: false,
            },
        },
        // Access check for referring to package by a different name (but using
        // a hash to load the correct executable).
        AccessCheckRequest {
            config: AccessCheckConfig {
                package_name: "system_image".to_string(),
                domain_with_hash: config.update_domain.clone(),
                domain_without_hash: DEFAULT_DOMAIN.to_string(),
                local_package_path: hello_world_v1_meta_far_path.to_string(),
                packaged_binary_path: HELLO_WORLD_V1_PACKAGED_BINARY_PATH.to_string(),
            },
            selectors: AccessCheckSelectors {
                pkg_resolver_with_hash: true,

                // Disable most checks; only interested in package resolution.
                pkgfs_versions: false,
                pkg_cache_open: false,
                pkg_cache_get: false,
                pkgfs_packages: false,
                pkg_resolver_without_hash: false,
            },
        },
    );

    // Setup package server and perform pre-update access check.
    let (hello_world_v0_access_check_result, update_merkle, package_server_url) = join!(
        hello_world_v0_access_check.perform_access_check(),
        get_hello_world_v1_update_merkle(v1_update_far_path.to_string()),
        get_local_package_server_url()
    );

    // Pre-update base version access check: Access should always succeed.
    assert!(hello_world_v0_access_check_result.pkgfs_versions.unwrap().is_readable_executable_ok());
    assert!(hello_world_v0_access_check_result.pkgfs_packages.unwrap().is_readable_executable_ok());
    assert!(hello_world_v0_access_check_result.pkg_cache_open.unwrap().is_readable_executable_ok());
    assert!(hello_world_v0_access_check_result.pkg_cache_get.unwrap().is_readable_executable_ok());
    assert!(hello_world_v0_access_check_result
        .pkg_resolver_with_hash
        .unwrap()
        .is_readable_executable_ok());
    assert!(hello_world_v0_access_check_result
        .pkg_resolver_without_hash
        .unwrap()
        .is_readable_executable_ok());

    info!("Package server running on {}", package_server_url);

    // Placeholder assertion for well-formed local URL. Test will eventually use
    // URL to configure network connection for `pkg-resolver`.
    assert!(package_server_url.starts_with("https://localhost"));

    let update_url =
        format!("fuchsia-pkg://{}/update/0?hash={}", config.update_domain, update_merkle);

    info!(%update_url, "Initiating update");

    perform_update(&update_url).await;

    let hello_world_v1_access_check_result =
        hello_world_v1_access_check.perform_access_check().await;

    // Post-update new version access check: Access should fail on all
    // hash-qualified attempts to open as executable. Additionally, the system-updater adds
    // the OTA packages to the retained index before resolving them, preventing the packages from
    // appearing in the dynamic index, so we should not be able to obtain readable packages from
    // the pkgfs directories or PackageCache.Open.
    assert!(hello_world_v1_access_check_result
        .pkgfs_versions
        .unwrap()
        .is_readable_executable_err());
    assert!(hello_world_v1_access_check_result
        .pkg_cache_open
        .unwrap()
        .is_readable_executable_err());
    assert!(hello_world_v1_access_check_result.pkg_cache_get.unwrap().is_executable_err());
    assert!(hello_world_v1_access_check_result.pkg_resolver_with_hash.unwrap().is_executable_err());

    let hello_world_v0_access_check_result =
        hello_world_v0_access_check.perform_access_check().await;

    // Post-update base version access check: Access should always succeed.
    assert!(hello_world_v0_access_check_result.pkgfs_versions.unwrap().is_readable_executable_ok());
    assert!(hello_world_v0_access_check_result.pkgfs_packages.unwrap().is_readable_executable_ok());
    assert!(hello_world_v0_access_check_result.pkg_cache_open.unwrap().is_readable_executable_ok());
    assert!(hello_world_v0_access_check_result.pkg_cache_get.unwrap().is_readable_executable_ok());
    assert!(hello_world_v0_access_check_result
        .pkg_resolver_with_hash
        .unwrap()
        .is_readable_executable_ok());
    assert!(hello_world_v0_access_check_result
        .pkg_resolver_without_hash
        .unwrap()
        .is_readable_executable_ok());

    let different_package_name_access_check_result =
        different_package_name_access_check.perform_access_check().await;

    // Accessing via different package name: Should fail.
    assert!(different_package_name_access_check_result
        .pkg_resolver_with_hash
        .unwrap()
        .is_executable_err());
}
