// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::{cache::Cache, resolver::Resolver},
    anyhow::{Context, Error},
    fidl::endpoints::{ClientEnd, DiscoverableService},
    fidl_fuchsia_io::DirectoryMarker,
    fidl_fuchsia_paver::{BootManagerMarker, Configuration, PaverMarker},
    fidl_fuchsia_pkg::{PackageCacheMarker, PackageResolverMarker},
    fuchsia_async as fasync,
    fuchsia_component::{
        client::{AppBuilder, Output},
        server::{ServiceFs, ServiceObj},
    },
    fuchsia_zircon::{self as zx, HandleBased},
    futures::prelude::*,
    std::io::Write,
    std::sync::Arc,
};

pub const UPDATER_URL: &str =
    "fuchsia-pkg://fuchsia.com/isolated-swd-components#meta/system-updater-isolated.cmx";

pub struct Updater {}

impl Updater {
    /// Perform an update using the given components, using the provided board name.
    /// If `update_package` is Some, use the given package URL as the URL for the update package.
    /// Otherwise, `system-updater` uses the default URL.
    /// This will not install any images to the recovery partitions.
    pub async fn launch(
        blobfs: ClientEnd<DirectoryMarker>,
        paver: ClientEnd<DirectoryMarker>,
        cache: Arc<Cache>,
        resolver: Arc<Resolver>,
        board_name: &str,
        update_package: Option<String>,
    ) -> Result<(), Error> {
        let output = Updater::launch_with_components(
            blobfs,
            paver,
            cache,
            resolver,
            board_name,
            update_package,
            UPDATER_URL,
        )
        .await?;
        output.ok().context("Running the updater")?;
        Ok(())
    }

    /// Perform an update. This is the same as `launch`,
    /// except that it expects the path to the `system-updater` manifest to be provided.
    pub async fn launch_with_components(
        blobfs: ClientEnd<DirectoryMarker>,
        paver: ClientEnd<DirectoryMarker>,
        cache: Arc<Cache>,
        resolver: Arc<Resolver>,
        board_name: &str,
        update_package: Option<String>,
        updater_url: &str,
    ) -> Result<Output, Error> {
        let board_info_dir = tempfile::tempdir()?;
        let mut path = board_info_dir.path().to_owned();
        path.push("board");
        let mut file = std::fs::File::create(path).context("creating board file")?;
        file.write(board_name.as_bytes())?;
        drop(file);

        let update_package_ref = update_package.as_ref();
        let mut args = vec!["--skip-recovery", "true", "--reboot", "false", "--oneshot", "true"];
        if let Some(pkg) = update_package_ref {
            args.push("--update");
            args.push(pkg);
        }

        let updater = AppBuilder::new(updater_url)
            .args(args)
            .add_handle_to_namespace("/blob".to_owned(), blobfs.into_handle())
            .add_dir_to_namespace(
                "/config/build-info".to_owned(),
                std::fs::File::open(board_info_dir.into_path())?,
            )?;

        let mut fs: ServiceFs<ServiceObj<'_, ()>> = ServiceFs::new();
        let paver = Arc::new(paver.into_channel());
        fs.add_proxy_service_to::<PaverMarker, _>(Arc::clone(&paver))
            .add_proxy_service_to::<PackageCacheMarker, _>(cache.directory_request())
            .add_proxy_service_to::<PackageResolverMarker, _>(resolver.directory_request());

        let env = fs.create_salted_nested_environment("isolated-swd-updater-env")?;
        fasync::Task::spawn(fs.collect()).detach();

        let output = updater
            .output(env.launcher())
            .context("launching system updater")?
            .await
            .context("waiting for updater to exit")?;
        if output.ok().is_ok() {
            Updater::activate_installed_slot(&paver).await.context("activating installed slot")?;
        }

        Ok(output)
    }

