// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::cache::Cache,
    crate::resolver::Resolver,
    anyhow::{anyhow, Context, Error},
    fidl::endpoints::{ClientEnd, DiscoverableProtocolMarker},
    fidl_fuchsia_io as fio,
    fidl_fuchsia_paver::{BootManagerMarker, Configuration, PaverMarker, PaverProxy},
    fidl_fuchsia_pkg::{PackageCacheMarker, PackageResolverMarker},
    fidl_fuchsia_update_installer::{InstallerMarker, InstallerProxy, RebootControllerMarker},
    fidl_fuchsia_update_installer_ext::{
        options::{Initiator, Options},
        start_update, UpdateAttempt,
    },
    fuchsia_async as fasync,
    fuchsia_component::{
        client::{App, AppBuilder},
        server::{NestedEnvironment, ServiceFs, ServiceObj},
    },
    fuchsia_zircon as zx,
    futures::prelude::*,
    std::io::Write,
    std::sync::Arc,
};

pub const UPDATER_URL: &str =
    "fuchsia-pkg://fuchsia.com/isolated-swd-components#meta/system-updater-isolated.cmx";

pub const DEFAULT_UPDATE_PACKAGE_URL: &str = "fuchsia-pkg://fuchsia.com/update";

pub struct Updater {
    _system_updater: App,
    _resolver: Arc<Resolver>,
    proxy: InstallerProxy,
    paver_proxy: PaverProxy,
    _env: NestedEnvironment,
}

impl Updater {
    /// Launch the system updater using the given components and board name.
    pub async fn launch(
        blobfs: ClientEnd<fio::DirectoryMarker>,
        paver: ClientEnd<fio::DirectoryMarker>,
        resolver: Arc<Resolver>,
        cache: Arc<Cache>,
        board_name: &str,
    ) -> Result<Self, Error> {
        Self::launch_with_components(blobfs, paver, resolver, cache, board_name, UPDATER_URL).await
    }

    /// Launch the system updater. This is the same as `launch`, except that it expects the path
    /// to the `system-updater` component to be provided.
    pub async fn launch_with_components(
        blobfs: ClientEnd<fio::DirectoryMarker>,
        paver: ClientEnd<fio::DirectoryMarker>,
        resolver: Arc<Resolver>,
        cache: Arc<Cache>,
        board_name: &str,
        updater_url: &str,
    ) -> Result<Self, Error> {
        let board_info_dir = tempfile::tempdir()?;
        let mut path = board_info_dir.path().to_owned();
        path.push("board");
        let mut file = std::fs::File::create(path).context("creating board file")?;
        file.write_all(board_name.as_bytes())?;
        drop(file);

        let updater = AppBuilder::new(updater_url)
            .add_handle_to_namespace("/blob".to_owned(), blobfs)
            .add_dir_to_namespace(
                "/config/build-info".to_owned(),
                std::fs::File::open(board_info_dir.into_path())?,
            )?;

        let mut fs: ServiceFs<ServiceObj<'_, ()>> = ServiceFs::new();
        let paver = Arc::new(paver);
        fs.add_proxy_service_to::<PaverMarker, _>(Arc::clone(&paver))
            .add_proxy_service_to::<PackageResolverMarker, _>(resolver.directory_request()?)
            .add_proxy_service_to::<PackageCacheMarker, _>(cache.directory_request()?)
            .add_proxy_service::<fidl_fuchsia_logger::LogSinkMarker, _>();

        let (paver_proxy, remote) =
            fidl::endpoints::create_proxy::<PaverMarker>().context("Creating paver proxy")?;
        fdio::service_connect_at(
            paver.channel(),
            PaverMarker::PROTOCOL_NAME,
            remote.into_channel(),
        )
        .context("Connecting to paver")?;

        #[allow(deprecated)]
        let env = fs.create_salted_nested_environment("isolated-swd-updater-env")?;
        fasync::Task::spawn(fs.collect()).detach();

        let updater = updater.spawn(env.launcher()).context("launching system updater")?;

        let proxy = updater
            .connect_to_protocol::<InstallerMarker>()
            .context("connect to fuchsia.update.installer.Installer")?;

        Ok(Self { _system_updater: updater, _resolver: resolver, proxy, paver_proxy, _env: env })
    }

