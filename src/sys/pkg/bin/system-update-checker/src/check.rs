// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        errors::{self, Error},
        update_manager::TargetChannelUpdater,
        DEFAULT_UPDATE_PACKAGE_URL,
    },
    anyhow::{anyhow, Context as _},
    fidl_fuchsia_paver::{
        Asset, BootManagerMarker, Configuration, DataSinkMarker, PaverMarker, PaverProxy,
    },
    fidl_fuchsia_pkg::{self as fpkg, PackageResolverMarker, PackageResolverProxyInterface},
    fidl_fuchsia_space as fidl_space,
    fuchsia_component::client::connect_to_protocol,
    fuchsia_hash::Hash,
    fuchsia_zircon as zx,
    std::{cmp::min, convert::TryInto as _, io},
    tracing::{error, info, warn},
    update_package::{ImageType, UpdatePackage},
};

#[derive(PartialEq, Eq, Debug, Clone)]
pub enum SystemUpdateStatus {
    UpToDate { system_image: Hash, update_package: Hash },
    UpdateAvailable { current_system_image: Hash, latest_system_image: Hash },
}

pub async fn check_for_system_update(
    last_known_update_package: Option<&Hash>,
    target_channel_manager: &dyn TargetChannelUpdater,
) -> Result<SystemUpdateStatus, Error> {
    let mut file_system = RealFileSystem;
    let package_resolver =
        connect_to_protocol::<PackageResolverMarker>().map_err(Error::ConnectPackageResolver)?;
    let paver = connect_to_protocol::<PaverMarker>().map_err(Error::ConnectPaver)?;
    let space_manager =
        connect_to_protocol::<fidl_space::ManagerMarker>().map_err(Error::ConnectSpaceManager)?;

    check_for_system_update_impl(
        DEFAULT_UPDATE_PACKAGE_URL,
        &mut file_system,
        &package_resolver,
        &paver,
        target_channel_manager,
        last_known_update_package,
        &space_manager,
    )
    .await
}

// For mocking
trait FileSystem {
    fn read_to_string(&self, path: &str) -> io::Result<String>;
    fn remove_file(&mut self, path: &str) -> io::Result<()>;
}

struct RealFileSystem;

impl FileSystem for RealFileSystem {
    fn read_to_string(&self, path: &str) -> io::Result<String> {
        std::fs::read_to_string(path)
    }
    fn remove_file(&mut self, path: &str) -> io::Result<()> {
        std::fs::remove_file(path)
    }
}

async fn check_for_system_update_impl(
    default_update_url: &str,
    file_system: &mut impl FileSystem,
    package_resolver: &impl PackageResolverProxyInterface,
    paver: &PaverProxy,
    target_channel_manager: &dyn TargetChannelUpdater,
    last_known_update_package: Option<&Hash>,
    space_manager: &fidl_space::ManagerProxy,
) -> Result<SystemUpdateStatus, Error> {
    let update_pkg = latest_update_package(
        default_update_url,
        package_resolver,
        target_channel_manager,
        space_manager,
    )
    .await?;
    let latest_update_merkle = update_pkg.hash().await.map_err(errors::UpdatePackage::Hash)?;
    let current_system_image = current_system_image_merkle(file_system)?;
    let latest_system_image = latest_system_image_merkle(&update_pkg).await?;

    let up_to_date = Ok(SystemUpdateStatus::UpToDate {
        system_image: current_system_image,
        update_package: latest_update_merkle,
    });

    if let Some(last_known_update_package) = last_known_update_package {
        if *last_known_update_package == latest_update_merkle {
            info!("Last known update package is latest, system is up to date");
            return up_to_date;
        }
    }

    let update_available =
        Ok(SystemUpdateStatus::UpdateAvailable { current_system_image, latest_system_image });

    if current_system_image != latest_system_image {
        info!("Current system image is not latest, system is not up to date");
        return update_available;
    }

    // When present, checking vbmeta is sufficient, but vbmeta isn't supported on all devices.
    for (image, asset) in [
        (ImageType::FuchsiaVbmeta, Asset::VerifiedBootMetadata),
        (ImageType::Zbi, Asset::Kernel),
        (ImageType::ZbiSigned, Asset::Kernel),
    ]
    .iter()
    {
        match is_image_up_to_date(&update_pkg, paver, *image, *asset).await {
            Ok(true) => {
                info!("Image {:?} is up to date, system is up to date", image);
                return up_to_date;
            }
            Ok(false) => {
                info!("Image {:?} is not latest, system is not up to date", image);
                return update_available;
            }
            Err(err) => {
                warn!("Failed to check if {} is up to date: {:#}", image.name(), anyhow!(err));
            }
        }
    }

    info!("Could not find any system differences, assuming system is up to date");
    up_to_date
}

fn current_system_image_merkle(file_system: &impl FileSystem) -> Result<Hash, Error> {
    file_system
        .read_to_string("/pkgfs/system/meta")
        .map_err(Error::ReadSystemMeta)?
        .parse::<Hash>()
        .map_err(Error::ParseSystemMeta)
}

async fn gc(space_manager: &fidl_space::ManagerProxy) -> Result<(), anyhow::Error> {
    let () = space_manager
        .gc()
        .await
        .context("while performing gc call")?
        .map_err(|e| anyhow!("garbage collection responded with {:?}", e))?;
    Ok(())
}

async fn latest_update_package(
    default_update_url: &str,
    package_resolver: &impl PackageResolverProxyInterface,
    channel_manager: &dyn TargetChannelUpdater,
    space_manager: &fidl_space::ManagerProxy,
) -> Result<UpdatePackage, errors::UpdatePackage> {
    match latest_update_package_attempt(default_update_url, package_resolver, channel_manager).await
    {
        Ok(update_package) => return Ok(update_package),
        Err(errors::UpdatePackage::Resolve(fidl_fuchsia_pkg_ext::ResolveError::NoSpace)) => {}
        Err(raw) => return Err(raw),
    }

    info!("No space left for update package.  Attempting to clean up older packages.");

    // If the first attempt fails with NoSpace, perform a GC and retry.
    if let Err(e) = gc(space_manager).await {
        error!("unable to gc packages: {:#}", anyhow!(e));
    }

    match latest_update_package_attempt(default_update_url, package_resolver, channel_manager).await
    {
        Ok(update_package) => Ok(update_package),
        Err(raw) => {
            error!("`latest_update_package_attempt` failed twice to fetch the update package.");
            Err(raw)
        }
    }
}

async fn latest_update_package_attempt(
    default_update_url: &str,
    package_resolver: &impl PackageResolverProxyInterface,
    channel_manager: &dyn TargetChannelUpdater,
) -> Result<UpdatePackage, errors::UpdatePackage> {
    let (dir_proxy, dir_server_end) =
        fidl::endpoints::create_proxy().map_err(errors::UpdatePackage::CreateDirectoryProxy)?;
    let update_package = channel_manager
        .get_target_channel_update_url()
        .unwrap_or_else(|| default_update_url.to_owned());
    let fut = package_resolver.resolve(&update_package, dir_server_end);
    let _: fpkg::ResolutionContext = fut
        .await
        .map_err(errors::UpdatePackage::ResolveFidl)?
        .map_err(|raw| errors::UpdatePackage::Resolve(raw.into()))?;
    Ok(UpdatePackage::new(dir_proxy))
}