    async fn activate_installed_slot(paver: &zx::Channel) -> Result<(), Error> {
        let (proxy, remote) =
            fidl::endpoints::create_proxy::<PaverMarker>().context("Creating paver proxy")?;
        fdio::service_connect_at(paver, PaverMarker::SERVICE_NAME, remote.into_channel())
            .context("Connecting to paver")?;

        let (boot_manager, remote) = fidl::endpoints::create_proxy::<BootManagerMarker>()
            .context("Creating boot manager proxy")?;
        proxy.find_boot_manager(remote).context("finding boot manager")?;

        let result = boot_manager.query_active_configuration().await;
        if let Err(fidl::Error::ClientChannelClosed { status: zx::Status::NOT_SUPPORTED, .. }) =
            result
        {
            // board does not actually support ABR, so return.
            println!("ABR not supported, not configuring slots.");
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

pub mod for_tests {
    use {
        super::*,
        crate::resolver::for_tests::{ResolverForTest, EMPTY_REPO_PATH},
        fidl_fuchsia_paver::PaverRequestStream,
        fuchsia_merkle::Hash,
        fuchsia_pkg_testing::{
            Package, PackageBuilder, Repository, RepositoryBuilder, SystemImageBuilder,
        },
        fuchsia_url::pkg_url::RepoUrl,
        fuchsia_zircon as zx,
        mock_paver::{MockPaverService, MockPaverServiceBuilder, PaverEvent},
        pkgfs,
        std::collections::HashMap,
    };

    #[cfg(test)]
    const TEST_CHANNEL: &str = "test";
    pub const TEST_UPDATER_URL: &str =
        "fuchsia-pkg://fuchsia.com/isolated-swd-tests#meta/system-updater.cmx";
    pub const TEST_REPO_URL: &str = "fuchsia-pkg://fuchsia.com";

    pub struct UpdaterBuilder {
        paver_builder: MockPaverServiceBuilder,
        packages: Vec<Package>,
        images: HashMap<String, Vec<u8>>,
        repo_url: RepoUrl,
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

    pub fn generate_packages_json(packages: &Vec<Package>, repo_url: &str) -> String {
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
        pub repo_url: RepoUrl,
    }

    impl UpdaterForTest {
        #[cfg(test)]
        pub async fn run(self) -> UpdaterResult {
            let resolver = ResolverForTest::new(
                self.repo.clone(),
                self.repo_url.clone(),
                Some(TEST_CHANNEL.to_owned()),
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

            let (client, server) = zx::Channel::create().expect("creating channel");
            fs.serve_connection(server).expect("Failed to start mock paver");
            fasync::Task::spawn(fs.collect()).detach();

            let output = Updater::launch_with_components(
                resolver.cache.pkgfs.blobfs.root_dir_handle().expect("getting blobfs root handle"),
                ClientEnd::from(client),
                Arc::clone(&resolver.cache.cache),
                Arc::clone(&resolver.resolver),
                "test",
                None,
                TEST_UPDATER_URL,
            )
            .await
            .expect("launching updater");

            UpdaterResult {
                paver_events: self.paver.take_events(),
                resolver,
                output: Some(output),
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
        /// The stdout/stderr output of the system updater.
        pub output: Option<Output>,
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
                let client = pkgfs::packages::Client::open_from_pkgfs_root(
                    &self.resolver.cache.pkgfs.root_proxy()?,
                )
                .context("opening pkgfs")?;
                let dir =
                    client.open_package(package.name(), None).await.context("opening package")?;
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
        fidl_fuchsia_sys::TerminationReason,
        fuchsia_pkg_testing::PackageBuilder,
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
            .add_image("zbi.signed", &data)
            .add_image("fuchsia.vbmeta", &data)
            .add_image("zedboot.signed", &data)
            .add_image("recovery.vbmeta", &data);
        let result = updater.build_and_run().await;

        let output = result.output.as_ref().unwrap();
        if !output.stdout.is_empty() {
            eprintln!("TEST: system updater stdout:\n{}", String::from_utf8_lossy(&output.stdout));
        }
        if !output.stderr.is_empty() {
            eprintln!("TEST: system updater stderr:\n{}", String::from_utf8_lossy(&output.stderr));
        }

        assert_eq!(output.exit_status.reason(), TerminationReason::Exited);
        output.ok().context("System updater exited with error")?;

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