    /// Perform an update, skipping the final reboot.
    /// If `update_package` is Some, use the given package URL as the URL for the update package.
    /// Otherwise, `system-updater` uses the default URL.
    /// This will not install any images to the recovery partitions.
    pub async fn install_update(
        &mut self,
        update_package: Option<&fuchsia_url::AbsolutePackageUrl>,
    ) -> Result<(), Error> {
        let update_package = match update_package {
            Some(url) => url.to_owned(),
            None => DEFAULT_UPDATE_PACKAGE_URL.parse().unwrap(),
        };

        let (reboot_controller, reboot_controller_server_end) =
            fidl::endpoints::create_proxy::<RebootControllerMarker>()
                .context("creating reboot controller proxy")?;
        let () = reboot_controller.detach().context("disabling automatic reboot")?;

        let attempt = start_update(
            &update_package,
            Options {
                initiator: Initiator::User,
                allow_attach_to_existing_attempt: false,
                should_write_recovery: false,
            },
            &self.proxy,
            Some(reboot_controller_server_end),
        )
        .await
        .context("starting system update")?;

        let () = Self::monitor_update_attempt(attempt).await.context("monitoring installation")?;

        let () = Self::activate_installed_slot(&self.paver_proxy)
            .await
            .context("activating installed slot")?;

        Ok(())
    }

    async fn monitor_update_attempt(mut attempt: UpdateAttempt) -> Result<(), Error> {
        while let Some(state) = attempt.try_next().await.context("fetching next update state")? {
            tracing::info!("Install: {:?}", state);
            if state.is_success() {
                return Ok(());
            } else if state.is_failure() {
                return Err(anyhow!("update attempt failed"));
            }
        }

        Err(anyhow!("unexpected end of update attempt"))
    }

    async fn activate_installed_slot(paver: &PaverProxy) -> Result<(), Error> {
        let (boot_manager, remote) = fidl::endpoints::create_proxy::<BootManagerMarker>()
            .context("Creating boot manager proxy")?;
        paver.find_boot_manager(remote).context("finding boot manager")?;

        let result = boot_manager.query_active_configuration().await;
        if let Err(fidl::Error::ClientChannelClosed { status: zx::Status::NOT_SUPPORTED, .. }) =
            result
        {
            // board does not actually support ABR, so return.
            tracing::info!("ABR not supported, not configuring slots.");
            return Ok(());
        }
        let result = result?;
        if result.is_ok() {
            // active slot is valid - assume that system-updater handled this for us.
            return Ok(());
        }

        // In recovery, the paver will return ZX_ERR_NOT_SUPPORTED to query_active_configuration(),
        // even on devices which support ABR. Handle this manually in case it is actually
        // supported.
        zx::Status::ok(
            boot_manager
                .set_configuration_active(Configuration::A)
                .await
                .context("Sending set active configuration request")?,
        )
        .context("Setting A to active configuration")?;
        Ok(())
    }
}

#[cfg(test)]
pub(crate) mod for_tests {
    use {
        super::*,
        crate::resolver::for_tests::{ResolverForTest, EMPTY_REPO_PATH},
        fidl_fuchsia_paver::PaverRequestStream,
        fuchsia_component_test::RealmBuilder,
        fuchsia_merkle::Hash,
        fuchsia_pkg_testing::{
            Package, PackageBuilder, Repository, RepositoryBuilder, SystemImageBuilder,
        },
        mock_paver::{MockPaverService, MockPaverServiceBuilder, PaverEvent},
        std::collections::HashMap,
    };

    #[cfg(test)]
    const TEST_CHANNEL: &str = "test";
    pub const TEST_UPDATER_URL: &str =
        "fuchsia-pkg://fuchsia.com/isolated-swd-tests#meta/system-updater-isolated.cmx";
    pub const TEST_REPO_URL: &str = "fuchsia-pkg://fuchsia.com";

    pub struct UpdaterBuilder {
        paver_builder: MockPaverServiceBuilder,
        packages: Vec<Package>,
        images: HashMap<String, Vec<u8>>,
        repo_url: fuchsia_url::RepositoryUrl,
    }

