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

const UPDATER_URL: &str = "fuchsia-pkg://fuchsia.com/isolated-swd#meta/system_updater-isolated.cmx";

pub struct Updater {}

impl Updater {
    pub async fn launch(
        blobfs: ClientEnd<DirectoryMarker>,
        paver: ClientEnd<DirectoryMarker>,
        cache: &Cache,
        resolver: &Resolver,
        board_name: &str,
    ) -> Result<(), Error> {
        let output = Updater::launch_with_components(
            blobfs,
            paver,
            cache,
            resolver,
            board_name,
            UPDATER_URL,
        )
        .await?;
        output.ok().context("Running the updater")?;
        Ok(())
    }

    async fn launch_with_components(
        blobfs: ClientEnd<DirectoryMarker>,
        paver: ClientEnd<DirectoryMarker>,
        cache: &Cache,
        resolver: &Resolver,
        board_name: &str,
        updater_url: &str,
    ) -> Result<Output, Error> {
        let board_info_dir = tempfile::tempdir()?;
        let mut path = board_info_dir.path().to_owned();
        path.push("board");
        let mut file = std::fs::File::create(path).context("creating board file")?;
        file.write(board_name.as_bytes())?;
        drop(file);

        let updater = AppBuilder::new(updater_url)
            .args(vec!["--skip-recovery", "true", "--reboot", "false"])
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

        let env = fs.create_salted_nested_environment("isolated-ota-updater-env")?;
        fasync::spawn(fs.collect());

        let output = updater
            .output(env.launcher())
            .context("launching system_updater")?
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
        if let Err(fidl::Error::ClientChannelClosed(zx::Status::NOT_SUPPORTED)) = result {
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

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::resolver::tests::{ResolverForTest, EMPTY_REPO_PATH},
        fidl_fuchsia_paver::{Asset, Configuration, PaverRequestStream},
        fidl_fuchsia_sys::TerminationReason,
        fuchsia_pkg_testing::{Package, PackageBuilder, RepositoryBuilder, SystemImageBuilder},
        fuchsia_zircon as zx,
        itertools::Itertools,
        mock_paver::{MockPaverServiceBuilder, PaverEvent},
        pkgfs,
        std::collections::HashMap,
    };

    const TEST_UPDATER_URL: &str =
        "fuchsia-pkg://fuchsia.com/isolated-ota-tests#meta/system_updater.cmx";
    const TEST_CHANNEL: &str = "test";
    const TEST_REPO_URL: &str = "fuchsia-pkg://fuchsia.com";

    struct UpdaterBuilder {
        paver_builder: MockPaverServiceBuilder,
        packages: Vec<Package>,
        images: HashMap<String, Vec<u8>>,
    }

    impl UpdaterBuilder {
        pub async fn new() -> UpdaterBuilder {
            UpdaterBuilder {
                paver_builder: MockPaverServiceBuilder::new(),
                packages: vec![SystemImageBuilder::new().build().await],
                images: HashMap::new(),
            }
        }

        pub fn add_package(mut self, package: Package) -> Self {
            self.packages.push(package);
            self
        }

        pub fn add_image(mut self, name: &str, contents: &[u8]) -> Self {
            self.images.insert(name.to_owned(), contents.to_owned());
            self
        }

        pub fn paver<F>(mut self, f: F) -> Self
        where
            F: FnOnce(MockPaverServiceBuilder) -> MockPaverServiceBuilder,
        {
            self.paver_builder = f(self.paver_builder);
            self
        }

        fn generate_packages(&self) -> String {
            // FIXME(51061): switch to new update package format.
            format!(
                "{}",
                self.packages.iter().format_with("\n", |p, f| f(&format_args!(
                    "{}/0={}",
                    p.name(),
                    p.meta_far_merkle_root()
                )))
            )
        }

        pub async fn build_and_run(self) -> UpdaterResult {
            let mut update = PackageBuilder::new("update")
                .add_resource_at("packages", self.generate_packages().as_bytes());
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

            let mut fs: ServiceFs<ServiceObj<'_, ()>> = ServiceFs::new();
            let paver_clone = Arc::clone(&paver);
            fs.add_fidl_service(move |stream: PaverRequestStream| {
                fasync::spawn(
                    Arc::clone(&paver_clone)
                        .run_paver_service(stream)
                        .unwrap_or_else(|e| panic!("Failed to run mock paver: {:?}", e)),
                );
            });

            let resolver = ResolverForTest::new(repo, TEST_REPO_URL, Some(TEST_CHANNEL.to_owned()))
                .await
                .expect("Creating resolver");

            let (client, server) = zx::Channel::create().expect("creating channel");
            fs.serve_connection(server).expect("Failed to start mock paver");
            fasync::spawn(fs.collect());

            let output = Updater::launch_with_components(
                resolver.cache.pkgfs.blobfs.root_dir_handle().expect("getting blobfs root handle"),
                ClientEnd::from(client),
                &resolver.cache.cache,
                &resolver.resolver,
                "test",
                TEST_UPDATER_URL,
            )
            .await
            .expect("launching updater");

            UpdaterResult {
                paver_events: paver.take_events(),
                resolver,
                output,
                packages: self.packages,
            }
        }
    }

    struct UpdaterResult {
        pub paver_events: Vec<PaverEvent>,
        resolver: ResolverForTest,
        pub output: Output,
        packages: Vec<Package>,
    }

    impl UpdaterResult {
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

    #[fasync::run_singlethreaded(test)]
    pub async fn test_updater() -> Result<(), Error> {
        let data = "hello world!".as_bytes();
        let hook = |p: &PaverEvent| {
            if let PaverEvent::QueryActiveConfiguration = p {
                return zx::Status::NOT_SUPPORTED;
            }
            zx::Status::OK
        };
        let test_package = PackageBuilder::new("test_package")
            .add_resource_at("bin/hello", "this is a test".as_bytes())
            .add_resource_at("data/file", "this is a file".as_bytes())
            .add_resource_at("meta/test_package.cmx", "{}".as_bytes())
            .build()
            .await
            .context("Building test_package")?;
        let updater = UpdaterBuilder::new()
            .await
            .paver(|p| p.call_hook(hook))
            .add_package(test_package)
            .add_image("zbi.signed", &data)
            .add_image("fuchsia.vbmeta", &data)
            .add_image("zedboot.signed", &data)
            .add_image("recovery.vbmeta", &data);
        let result = updater.build_and_run().await;

        if !result.output.stdout.is_empty() {
            eprintln!(
                "TEST: system updater stdout:\n{}",
                String::from_utf8_lossy(&result.output.stdout)
            );
        }
        if !result.output.stderr.is_empty() {
            eprintln!(
                "TEST: system updater stderr:\n{}",
                String::from_utf8_lossy(&result.output.stderr)
            );
        }

        assert_eq!(result.output.exit_status.reason(), TerminationReason::Exited);
        result.output.ok().context("System updater exited with error")?;

        assert_eq!(
            result.paver_events,
            vec![
                PaverEvent::QueryActiveConfiguration,
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
                PaverEvent::QueryActiveConfiguration,
                PaverEvent::SetConfigurationActive { configuration: Configuration::A },
            ]
        );

        result.verify_packages().await.context("Verifying packages were correctly installed")?;
        Ok(())
    }
}