async fn latest_system_image_merkle(
    update_package: &UpdatePackage,
) -> Result<Hash, errors::UpdatePackage> {
    let packages =
        update_package.packages().await.map_err(errors::UpdatePackage::ExtractPackagesManifest)?;
    let system_image = packages
        .into_iter()
        .find(|url| url.path() == "/system_image/0")
        .ok_or(errors::UpdatePackage::MissingSystemImage)?;
    let hash = system_image
        .hash()
        .ok_or_else(|| errors::UpdatePackage::UnPinnedSystemImage(system_image.clone()))?;
    Ok(hash.to_owned())
}

async fn is_image_up_to_date(
    update_package: &UpdatePackage,
    paver: &PaverProxy,
    image_type: ImageType,
    asset: Asset,
) -> Result<bool, anyhow::Error> {
    let latest_image =
        update_package.open_image(&update_package::Image::new(image_type, None)).await?;

    let (boot_manager, server_end) = fidl::endpoints::create_proxy::<BootManagerMarker>()?;
    let () = paver.find_boot_manager(server_end).context("connect to fuchsia.paver.BootManager")?;
    let configuration = match boot_manager.query_current_configuration().await {
        Ok(response) => {
            response.map_err(zx::Status::from_raw).context("querying current configuration")?
        }
        Err(fidl::Error::ClientChannelClosed { status: zx::Status::NOT_SUPPORTED, .. }) => {
            warn!("device does not support ABR. Checking image in slot A");
            Configuration::A
        }
        Err(err) => return Err(err).context("querying current configuration"),
    };

    let (data_sink, server_end) = fidl::endpoints::create_proxy::<DataSinkMarker>()?;
    let () = paver.find_data_sink(server_end).context("connect to fuchsia.paver.DataSink")?;

    let current_image = data_sink
        .read_asset(configuration, asset)
        .await?
        .map_err(|status| anyhow!("read_asset responded with {}", zx::Status::from_raw(status)))?;

    compare_buffer(latest_image, current_image)
}

// Compare two buffers and return true if they are equal.
// If they have different sizes, only compare up to the smaller size and ignoring the rest.
// This is because when reading asset from paver, we might get extra data after the image.
fn compare_buffer(
    a: fidl_fuchsia_mem::Buffer,
    b: fidl_fuchsia_mem::Buffer,
) -> Result<bool, anyhow::Error> {
    let a_size = a.size.try_into()?;
    let b_size = b.size.try_into()?;
    let size = min(a_size, b_size);
    let max_buffer_size = 32 * 1024;
    let step_size = min(size, max_buffer_size);
    let mut a_buf = vec![0u8; step_size];
    let mut b_buf = vec![0u8; step_size];
    for offset in (0..size).step_by(step_size) {
        let current_step_size = min(step_size, size - offset);
        a.vmo.read(&mut a_buf[..current_step_size], offset as u64)?;
        b.vmo.read(&mut b_buf[..current_step_size], offset as u64)?;
        if a_buf[..current_step_size] != b_buf[..current_step_size] {
            return Ok(false);
        }
    }
    Ok(true)
}

#[cfg(test)]
pub mod test_check_for_system_update_impl {
    use {
        super::*,
        crate::update_manager::tests::FakeTargetChannelUpdater,
        assert_matches::assert_matches,
        fidl_fuchsia_io as fio,
        fidl_fuchsia_paver::Configuration,
        fidl_fuchsia_pkg::{
            PackageResolverGetHashResult, PackageResolverResolveResult,
            PackageResolverResolveWithContextResult, PackageUrl,
        },
        fuchsia_async as fasync,
        futures::{future, TryFutureExt, TryStreamExt},
        lazy_static::lazy_static,
        maplit::hashmap,
        mock_paver::MockPaverServiceBuilder,
        parking_lot::Mutex,
        std::{collections::hash_map::HashMap, fs, sync::Arc},
    };

    const ACTIVE_SYSTEM_IMAGE_MERKLE: &str =
        "0000000000000000000000000000000000000000000000000000000000000000";
    const NEW_SYSTEM_IMAGE_MERKLE: &str =
        "1111111111111111111111111111111111111111111111111111111111111111";
    const TEST_UPDATE_PACKAGE_URL: &str = "fuchsia-pkg://fuchsia.test/update";

    lazy_static! {
        static ref UPDATE_PACKAGE_MERKLE: Hash = [0x22; 32].into();
    }

    struct FakeFileSystem {
        contents: HashMap<String, String>,
    }
    impl FakeFileSystem {
        fn new_with_valid_system_meta() -> FakeFileSystem {
            FakeFileSystem {
                contents: hashmap![
                    "/pkgfs/system/meta".to_string() => ACTIVE_SYSTEM_IMAGE_MERKLE.to_string()
                ],
            }
        }
    }
    impl FileSystem for FakeFileSystem {
        fn read_to_string(&self, path: &str) -> io::Result<String> {
            self.contents
                .get(path)
                .ok_or_else(|| {
                    io::Error::new(
                        io::ErrorKind::NotFound,
                        format!("not present in fake file system: {}", path),
                    )
                })
                .map(|s| s.to_string())
        }
        fn remove_file(&mut self, path: &str) -> io::Result<()> {
            self.contents.remove(path).and(Some(())).ok_or_else(|| {
                io::Error::new(
                    io::ErrorKind::NotFound,
                    format!("fake file system cannot remove non-existent file: {}", path),
                )
            })
        }
    }

    pub struct PackageResolverProxyTempDirBuilder {
        temp_dir: tempfile::TempDir,
        expected_package_url: String,
        write_packages_json: bool,
        packages: Vec<String>,
        images: HashMap<String, Vec<u8>>,
    }

    impl PackageResolverProxyTempDirBuilder {
        const VBMETA_NAME: &'static str = "fuchsia.vbmeta";
        const ZBI_NAME: &'static str = "zbi";
        const ZBI_SIGNED_NAME: &'static str = "zbi.signed";
        fn new() -> PackageResolverProxyTempDirBuilder {
            Self {
                temp_dir: tempfile::tempdir().expect("create temp dir"),
                expected_package_url: TEST_UPDATE_PACKAGE_URL.to_owned(),
                write_packages_json: false,
                packages: Vec::new(),
                images: HashMap::new(),
            }
        }

        fn with_packages_json(mut self) -> Self {
            self.write_packages_json = true;
            self
        }

        fn with_system_image_merkle(mut self, merkle: &str) -> Self {
            assert!(self.write_packages_json);
            self.packages.push(format!("fuchsia-pkg://fuchsia.com/system_image/0?hash={}", merkle));
            self
        }