    impl UpdaterBuilder {
        /// Construct a new UpdateBuilder. Initially, this contains no images and an empty system
        /// image package.
        pub async fn new() -> UpdaterBuilder {
            UpdaterBuilder {
                paver_builder: MockPaverServiceBuilder::new(),
                packages: vec![SystemImageBuilder::new().build().await],
                images: HashMap::new(),
                repo_url: TEST_REPO_URL.parse().unwrap(),
            }
        }

        /// Add a package to the update package this builder will generate.
        pub fn add_package(mut self, package: Package) -> Self {
            self.packages.push(package);
            self
        }

        /// Add an image to the update package this builder will generate.
        pub fn add_image(mut self, name: &str, contents: &[u8]) -> Self {
            self.images.insert(name.to_owned(), contents.to_owned());
            self
        }

        /// Mutate the `MockPaverServiceBuilder` contained in this UpdaterBuilder.
        pub fn paver<F>(mut self, f: F) -> Self
        where
            F: FnOnce(MockPaverServiceBuilder) -> MockPaverServiceBuilder,
        {
            self.paver_builder = f(self.paver_builder);
            self
        }

        pub fn repo_url(mut self, url: &str) -> Self {
            self.repo_url = url.parse().expect("Valid URL supplied to repo_url()");
            self
        }

        /// Create an UpdateForTest from this UpdaterBuilder.
        /// This will construct an update package containing all packages and images added to the
        /// builder, create a repository containing the packages, and create a MockPaver.
        pub async fn build(self) -> UpdaterForTest {
            let mut update = PackageBuilder::new("update").add_resource_at(
                "packages.json",
                generate_packages_json(&self.packages, &self.repo_url.to_string()).as_bytes(),
            );
            for (name, data) in self.images.iter() {
                update = update.add_resource_at(name, data.as_slice());
            }

            let update = update.build().await.expect("Building update package");

            let repo = Arc::new(
                self.packages
                    .iter()
                    .fold(
                        RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH).add_package(&update),
                        |repo, package| repo.add_package(package),
                    )
                    .build()
                    .await
                    .expect("Building repo"),
            );
            let paver = Arc::new(self.paver_builder.build());

            UpdaterForTest {
                repo,
                paver,
                packages: self.packages,
                update_merkle_root: *update.meta_far_merkle_root(),
                repo_url: self.repo_url,
            }
        }