        fn with_image(mut self, name: &str, value: impl AsRef<[u8]>) -> Self {
            self.images.insert(name.to_owned(), value.as_ref().to_vec());
            self
        }

        fn with_update_url(mut self, url: &str) -> Self {
            self.expected_package_url = url.to_owned();
            self
        }

        fn build(self) -> PackageResolverProxyTempDir {
            fs::write(self.temp_dir.path().join("meta"), UPDATE_PACKAGE_MERKLE.to_string())
                .expect("write meta");
            if self.write_packages_json {
                let packages_json = serde_json::json!({
                    "version": "1",
                    "content": self.packages
                })
                .to_string();
                eprintln!("{}", packages_json);
                fs::write(self.temp_dir.path().join("packages.json"), packages_json)
                    .expect("write packages.json");
            }

            for (name, image) in self.images.into_iter() {
                fs::write(self.temp_dir.path().join(name.clone()), image)
                    .unwrap_or_else(|_| panic!("write {}", name));
            }
            PackageResolverProxyTempDir {
                temp_dir: self.temp_dir,
                expected_package_url: self.expected_package_url,
            }
        }
    }

    pub struct PackageResolverProxyTempDir {
        temp_dir: tempfile::TempDir,
        expected_package_url: String,
    }
    impl PackageResolverProxyTempDir {
        fn new_with_default_meta() -> PackageResolverProxyTempDir {
            PackageResolverProxyTempDirBuilder::new().build()
        }

        fn new_with_empty_packages_json() -> PackageResolverProxyTempDir {
            PackageResolverProxyTempDirBuilder::new().with_packages_json().build()
        }

        fn new_with_latest_system_image(merkle: &str) -> PackageResolverProxyTempDir {
            PackageResolverProxyTempDirBuilder::new()
                .with_packages_json()
                .with_system_image_merkle(merkle)
                .build()
        }

        fn new_with_vbmeta(vbmeta: impl AsRef<[u8]>) -> PackageResolverProxyTempDir {
            PackageResolverProxyTempDirBuilder::new()
                .with_packages_json()
                .with_system_image_merkle(ACTIVE_SYSTEM_IMAGE_MERKLE)
                .with_image(PackageResolverProxyTempDirBuilder::VBMETA_NAME, vbmeta)
                .build()
        }

        fn new_with_zbi(zbi: impl AsRef<[u8]>) -> PackageResolverProxyTempDir {
            PackageResolverProxyTempDirBuilder::new()
                .with_packages_json()
                .with_system_image_merkle(ACTIVE_SYSTEM_IMAGE_MERKLE)
                .with_image(PackageResolverProxyTempDirBuilder::ZBI_NAME, zbi)
                .build()
        }

        fn new_with_zbi_signed(zbi: impl AsRef<[u8]>) -> PackageResolverProxyTempDir {
            PackageResolverProxyTempDirBuilder::new()
                .with_packages_json()
                .with_system_image_merkle(ACTIVE_SYSTEM_IMAGE_MERKLE)
                .with_image(PackageResolverProxyTempDirBuilder::ZBI_SIGNED_NAME, zbi)
                .build()
        }
    }
    impl PackageResolverProxyInterface for PackageResolverProxyTempDir {
        type ResolveResponseFut = future::Ready<Result<PackageResolverResolveResult, fidl::Error>>;
        fn resolve(
            &self,
            package_url: &str,
            dir: fidl::endpoints::ServerEnd<fio::DirectoryMarker>,
        ) -> Self::ResolveResponseFut {
            assert_eq!(package_url, self.expected_package_url);
            fdio::service_connect(
                self.temp_dir.path().to_str().expect("path is utf8"),
                dir.into_channel(),
            )
            .unwrap();
            future::ok(Ok(fpkg::ResolutionContext { bytes: vec![] }))
        }

        type ResolveWithContextResponseFut =
            future::Ready<Result<PackageResolverResolveWithContextResult, fidl::Error>>;
        fn resolve_with_context(
            &self,
            _package_url: &str,
            _context: &mut fpkg::ResolutionContext,
            _dir: fidl::endpoints::ServerEnd<fio::DirectoryMarker>,
        ) -> Self::ResolveWithContextResponseFut {
            panic!("resolve_with_context not implemented");
        }

        type GetHashResponseFut = future::Ready<Result<PackageResolverGetHashResult, fidl::Error>>;
        fn get_hash(&self, _package_url: &mut PackageUrl) -> Self::GetHashResponseFut {
            panic!("get_hash not implemented");
        }
    }

    struct MockSpaceManagerService {
        call_count: Mutex<u32>,
    }

    impl MockSpaceManagerService {
        fn new() -> Self {
            Self { call_count: Mutex::new(0) }
        }

        /// Spawns a new task to serve the space manager protocol.
        pub fn spawn_gc_service(self: &Arc<Self>) -> fidl_space::ManagerProxy {
            let (proxy, server_end) =
                fidl::endpoints::create_proxy_and_stream::<fidl_space::ManagerMarker>().unwrap();

            fasync::Task::spawn(Arc::clone(self).run_gc_service(server_end).unwrap_or_else(|e| {
                panic!("error running space manager service: {:#}", anyhow!(e))
            }))
            .detach();

            proxy
        }

        async fn run_gc_service(
            self: Arc<Self>,
            mut stream: fidl_space::ManagerRequestStream,
        ) -> Result<(), anyhow::Error> {
            while let Some(req) = stream.try_next().await? {
                match req {
                    fidl_space::ManagerRequest::Gc { responder } => {
                        *self.call_count.lock() += 1;
                        responder.send(&mut Ok(())).unwrap()
                    }
                }
            }
            Ok(())
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_missing_system_meta_file() {
        let mut file_system = FakeFileSystem { contents: hashmap![] };
        let package_resolver = PackageResolverProxyTempDir::new_with_default_meta();
        let mock_paver = Arc::new(MockPaverServiceBuilder::new().build());
        let paver = mock_paver.spawn_paver_service();

        let mock_space_manager = Arc::new(MockSpaceManagerService::new());
        let space_manager = mock_space_manager.spawn_gc_service();

        let result = check_for_system_update_impl(
            TEST_UPDATE_PACKAGE_URL,
            &mut file_system,
            &package_resolver,
            &paver,
            &FakeTargetChannelUpdater::new(),
            None,
            &space_manager,
        )
        .await;

        assert_matches!(result, Err(Error::ReadSystemMeta(_)));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_malformatted_system_meta_file() {
        let mut file_system = FakeFileSystem {
            contents: hashmap![
                "/pkgfs/system/meta".to_string() => "not-a-merkle".to_string()
            ],
        };
        let package_resolver = PackageResolverProxyTempDir::new_with_default_meta();
        let mock_paver = Arc::new(MockPaverServiceBuilder::new().build());
        let paver = mock_paver.spawn_paver_service();

        let mock_space_manager = Arc::new(MockSpaceManagerService::new());
        let space_manager = mock_space_manager.spawn_gc_service();

        let result = check_for_system_update_impl(
            TEST_UPDATE_PACKAGE_URL,
            &mut file_system,
            &package_resolver,
            &paver,
            &FakeTargetChannelUpdater::new(),
            None,
            &space_manager,
        )
        .await;

        assert_matches!(result, Err(Error::ParseSystemMeta(_)));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_resolve_update_package_fidl_error() {
        struct PackageResolverProxyFidlError;
        impl PackageResolverProxyInterface for PackageResolverProxyFidlError {
            type ResolveResponseFut =
                future::Ready<Result<PackageResolverResolveResult, fidl::Error>>;
            fn resolve(
                &self,
                _package_url: &str,
                _dir: fidl::endpoints::ServerEnd<fio::DirectoryMarker>,
            ) -> Self::ResolveResponseFut {
                future::err(fidl::Error::Invalid)
            }

            type ResolveWithContextResponseFut =
                future::Ready<Result<PackageResolverResolveWithContextResult, fidl::Error>>;
            fn resolve_with_context(
                &self,
                _package_url: &str,
                _context: &mut fpkg::ResolutionContext,
                _dir: fidl::endpoints::ServerEnd<fio::DirectoryMarker>,
            ) -> Self::ResolveWithContextResponseFut {
                future::err(fidl::Error::Invalid)
            }

            type GetHashResponseFut =
                future::Ready<Result<PackageResolverGetHashResult, fidl::Error>>;
            fn get_hash(&self, _package_url: &mut PackageUrl) -> Self::GetHashResponseFut {
                panic!("get_hash not implemented");
            }
        }

        let mut file_system = FakeFileSystem::new_with_valid_system_meta();
        let package_resolver = PackageResolverProxyFidlError;
        let mock_paver = Arc::new(MockPaverServiceBuilder::new().build());
        let paver = mock_paver.spawn_paver_service();
        let mock_space_manager = Arc::new(MockSpaceManagerService::new());
        let space_manager = mock_space_manager.spawn_gc_service();

        let result = check_for_system_update_impl(
            TEST_UPDATE_PACKAGE_URL,
            &mut file_system,
            &package_resolver,
            &paver,
            &FakeTargetChannelUpdater::new(),
            None,
            &space_manager,
        )
        .await;

        assert_matches!(result, Err(Error::UpdatePackage(errors::UpdatePackage::ResolveFidl(_))));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_update_package_garbage_collection_called_no_space() {
        struct PackageResolverProxyNoSpaceOnce {
            temp_dir: tempfile::TempDir,
            call_count: Mutex<u32>,
        }

        impl PackageResolverProxyNoSpaceOnce {
            fn new() -> PackageResolverProxyNoSpaceOnce {
                let package_resolver = PackageResolverProxyTempDirBuilder::new()
                    .with_packages_json()
                    .with_system_image_merkle(ACTIVE_SYSTEM_IMAGE_MERKLE)
                    .build();
                PackageResolverProxyNoSpaceOnce {
                    temp_dir: package_resolver.temp_dir,
                    call_count: Mutex::new(0),
                }
            }
        }

        impl PackageResolverProxyInterface for PackageResolverProxyNoSpaceOnce {
            type ResolveResponseFut =
                future::Ready<Result<PackageResolverResolveResult, fidl::Error>>;
            fn resolve(
                &self,
                package_url: &str,
                dir: fidl::endpoints::ServerEnd<fio::DirectoryMarker>,
            ) -> Self::ResolveResponseFut {
                *self.call_count.lock() += 1;

                if *self.call_count.lock() == 1 {
                    return future::ok(Err(fidl_fuchsia_pkg::ResolveError::NoSpace));
                }

                assert_eq!(package_url, TEST_UPDATE_PACKAGE_URL);
                fdio::service_connect(
                    self.temp_dir.path().to_str().expect("path is utf8"),
                    dir.into_channel(),
                )
                .unwrap();
                future::ok(Ok(fpkg::ResolutionContext { bytes: vec![] }))
            }

            type ResolveWithContextResponseFut =
                future::Ready<Result<PackageResolverResolveWithContextResult, fidl::Error>>;
            fn resolve_with_context(
                &self,
                _package_url: &str,
                _context: &mut fpkg::ResolutionContext,
                _dir: fidl::endpoints::ServerEnd<fio::DirectoryMarker>,
            ) -> Self::ResolveWithContextResponseFut {
                panic!("resolve_with_context not implemented");
            }

            type GetHashResponseFut =
                future::Ready<Result<PackageResolverGetHashResult, fidl::Error>>;
            fn get_hash(&self, _package_url: &mut PackageUrl) -> Self::GetHashResponseFut {
                panic!("get_hash not implemented");
            }
        }

        let mut file_system = FakeFileSystem::new_with_valid_system_meta();
        let package_resolver = PackageResolverProxyNoSpaceOnce::new();
        let mock_paver = Arc::new(MockPaverServiceBuilder::new().build());
        let paver = mock_paver.spawn_paver_service();
        let mock_space_manager = Arc::new(MockSpaceManagerService::new());
        let space_manager = mock_space_manager.spawn_gc_service();

        let result = check_for_system_update_impl(
            TEST_UPDATE_PACKAGE_URL,
            &mut file_system,
            &package_resolver,
            &paver,
            &FakeTargetChannelUpdater::new(),
            Some(&UPDATE_PACKAGE_MERKLE),
            &space_manager,
        )
        .await;

        assert_matches!(result, Ok(SystemUpdateStatus::UpToDate { system_image, update_package: _ })
        if system_image == ACTIVE_SYSTEM_IMAGE_MERKLE
        .parse()
        .expect("active system image string literal"));

        assert_matches!(*mock_space_manager.call_count.lock(), 1);
        assert_matches!(*package_resolver.call_count.lock(), 2);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_update_package_garbage_collection_succeeds() {
        struct PackageResolverProxyNoSpaceError;
        impl PackageResolverProxyInterface for PackageResolverProxyNoSpaceError {
            type ResolveResponseFut =
                future::Ready<Result<PackageResolverResolveResult, fidl::Error>>;
            fn resolve(
                &self,
                _package_url: &str,
                _dir: fidl::endpoints::ServerEnd<fio::DirectoryMarker>,
            ) -> Self::ResolveResponseFut {
                future::ok(Err(fidl_fuchsia_pkg::ResolveError::NoSpace))
            }

            type ResolveWithContextResponseFut =
                future::Ready<Result<PackageResolverResolveWithContextResult, fidl::Error>>;
            fn resolve_with_context(
                &self,
                _package_url: &str,
                _context: &mut fpkg::ResolutionContext,
                _dir: fidl::endpoints::ServerEnd<fio::DirectoryMarker>,
            ) -> Self::ResolveWithContextResponseFut {
                panic!("resolve_with_context not implemented");
            }

            type GetHashResponseFut =
                future::Ready<Result<PackageResolverGetHashResult, fidl::Error>>;
            fn get_hash(&self, _package_url: &mut PackageUrl) -> Self::GetHashResponseFut {
                panic!("get_hash not implemented");
            }
        }

        let mut file_system = FakeFileSystem::new_with_valid_system_meta();
        let package_resolver = PackageResolverProxyNoSpaceError;
        let mock_paver = Arc::new(MockPaverServiceBuilder::new().build());
        let paver = mock_paver.spawn_paver_service();
        let mock_space_manager = Arc::new(MockSpaceManagerService::new());
        let space_manager = mock_space_manager.spawn_gc_service();

        let result = check_for_system_update_impl(
            TEST_UPDATE_PACKAGE_URL,
            &mut file_system,
            &package_resolver,
            &paver,
            &FakeTargetChannelUpdater::new(),
            None,
            &space_manager,
        )
        .await;

        assert_matches!(
            result,
            Err(Error::UpdatePackage(errors::UpdatePackage::Resolve(
                fidl_fuchsia_pkg_ext::ResolveError::NoSpace
            )))
        );
        assert_matches!(*mock_space_manager.call_count.lock(), 1);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_resolve_update_package_zx_error() {
        struct PackageResolverProxyZxError;
        impl PackageResolverProxyInterface for PackageResolverProxyZxError {
            type ResolveResponseFut =
                future::Ready<Result<PackageResolverResolveResult, fidl::Error>>;
            fn resolve(
                &self,
                _package_url: &str,
                _dir: fidl::endpoints::ServerEnd<fio::DirectoryMarker>,
            ) -> Self::ResolveResponseFut {
                future::ok(Err(fidl_fuchsia_pkg::ResolveError::Internal))
            }

            type ResolveWithContextResponseFut =
                future::Ready<Result<PackageResolverResolveWithContextResult, fidl::Error>>;
            fn resolve_with_context(
                &self,
                _package_url: &str,
                _context: &mut fpkg::ResolutionContext,
                _dir: fidl::endpoints::ServerEnd<fio::DirectoryMarker>,
            ) -> Self::ResolveWithContextResponseFut {
                panic!("resolve_with_context not implemented");
            }

            type GetHashResponseFut =
                future::Ready<Result<PackageResolverGetHashResult, fidl::Error>>;
            fn get_hash(&self, _package_url: &mut PackageUrl) -> Self::GetHashResponseFut {
                panic!("get_hash not implemented");
            }
        }

        let mut file_system = FakeFileSystem::new_with_valid_system_meta();
        let package_resolver = PackageResolverProxyZxError;
        let mock_paver = Arc::new(MockPaverServiceBuilder::new().build());
        let paver = mock_paver.spawn_paver_service();
        let mock_space_manager = Arc::new(MockSpaceManagerService::new());
        let space_manager = mock_space_manager.spawn_gc_service();

        let result = check_for_system_update_impl(
            TEST_UPDATE_PACKAGE_URL,
            &mut file_system,
            &package_resolver,
            &paver,
            &FakeTargetChannelUpdater::new(),
            None,
            &space_manager,
        )
        .await;

        assert_matches!(result, Err(Error::UpdatePackage(errors::UpdatePackage::Resolve(_))));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_resolve_update_package_directory_closed() {
        struct PackageResolverProxyDirectoryCloser;
        impl PackageResolverProxyInterface for PackageResolverProxyDirectoryCloser {
            type ResolveResponseFut =
                future::Ready<Result<PackageResolverResolveResult, fidl::Error>>;
            fn resolve(
                &self,
                _package_url: &str,
                _dir: fidl::endpoints::ServerEnd<fio::DirectoryMarker>,
            ) -> Self::ResolveResponseFut {
                future::ok(Ok(fpkg::ResolutionContext { bytes: vec![] }))
            }

            type ResolveWithContextResponseFut =
                future::Ready<Result<PackageResolverResolveWithContextResult, fidl::Error>>;
            fn resolve_with_context(
                &self,
                _package_url: &str,
                _context: &mut fpkg::ResolutionContext,
                _dir: fidl::endpoints::ServerEnd<fio::DirectoryMarker>,
            ) -> Self::ResolveWithContextResponseFut {
                panic!("resolve_with_context not implemented");
            }

            type GetHashResponseFut =
                future::Ready<Result<PackageResolverGetHashResult, fidl::Error>>;
            fn get_hash(&self, _package_url: &mut PackageUrl) -> Self::GetHashResponseFut {
                panic!("get_hash not implemented");
            }
        }

        let mut file_system = FakeFileSystem::new_with_valid_system_meta();
        let package_resolver = PackageResolverProxyDirectoryCloser;
        let mock_paver = Arc::new(MockPaverServiceBuilder::new().build());
        let paver = mock_paver.spawn_paver_service();
        let mock_space_manager = Arc::new(MockSpaceManagerService::new());
        let space_manager = mock_space_manager.spawn_gc_service();

        let result = check_for_system_update_impl(
            TEST_UPDATE_PACKAGE_URL,
            &mut file_system,
            &package_resolver,
            &paver,
            &FakeTargetChannelUpdater::new(),
            None,
            &space_manager,
        )
        .await;

        assert_matches!(result, Err(Error::UpdatePackage(errors::UpdatePackage::Hash(_))));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_update_package_missing_packages_json() {
        let mut file_system = FakeFileSystem::new_with_valid_system_meta();
        let package_resolver = PackageResolverProxyTempDir::new_with_default_meta();
        let mock_paver = Arc::new(MockPaverServiceBuilder::new().build());
        let paver = mock_paver.spawn_paver_service();
        let mock_space_manager = Arc::new(MockSpaceManagerService::new());
        let space_manager = mock_space_manager.spawn_gc_service();

        let result = check_for_system_update_impl(
            TEST_UPDATE_PACKAGE_URL,
            &mut file_system,
            &package_resolver,
            &paver,
            &FakeTargetChannelUpdater::new(),
            None,
            &space_manager,
        )
        .await;

        assert_matches!(
            result,
            Err(Error::UpdatePackage(errors::UpdatePackage::ExtractPackagesManifest(_)))
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_update_package_empty_packages_json() {
        let mut file_system = FakeFileSystem::new_with_valid_system_meta();
        let package_resolver = PackageResolverProxyTempDir::new_with_empty_packages_json();
        let mock_paver = Arc::new(MockPaverServiceBuilder::new().build());
        let paver = mock_paver.spawn_paver_service();
        let mock_space_manager = Arc::new(MockSpaceManagerService::new());
        let space_manager = mock_space_manager.spawn_gc_service();

        let result = check_for_system_update_impl(
            TEST_UPDATE_PACKAGE_URL,
            &mut file_system,
            &package_resolver,
            &paver,
            &FakeTargetChannelUpdater::new(),
            None,
            &space_manager,
        )
        .await;

        assert_matches!(
            result,
            Err(Error::UpdatePackage(errors::UpdatePackage::MissingSystemImage))
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_update_package_bad_system_image() {
        let mut file_system = FakeFileSystem::new_with_valid_system_meta();
        let package_resolver =
            PackageResolverProxyTempDir::new_with_latest_system_image("bad-merkle");
        let mock_paver = Arc::new(MockPaverServiceBuilder::new().build());
        let paver = mock_paver.spawn_paver_service();
        let mock_space_manager = Arc::new(MockSpaceManagerService::new());
        let space_manager = mock_space_manager.spawn_gc_service();

        let result = check_for_system_update_impl(
            TEST_UPDATE_PACKAGE_URL,
            &mut file_system,
            &package_resolver,
            &paver,
            &FakeTargetChannelUpdater::new(),
            None,
            &space_manager,
        )
        .await;
        assert_matches!(
            result,
            Err(Error::UpdatePackage(errors::UpdatePackage::ExtractPackagesManifest(_)))
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_up_to_date() {
        let mut file_system = FakeFileSystem::new_with_valid_system_meta();
        let package_resolver = PackageResolverProxyTempDir::new_with_vbmeta([1]);
        let mock_paver = Arc::new(
            MockPaverServiceBuilder::new()
                .active_config(Configuration::A)
                .insert_hook(mock_paver::hooks::read_asset(|configuration, asset| {
                    assert_eq!(configuration, Configuration::A);
                    match asset {
                        Asset::Kernel => panic!("shouldn't read kernel if vbmeta is the same!"),
                        Asset::VerifiedBootMetadata => Ok(vec![1]),
                    }
                }))
                .build(),
        );
        let paver = mock_paver.spawn_paver_service();
        let mock_space_manager = Arc::new(MockSpaceManagerService::new());
        let space_manager = mock_space_manager.spawn_gc_service();

        let result = check_for_system_update_impl(
            TEST_UPDATE_PACKAGE_URL,
            &mut file_system,
            &package_resolver,
            &paver,
            &FakeTargetChannelUpdater::new(),
            Some(&UPDATE_PACKAGE_MERKLE),
            &space_manager,
        )
        .await;

        assert_matches!(
            result,
            Ok(SystemUpdateStatus::UpToDate { system_image, update_package: _ })
                if system_image == ACTIVE_SYSTEM_IMAGE_MERKLE
                .parse()
                .expect("active system image string literal")
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_up_to_date_with_missing_vbmeta() {
        let mut file_system = FakeFileSystem::new_with_valid_system_meta();
        let package_resolver = PackageResolverProxyTempDir::new_with_vbmeta([1]);
        let mock_paver = Arc::new(
            MockPaverServiceBuilder::new()
                .active_config(Configuration::A)
                .insert_hook(mock_paver::hooks::read_asset(|configuration, asset| {
                    assert_eq!(configuration, Configuration::A);
                    match asset {
                        Asset::Kernel => Err(zx::Status::NOT_FOUND),
                        Asset::VerifiedBootMetadata => Err(zx::Status::NOT_FOUND),
                    }
                }))
                .build(),
        );
        let paver = mock_paver.spawn_paver_service();
        let mock_space_manager = Arc::new(MockSpaceManagerService::new());
        let space_manager = mock_space_manager.spawn_gc_service();

        let result = check_for_system_update_impl(
            TEST_UPDATE_PACKAGE_URL,
            &mut file_system,
            &package_resolver,
            &paver,
            &FakeTargetChannelUpdater::new(),
            Some(&UPDATE_PACKAGE_MERKLE),
            &space_manager,
        )
        .await;

        assert_matches!(
            result,
            Ok(SystemUpdateStatus::UpToDate { system_image, update_package: _ })
                if system_image == ACTIVE_SYSTEM_IMAGE_MERKLE
                .parse()
                .expect("active system image string literal")
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_update_available() {
        let mut file_system = FakeFileSystem::new_with_valid_system_meta();
        let package_resolver =
            PackageResolverProxyTempDir::new_with_latest_system_image(NEW_SYSTEM_IMAGE_MERKLE);
        let mock_paver = Arc::new(MockPaverServiceBuilder::new().build());
        let paver = mock_paver.spawn_paver_service();
        let mock_space_manager = Arc::new(MockSpaceManagerService::new());
        let space_manager = mock_space_manager.spawn_gc_service();

        let result = check_for_system_update_impl(
            TEST_UPDATE_PACKAGE_URL,
            &mut file_system,
            &package_resolver,
            &paver,
            &FakeTargetChannelUpdater::new(),
            None,
            &space_manager,
        )
        .await;

        assert_matches!(
            result,
            Ok(SystemUpdateStatus::UpdateAvailable { current_system_image, latest_system_image })
            if
                current_system_image == ACTIVE_SYSTEM_IMAGE_MERKLE
                    .parse()
                    .expect("active system image string literal") &&
                latest_system_image == NEW_SYSTEM_IMAGE_MERKLE
                    .parse()
                    .expect("new system image string literal")
        );
        assert_matches!(*mock_space_manager.call_count.lock(), 0);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_update_available_changing_target_channel() {
        let update_url: &str = "fuchsia-pkg://target.channel.test/my-update-package";
        let mut file_system = FakeFileSystem::new_with_valid_system_meta();
        let package_resolver = PackageResolverProxyTempDirBuilder::new()
            .with_packages_json()
            .with_system_image_merkle(NEW_SYSTEM_IMAGE_MERKLE)
            .with_update_url(update_url)
            .build();
        let mock_paver = Arc::new(MockPaverServiceBuilder::new().build());
        let paver = mock_paver.spawn_paver_service();
        let mock_space_manager = Arc::new(MockSpaceManagerService::new());
        let space_manager = mock_space_manager.spawn_gc_service();

        let result = check_for_system_update_impl(
            TEST_UPDATE_PACKAGE_URL,
            &mut file_system,
            &package_resolver,
            &paver,
            &FakeTargetChannelUpdater::new_with_update_url(update_url),
            None,
            &space_manager,
        )
        .await;

        assert_matches!(
            result,
            Ok(SystemUpdateStatus::UpdateAvailable { current_system_image, latest_system_image })
            if
                current_system_image == ACTIVE_SYSTEM_IMAGE_MERKLE
                    .parse()
                    .expect("active system image string literal") &&
                latest_system_image == NEW_SYSTEM_IMAGE_MERKLE
                    .parse()
                    .expect("new system image string literal")
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_different_update_package_hash_does_not_trigger_update() {
        let mut file_system = FakeFileSystem::new_with_valid_system_meta();
        let package_resolver =
            PackageResolverProxyTempDir::new_with_latest_system_image(ACTIVE_SYSTEM_IMAGE_MERKLE);
        let mock_paver = Arc::new(MockPaverServiceBuilder::new().build());
        let paver = mock_paver.spawn_paver_service();
        let mock_space_manager = Arc::new(MockSpaceManagerService::new());
        let space_manager = mock_space_manager.spawn_gc_service();

        let previous_update_package = Hash::from([0x44; 32]);
        let result = check_for_system_update_impl(
            TEST_UPDATE_PACKAGE_URL,
            &mut file_system,
            &package_resolver,
            &paver,
            &FakeTargetChannelUpdater::new(),
            Some(&previous_update_package),
            &space_manager,
        )
        .await;

        assert_matches!(
            result,
            Ok(SystemUpdateStatus::UpToDate { system_image, update_package})
            if system_image == ACTIVE_SYSTEM_IMAGE_MERKLE
                .parse()
                .expect("active system image string literal") &&
            update_package == *UPDATE_PACKAGE_MERKLE
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_vbmeta_only_update_available() {
        let mut file_system = FakeFileSystem::new_with_valid_system_meta();
        let package_resolver = PackageResolverProxyTempDir::new_with_vbmeta([1]);
        let mock_paver = Arc::new(
            MockPaverServiceBuilder::new()
                .active_config(Configuration::A)
                .insert_hook(mock_paver::hooks::read_asset(|configuration, asset| {
                    assert_eq!(configuration, Configuration::A);
                    match asset {
                        Asset::Kernel => panic!("not expecting to read kernel"),
                        Asset::VerifiedBootMetadata => Ok(vec![0]),
                    }
                }))
                .build(),
        );
        let paver = mock_paver.spawn_paver_service();
        let mock_space_manager = Arc::new(MockSpaceManagerService::new());
        let space_manager = mock_space_manager.spawn_gc_service();

        let result = check_for_system_update_impl(
            TEST_UPDATE_PACKAGE_URL,
            &mut file_system,
            &package_resolver,
            &paver,
            &FakeTargetChannelUpdater::new(),
            None,
            &space_manager,
        )
        .await;

        assert_matches!(
            result,
            Ok(SystemUpdateStatus::UpdateAvailable { current_system_image, latest_system_image })
            if
                current_system_image == ACTIVE_SYSTEM_IMAGE_MERKLE
                    .parse()
                    .expect("active system image string literal") &&
                latest_system_image == ACTIVE_SYSTEM_IMAGE_MERKLE
                    .parse()
                    .expect("new system image string literal")
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_zbi_only_update_available() {
        let mut file_system = FakeFileSystem::new_with_valid_system_meta();
        let package_resolver = PackageResolverProxyTempDir::new_with_zbi([1]);
        let mock_paver = Arc::new(
            MockPaverServiceBuilder::new()
                .active_config(Configuration::A)
                .insert_hook(mock_paver::hooks::read_asset(|configuration, asset| {
                    assert_eq!(configuration, Configuration::A);
                    match asset {
                        Asset::Kernel => Ok(vec![0]),
                        Asset::VerifiedBootMetadata => panic!("no vbmeta available"),
                    }
                }))
                .build(),
        );
        let paver = mock_paver.spawn_paver_service();
        let mock_space_manager = Arc::new(MockSpaceManagerService::new());
        let space_manager = mock_space_manager.spawn_gc_service();

        let result = check_for_system_update_impl(
            TEST_UPDATE_PACKAGE_URL,
            &mut file_system,
            &package_resolver,
            &paver,
            &FakeTargetChannelUpdater::new(),
            None,
            &space_manager,
        )
        .await;

        assert_matches!(
            result,
            Ok(SystemUpdateStatus::UpdateAvailable { current_system_image, latest_system_image })
            if
                current_system_image == ACTIVE_SYSTEM_IMAGE_MERKLE
                    .parse()
                    .expect("active system image string literal") &&
                latest_system_image == ACTIVE_SYSTEM_IMAGE_MERKLE
                    .parse()
                    .expect("new system image string literal")
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_zbi_did_not_require_update() {
        let mut file_system = FakeFileSystem::new_with_valid_system_meta();
        let package_resolver = PackageResolverProxyTempDir::new_with_zbi([0]);
        let mock_paver = Arc::new(
            MockPaverServiceBuilder::new()
                .active_config(Configuration::A)
                .insert_hook(mock_paver::hooks::read_asset(|configuration, asset| {
                    assert_eq!(configuration, Configuration::A);
                    match asset {
                        Asset::Kernel => Ok(vec![0]),
                        Asset::VerifiedBootMetadata => panic!("no vbmeta available"),
                    }
                }))
                .build(),
        );
        let paver = mock_paver.spawn_paver_service();
        let mock_space_manager = Arc::new(MockSpaceManagerService::new());
        let space_manager = mock_space_manager.spawn_gc_service();

        let result = check_for_system_update_impl(
            TEST_UPDATE_PACKAGE_URL,
            &mut file_system,
            &package_resolver,
            &paver,
            &FakeTargetChannelUpdater::new(),
            None,
            &space_manager,
        )
        .await;

        assert_matches!(
            result,
            Ok(SystemUpdateStatus::UpToDate { system_image, update_package: _ })
                if system_image == ACTIVE_SYSTEM_IMAGE_MERKLE
                .parse()
                .expect("active system image string literal")
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_zbi_only_no_abr_update_available() {
        let mut file_system = FakeFileSystem::new_with_valid_system_meta();
        let package_resolver = PackageResolverProxyTempDir::new_with_zbi([1]);
        let mock_paver = Arc::new(
            MockPaverServiceBuilder::new()
                .boot_manager_close_with_epitaph(zx::Status::NOT_SUPPORTED)
                .insert_hook(mock_paver::hooks::read_asset(|configuration, asset| {
                    assert_eq!(configuration, Configuration::A);
                    match asset {
                        Asset::Kernel => Ok(vec![0]),
                        Asset::VerifiedBootMetadata => panic!("no vbmeta available"),
                    }
                }))
                .build(),
        );
        let paver = mock_paver.spawn_paver_service();
        let mock_space_manager = Arc::new(MockSpaceManagerService::new());
        let space_manager = mock_space_manager.spawn_gc_service();

        let result = check_for_system_update_impl(
            TEST_UPDATE_PACKAGE_URL,
            &mut file_system,
            &package_resolver,
            &paver,
            &FakeTargetChannelUpdater::new(),
            None,
            &space_manager,
        )
        .await;

        assert_matches!(
            result,
            Ok(SystemUpdateStatus::UpdateAvailable { current_system_image, latest_system_image })
            if
                current_system_image == ACTIVE_SYSTEM_IMAGE_MERKLE
                    .parse()
                    .expect("active system image string literal") &&
                latest_system_image == ACTIVE_SYSTEM_IMAGE_MERKLE
                    .parse()
                    .expect("new system image string literal")
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_zbi_no_abr_did_not_require_update() {
        let mut file_system = FakeFileSystem::new_with_valid_system_meta();
        let package_resolver = PackageResolverProxyTempDir::new_with_zbi([0]);
        let mock_paver = Arc::new(
            MockPaverServiceBuilder::new()
                .boot_manager_close_with_epitaph(zx::Status::NOT_SUPPORTED)
                .insert_hook(mock_paver::hooks::read_asset(|configuration, asset| {
                    assert_eq!(configuration, Configuration::A);
                    match asset {
                        Asset::Kernel => Ok(vec![0]),
                        Asset::VerifiedBootMetadata => panic!("no vbmeta available"),
                    }
                }))
                .build(),
        );
        let paver = mock_paver.spawn_paver_service();
        let mock_space_manager = Arc::new(MockSpaceManagerService::new());
        let space_manager = mock_space_manager.spawn_gc_service();

        let result = check_for_system_update_impl(
            TEST_UPDATE_PACKAGE_URL,
            &mut file_system,
            &package_resolver,
            &paver,
            &FakeTargetChannelUpdater::new(),
            None,
            &space_manager,
        )
        .await;

        assert_matches!(
            result,
            Ok(SystemUpdateStatus::UpToDate { system_image, update_package: _ })
                if system_image == ACTIVE_SYSTEM_IMAGE_MERKLE
                .parse()
                .expect("active system image string literal")
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_zbi_signed_only_update_available() {
        let mut file_system = FakeFileSystem::new_with_valid_system_meta();
        let package_resolver = PackageResolverProxyTempDir::new_with_zbi_signed([1]);
        let mock_paver = Arc::new(
            MockPaverServiceBuilder::new()
                .active_config(Configuration::A)
                .insert_hook(mock_paver::hooks::read_asset(|configuration, asset| {
                    assert_eq!(configuration, Configuration::A);
                    match asset {
                        Asset::Kernel => Ok(vec![0]),
                        Asset::VerifiedBootMetadata => panic!("no vbmeta available"),
                    }
                }))
                .build(),
        );
        let paver = mock_paver.spawn_paver_service();
        let mock_space_manager = Arc::new(MockSpaceManagerService::new());
        let space_manager = mock_space_manager.spawn_gc_service();

        let result = check_for_system_update_impl(
            TEST_UPDATE_PACKAGE_URL,
            &mut file_system,
            &package_resolver,
            &paver,
            &FakeTargetChannelUpdater::new(),
            None,
            &space_manager,
        )
        .await;

        assert_matches!(
            result,
            Ok(SystemUpdateStatus::UpdateAvailable { current_system_image, latest_system_image })
            if
                current_system_image == ACTIVE_SYSTEM_IMAGE_MERKLE
                    .parse()
                    .expect("active system image string literal") &&
                latest_system_image == ACTIVE_SYSTEM_IMAGE_MERKLE
                    .parse()
                    .expect("new system image string literal")
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_zbi_signed_did_not_require_update() {
        let mut file_system = FakeFileSystem::new_with_valid_system_meta();
        let package_resolver = PackageResolverProxyTempDir::new_with_zbi_signed([0]);
        let mock_paver = Arc::new(
            MockPaverServiceBuilder::new()
                .active_config(Configuration::A)
                .insert_hook(mock_paver::hooks::read_asset(|configuration, asset| {
                    assert_eq!(configuration, Configuration::A);
                    match asset {
                        Asset::Kernel => Ok(vec![0]),
                        Asset::VerifiedBootMetadata => panic!("no vbmeta available"),
                    }
                }))
                .build(),
        );
        let paver = mock_paver.spawn_paver_service();
        let mock_space_manager = Arc::new(MockSpaceManagerService::new());
        let space_manager = mock_space_manager.spawn_gc_service();

        let result = check_for_system_update_impl(
            TEST_UPDATE_PACKAGE_URL,
            &mut file_system,
            &package_resolver,
            &paver,
            &FakeTargetChannelUpdater::new(),
            None,
            &space_manager,
        )
        .await;

        assert_matches!(
            result,
            Ok(SystemUpdateStatus::UpToDate { system_image, update_package: _ })
                if system_image == ACTIVE_SYSTEM_IMAGE_MERKLE
                .parse()
                .expect("active system image string literal")
        );
    }
}

#[cfg(test)]
mod test_compare_buffer {
    use {super::*, assert_matches::assert_matches, fidl_fuchsia_mem::Buffer, fuchsia_zircon::Vmo};

    fn buffer(data: &[u8]) -> Buffer {
        let size = data.len() as u64;
        let vmo = Vmo::create(size).unwrap();
        vmo.write(data, 0).unwrap();
        Buffer { vmo, size }
    }

    #[test]
    fn equal() {
        let a = [1; 100];
        assert_matches!(compare_buffer(buffer(&a), buffer(&a)), Ok(true));
    }

    #[test]
    fn equal_different_size() {
        let a = [1; 100];
        let b = [1; 200];
        assert_matches!(compare_buffer(buffer(&a), buffer(&b)), Ok(true));
    }

    #[test]
    fn equal_large_buffer() {
        let a = [1; 100000];
        let b = [1; 100001];
        assert_matches!(compare_buffer(buffer(&a), buffer(&b)), Ok(true));
    }

    #[test]
    fn not_equal() {
        let a = [1; 100];
        let b = [0; 100];
        assert_matches!(compare_buffer(buffer(&a), buffer(&b)), Ok(false));
    }

    #[test]
    fn not_equal_different_size() {
        let a = [1; 100];
        let b = [0; 200];
        assert_matches!(compare_buffer(buffer(&a), buffer(&b)), Ok(false));
    }

    #[test]
    fn not_equal_large_buffer() {
        let a = [1; 100000];
        let mut b = [1; 100001];
        b[99999] = 0;
        assert_matches!(compare_buffer(buffer(&a), buffer(&b)), Ok(false));
    }

    #[test]
    fn vmo_read_error() {
        let a = Buffer { vmo: Vmo::create(100).unwrap(), size: 10000 };
        let b = Buffer { vmo: Vmo::create(10000).unwrap(), size: 10000 };
        assert_matches!(compare_buffer(a, b), Err(_));
    }
}

#[cfg(test)]
mod test_real_file_system {
    use super::*;
    use assert_matches::assert_matches;
    use proptest::prelude::*;
    use std::fs;
    use std::io::{self, Write};

    #[test]
    fn test_read_to_string_errors_on_missing_file() {
        let dir = tempfile::tempdir().expect("create temp dir");
        let read_res = RealFileSystem.read_to_string(
            dir.path().join("this-file-does-not-exist").to_str().expect("paths are utf8"),
        );
        assert_matches!(read_res.map_err(|e| e.kind()), Err(io::ErrorKind::NotFound));
    }

    proptest! {
        #[test]
        fn test_read_to_string_preserves_contents(
            contents in ".{0, 65}",
            file_name in "[^\\.\0/]{1,10}",
        ) {
            let dir = tempfile::tempdir().expect("create temp dir");
            let file_path = dir.path().join(file_name);
            let mut file = fs::File::create(&file_path).expect("create file");
            file.write_all(contents.as_bytes()).expect("write the contents");

            let read_contents = RealFileSystem
                .read_to_string(file_path.to_str().expect("paths are utf8"))
                .expect("read the file");

            prop_assert_eq!(read_contents, contents);
        }
    }
}