        #[cfg(test)]
        pub async fn build_and_run(self) -> UpdaterResult {
            self.build().await.run().await
        }
    }

    pub fn generate_packages_json(packages: &[Package], repo_url: &str) -> String {
        let package_urls: Vec<String> = packages
            .iter()
            .map(|p| format!("{}/{}/0?hash={}", repo_url, p.name(), p.meta_far_merkle_root()))
            .collect();

        let packages_json = serde_json::json!({
            "version": "1",
            "content": package_urls
        });
        serde_json::to_string(&packages_json).unwrap()
    }

    /// This wraps the `Updater` in order to reduce test boilerplate.
    /// Should be constructed using `UpdaterBuilder`.
    pub struct UpdaterForTest {
        pub repo: Arc<Repository>,
        pub paver: Arc<MockPaverService>,
        pub packages: Vec<Package>,
        pub update_merkle_root: Hash,
        pub repo_url: fuchsia_url::RepositoryUrl,
    }

    impl UpdaterForTest {
        #[cfg(test)]
        pub async fn run(self) -> UpdaterResult {
            let resolver = ResolverForTest::new(
                self.repo.clone(),
                self.repo_url.clone(),
                Some(TEST_CHANNEL.to_owned()),
                RealmBuilder::new().await.unwrap(),
            )
            .await
            .expect("Creating resolver");
            self.run_with_resolver(resolver).await
        }

        /// Run the system update, returning an `UpdaterResult` containing information about the
        /// result of the update.
        pub async fn run_with_resolver(self, resolver: ResolverForTest) -> UpdaterResult {
            let mut fs: ServiceFs<ServiceObj<'_, ()>> = ServiceFs::new();
            let paver_clone = Arc::clone(&self.paver);
            fs.add_fidl_service(move |stream: PaverRequestStream| {
                fasync::Task::spawn(
                    Arc::clone(&paver_clone)
                        .run_paver_service(stream)
                        .unwrap_or_else(|e| panic!("Failed to run mock paver: {:?}", e)),
                )
                .detach();
            });

            let (client, server) = fidl::endpoints::create_endpoints().expect("creating channel");
            fs.serve_connection(server).expect("Failed to start mock paver");
            fasync::Task::spawn(fs.collect()).detach();

            let mut updater = Updater::launch_with_components(
                resolver.cache.blobfs.root_dir_handle().expect("getting blobfs root handle"),
                client,
                Arc::clone(&resolver.resolver),
                Arc::clone(&resolver.cache.cache),
                "test",
                TEST_UPDATER_URL,
            )
            .await
            .expect("launching updater");

            let () = updater.install_update(None).await.expect("installing update");

            UpdaterResult {
                paver_events: self.paver.take_events(),
                resolver,
                packages: self.packages,
            }
        }
    }

    /// Contains information about the state of the system after the updater was run.
    pub struct UpdaterResult {
        /// All paver events received by the MockPaver during the update.
        pub paver_events: Vec<PaverEvent>,
        /// The resolver used by the updater.
        pub resolver: ResolverForTest,
        /// All the packages that should have been resolved by the update.
        pub packages: Vec<Package>,
    }

    impl UpdaterResult {
        /// Verify that all packages that should have been resolved by the update
        /// were resolved.
        pub async fn verify_packages(&self) -> Result<(), Error> {
            for package in self.packages.iter() {
                // we deliberately avoid the package resolver here,
                // as we want to make sure that the system-updater retrieved all the correct blobs.
                let dir = fidl_fuchsia_pkg_ext::cache::Client::from_proxy(
                    self.resolver.cache.cache.package_cache_proxy()?,
                )
                .open((*package.meta_far_merkle_root()).into())
                .await
                .context("opening package")?;

                package
                    .verify_contents(&dir.into_proxy())
                    .await
                    .expect("Package verification failed");
            }
            Ok(())
        }
    }
}

#[cfg(test)]
pub mod tests {
    use {
        super::for_tests::UpdaterBuilder,
        super::*,
        fidl_fuchsia_paver::{Asset, Configuration},
        fuchsia_pkg_testing::{make_current_epoch_json, PackageBuilder},
        mock_paver::PaverEvent,
    };

    #[fasync::run_singlethreaded(test)]
    pub async fn test_updater() -> Result<(), Error> {
        let data = "hello world!".as_bytes();
        let test_package = PackageBuilder::new("test_package")
            .add_resource_at("bin/hello", "this is a test".as_bytes())
            .add_resource_at("data/file", "this is a file".as_bytes())
            .add_resource_at("meta/test_package.cmx", "{}".as_bytes())
            .build()
            .await
            .context("Building test_package")?;
        let updater = UpdaterBuilder::new()
            .await
            .paver(|p| {
                // Emulate ABR not being supported
                p.boot_manager_close_with_epitaph(zx::Status::NOT_SUPPORTED)
            })
            .add_package(test_package)
            .add_image("zbi.signed", data)
            .add_image("fuchsia.vbmeta", data)
            .add_image("recovery", data)
            .add_image("epoch.json", make_current_epoch_json().as_bytes())
            .add_image("recovery.vbmeta", data);
        let result = updater.build_and_run().await;

        assert_eq!(
            result.paver_events,
            vec![
                PaverEvent::WriteAsset {
                    configuration: Configuration::A,
                    asset: Asset::Kernel,
                    payload: data.to_vec()
                },
                PaverEvent::WriteAsset {
                    configuration: Configuration::B,
                    asset: Asset::Kernel,
                    payload: data.to_vec()
                },
                PaverEvent::WriteAsset {
                    configuration: Configuration::A,
                    asset: Asset::VerifiedBootMetadata,
                    payload: data.to_vec()
                },
                PaverEvent::WriteAsset {
                    configuration: Configuration::B,
                    asset: Asset::VerifiedBootMetadata,
                    payload: data.to_vec()
                },
                PaverEvent::DataSinkFlush,
            ]
        );

        result.verify_packages().await.context("Verifying packages were correctly installed")?;
        Ok(())
    }
}
