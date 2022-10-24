// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::{setup::DevhostConfig, storage::Storage},
    anyhow::{bail, format_err, Context, Error},
    fidl::endpoints::ClientEnd,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_buildinfo::ProviderMarker as BuildInfoMarker,
    fidl_fuchsia_io as fio, fidl_fuchsia_paver as fpaver, fuchsia_async as fasync,
    fuchsia_component::{client, server::ServiceFs},
    futures::prelude::*,
    hyper::Uri,
    isolated_ota::{download_and_apply_update, OmahaConfig},
    serde::Deserialize,
    serde_json::{json, Value},
    std::sync::Arc,
    std::{fs::File, io::BufReader, str::FromStr},
    vfs::directory::{entry::DirectoryEntry, helper::DirectlyMutable, mutable::simple::Simple},
};

const PATH_TO_CONFIGS_DIR: &'static str = "/config/data/ota-configs";
const PATH_TO_RECOVERY_CONFIG: &'static str = "/config/data/recovery-config.json";
const DEFAULT_OMAHA_SERVICE_URL: &'static str =
    "https://clients2.google.com/service/update2/fuchsia/json";

enum PaverType {
    /// Use the real paver.
    Real,
    /// Use a fake paver, which can be connected to using the given connector.
    #[allow(dead_code)]
    Fake { connector: ClientEnd<fio::DirectoryMarker> },
}

enum OtaType {
    /// Ota from a devhost.
    Devhost { cfg: DevhostConfig },
    /// Ota from a well-known location. TODO(simonshields): implement this.
    WellKnown,
}

enum BoardName {
    /// Use board name from /config/build-info.
    BuildInfo,
    /// Override board name with given value.
    #[allow(dead_code)]
    Override { name: String },
}

pub enum StorageType {
    /// Use storage that has already had its blobfs partition wiped.
    Ready(Box<dyn Storage>),
    /// Use the given DirectoryMarker for blobfs, and the given path for minfs.
    #[allow(dead_code)]
    Fake { blobfs_root: ClientEnd<fio::DirectoryMarker>, minfs_path: Option<String> },
}

/// Helper for constructing OTAs.
pub struct OtaEnvBuilder {
    board_name: BoardName,
    omaha_config: Option<OmahaConfig>,
    ota_type: OtaType,
    paver: PaverType,
    ssl_certificates: String,
    storage_type: Option<StorageType>,
    factory_reset: bool,
    outgoing_dir: Arc<Simple>,
}

impl OtaEnvBuilder {
    /// Create a new `OtaEnvBuilder`. Requires an `outgoing_dir` which is served
    /// by an instantiation of a Rust VFS tied to this component's outgoing
    /// directory. This is required in order to prepare the outgoing directory
    /// with capabilities like directories and storage for the `pkg-recovery.cm`
    /// component which will be created as a child.
    pub fn new(outgoing_dir: Arc<Simple>) -> Self {
        OtaEnvBuilder {
            board_name: BoardName::BuildInfo,
            omaha_config: None,
            ota_type: OtaType::WellKnown,
            paver: PaverType::Real,
            ssl_certificates: "/config/ssl".to_owned(),
            storage_type: None,
            factory_reset: true,
            outgoing_dir,
        }
    }

    #[cfg(test)]
    /// Override the board name for this OTA.
    pub fn board_name(mut self, name: &str) -> Self {
        self.board_name = BoardName::Override { name: name.to_owned() };
        self
    }

    /// Use the given |DevhostConfig| to run an OTA.
    pub fn devhost(mut self, cfg: DevhostConfig) -> Self {
        self.ota_type = OtaType::Devhost { cfg };
        self
    }

    #[allow(dead_code)]
    /// Use the given |OmahaConfig| to run an OTA.
    pub fn omaha_config(mut self, omaha_config: OmahaConfig) -> Self {
        self.omaha_config = Some(omaha_config);
        self
    }

    /// Use the given StorageType as the storage target.
    pub fn storage_type(mut self, storage_type: StorageType) -> Self {
        self.storage_type = Some(storage_type);
        self
    }

    #[cfg(test)]
    /// Use the given connector to connect to a paver service.
    pub fn fake_paver(mut self, connector: ClientEnd<fio::DirectoryMarker>) -> Self {
        self.paver = PaverType::Fake { connector };
        self
    }

    #[cfg(test)]
    /// Use the given path for SSL certificates.
    pub fn ssl_certificates(mut self, path: &str) -> Self {
        self.ssl_certificates = path.to_owned();
        self
    }

    #[cfg(test)]
    /// Set whether the OTA process should factory reset the data partition.
    pub fn factory_reset(mut self, reset: bool) -> Self {
        self.factory_reset = reset;
        self
    }

    /// Returns the name of the board provided by fidl/fuchsia.buildinfo
    async fn get_board_name(&self) -> Result<String, Error> {
        match &self.board_name {
            BoardName::BuildInfo => {
                let proxy = match client::connect_to_protocol::<BuildInfoMarker>() {
                    Ok(p) => p,
                    Err(err) => {
                        bail!("Failed to connect to fuchsia.buildinfo.Provider proxy: {:?}", err)
                    }
                };
                let build_info =
                    proxy.get_build_info().await.context("Failed to read build info")?;
                build_info.board_config.ok_or(format_err!("No board name provided"))
            }
            BoardName::Override { name } => Ok(name.to_owned()),
        }
    }

    /// Takes a devhost config, and converts into a pkg-resolver friendly format.
    /// Returns SSH authorized keys and a |File| representing a directory with the repository
    /// configuration in it.
    async fn get_devhost_config(
        &self,
        cfg: &DevhostConfig,
    ) -> Result<(Option<String>, File), Error> {
        // Get the repository information from the devhost (including keys and repo URL).
        let client = fuchsia_hyper::new_client();
        let response = client
            .get(Uri::from_str(&cfg.url).context("Bad URL")?)
            .await
            .context("Fetching config from devhost")?;
        let body = response
            .into_body()
            .try_fold(Vec::new(), |mut vec, b| async move {
                vec.extend(b);
                Ok(vec)
            })
            .await
            .context("into body")?;
        let repo_info: Value = serde_json::from_slice(&body).context("Failed to parse JSON")?;

        // Convert into a pkg-resolver friendly format.
        let config_for_resolver = json!({
            "version": "1",
            "content": [
            {
                "repo_url": "fuchsia-pkg://fuchsia.com",
                "root_version": 1,
                "root_threshold": 1,
                "root_keys": repo_info["RootKeys"],
                "mirrors":[{
                    "mirror_url": repo_info["RepoURL"],
                    "subscribe": true
                }],
                "update_package_url": null
            }
            ]
        });

        // Set up a repo configuration folder for the resolver, and write out the config.
        let tempdir = tempfile::tempdir().context("tempdir")?;
        let file = tempdir.path().join("devhost.json");
        let tmp_file = File::create(file).context("Creating file")?;
        serde_json::to_writer(tmp_file, &config_for_resolver).context("Writing JSON")?;

        Ok((
            Some(cfg.authorized_keys.clone()),
            File::open(tempdir.into_path()).context("Opening tmpdir")?,
        ))
    }

    async fn get_wellknown_config(&self) -> Result<(Option<String>, File), Error> {
        println!("recovery-ota: passing in config from config_data");
        Ok((
            None, // No authorized_keys for wellknown builds, can be added to userdebug builds in the future
            File::open(PATH_TO_CONFIGS_DIR).context("Opening config data path")?,
        ))
    }

    /// Construct an |OtaEnv| from this |OtaEnvBuilder|.
    pub async fn build(self) -> Result<OtaEnv, Error> {
        let (authorized_keys, repo_dir) = match &self.ota_type {
            OtaType::Devhost { cfg } => {
                self.get_devhost_config(cfg).await.context("Getting devhost config")?
            }
            OtaType::WellKnown => {
                self.get_wellknown_config().await.context("Preparing wellknown config")?
            }
        };

        let ssl_certificates =
            File::open(&self.ssl_certificates).context("Opening SSL certificate folder")?;

        let board_name = self.get_board_name().await.context("Could not get board name")?;

        let storage_type = self.storage_type.expect("storage_type is required");
        let (storage, blobfs_root, minfs_root_path) = match storage_type {
            StorageType::Ready(storage) => {
                let blobfs_root = storage.wipe_or_get_storage().await.context("Opening blobfs")?;
                (Some(storage), blobfs_root, None)
            }
            StorageType::Fake { blobfs_root, minfs_path } => (None, blobfs_root, minfs_path),
        };

        let paver_connector = match self.paver {
            PaverType::Real => {
                let (paver_connector, remote) = fidl::endpoints::create_endpoints()?;
                let mut paver_fs = ServiceFs::new();
                paver_fs.add_proxy_service::<fpaver::PaverMarker, _>();
                paver_fs.serve_connection(remote).context("Failed to serve on channel")?;
                fasync::Task::spawn(paver_fs.collect()).detach();
                paver_connector
            }
            PaverType::Fake { connector } => connector,
        };

        Ok(OtaEnv {
            authorized_keys,
            blobfs_root,
            board_name,
            minfs_root_path,
            omaha_config: self.omaha_config,
            paver_connector,
            repo_dir,
            ssl_certificates,
            storage,
            factory_reset: self.factory_reset,
            outgoing_dir: self.outgoing_dir,
        })
    }
}

pub struct OtaEnv {
    authorized_keys: Option<String>,
    blobfs_root: ClientEnd<fio::DirectoryMarker>,
    board_name: String,
    minfs_root_path: Option<String>,
    omaha_config: Option<OmahaConfig>,
    paver_connector: ClientEnd<fio::DirectoryMarker>,
    repo_dir: File,
    ssl_certificates: File,
    storage: Option<Box<dyn Storage>>,
    factory_reset: bool,
    outgoing_dir: Arc<Simple>,
}

impl OtaEnv {
    /// Run the OTA, targeting the given channel and reporting the given version
    /// as the current system version.
    pub async fn do_ota(self, channel: &str, version: &str) -> Result<(), Error> {
        fn proxy_from_file(file: File) -> Result<fio::DirectoryProxy, Error> {
            Ok(fio::DirectoryProxy::new(fuchsia_async::Channel::from_channel(
                fdio::transfer_fd(file)?.into(),
            )?))
        }

        // Utilize the repository configs and ssl certificates we were provided,
        // by placing them in our outgoing directory.
        self.outgoing_dir.add_entry(
            "config",
            vfs::pseudo_directory! {
                "data" => vfs::pseudo_directory!{
                        "repositories" => vfs::remote::remote_dir(proxy_from_file(self.repo_dir)?)
                },
                "ssl" => vfs::remote::remote_dir(
                    proxy_from_file(self.ssl_certificates)?
                ),
            },
        )?;

        let blobfs_proxy = self
            .storage
            .as_ref()
            .ok_or_else(|| anyhow::anyhow!("couldn't get storage while applying ota"))?
            .wipe_or_get_storage()
            .await?
            .into_proxy()?;
        self.outgoing_dir.add_entry("blob", vfs::remote::remote_dir(blobfs_proxy))?;

        download_and_apply_update(
            self.blobfs_root,
            self.paver_connector,
            channel,
            &self.board_name,
            version,
            self.omaha_config,
        )
        .await
        .context("Installing OTA")?;

        if let Some(keys) = self.authorized_keys {
            match self.minfs_root_path {
                Some(path) => {
                    OtaEnv::install_ssh_certificates(&path, &keys)
                        .context("Installing SSH authorized keys")?;
                }
                None => eprintln!("Skipping SSH key installation: minfs not available"),
            };
        }

        if self.factory_reset {
            match self.storage {
                Some(storage) => {
                    storage.wipe_data().await.context("Wiping data")?;
                }
                // This should only be the case for test environments.
                None => eprintln!("Storage not available, skipping factory reset."),
            };
        }
        Ok(())
    }

    /// Install SSH certificates into the target minfs.
    fn install_ssh_certificates(minfs_root: &str, keys: &str) -> Result<(), Error> {
        std::fs::create_dir(&format!("{}/ssh", minfs_root)).context("Creating ssh dir")?;
        std::fs::write(&format!("{}/ssh/authorized_keys", minfs_root), keys)
            .context("Writing authorized_keys")?;
        Ok(())
    }
}

fn get_config() -> Result<RecoveryUpdateConfig, Error> {
    //TODO: Read config from vbmeta before falling back to json config
    let ota_config: RecoveryUpdateConfig = serde_json::from_reader(BufReader::new(
        File::open(PATH_TO_RECOVERY_CONFIG).context("Failed to find update config data")?,
    ))?;
    Ok(ota_config)
}

async fn get_running_version() -> Result<String, Error> {
    let proxy = match client::connect_to_protocol::<BuildInfoMarker>() {
        Ok(p) => p,
        Err(err) => bail!("Failed to connect to fuchsia.buildinfo.Provider proxy: {:?}", err),
    };
    let build_info = proxy.get_build_info().await.context("Failed to read build info")?;
    build_info.version.ok_or(format_err!("No version string provided"))
}

/// Run an OTA from a development host. Returns when the system and SSH keys have been installed.
pub async fn run_devhost_ota(
    cfg: DevhostConfig,
    out_dir: ServerEnd<fio::NodeMarker>,
) -> Result<(), Error> {
    // TODO(fxbug.dev/112997): deduplicate this spinup code with the code in
    // ota_main.rs. To do that, we'll need to remove the run_devhost_ota call
    // from //src/recovery/system/src/main.rs and make run_*_ota public to only ota_main.rs.
    // Also, remove out_dir - ota_main.rs should provide an outgoing directory already spun up.
    let outgoing_dir_vfs = vfs::mut_pseudo_directory! {};

    let scope = vfs::execution_scope::ExecutionScope::new();
    outgoing_dir_vfs.clone().open(
        scope.clone(),
        fio::OpenFlags::RIGHT_READABLE
            | fio::OpenFlags::RIGHT_WRITABLE
            | fio::OpenFlags::RIGHT_EXECUTABLE,
        0,
        vfs::path::Path::dot(),
        out_dir,
    );
    fasync::Task::local(async move { scope.wait().await }).detach();

    let ota_env = OtaEnvBuilder::new(outgoing_dir_vfs)
        .devhost(cfg)
        .build()
        .await
        .context("Failed to create devhost OTA env")?;
    ota_env.do_ota("devhost", "20200101.1.1").await
}

/// Run an OTA against a TUF or Omaha server. Returns Ok after the system has successfully been installed.
pub async fn run_wellknown_ota(
    storage_type: StorageType,
    outgoing_dir: Arc<Simple>,
) -> Result<(), Error> {
    let config: RecoveryUpdateConfig = get_config().context("Couldn't get config")?;

    let version = get_running_version().await.context("Error reading version")?;
    // Check for testing override
    let version = config.override_version.unwrap_or(version);

    match config.update_type {
        UpdateType::Tuf => {
            println!("recovery-ota: Creating TUF OTA environment");
            let ota_env = OtaEnvBuilder::new(outgoing_dir)
                .storage_type(storage_type)
                .build()
                .await
                .context("Failed to create OTA env")?;
            let channel = config.default_channel;
            println!(
                "recovery-ota: Starting TUF OTA on channel '{}' against version '{}'",
                &channel, &version
            );
            ota_env.do_ota(&channel, &version).await
        }
        UpdateType::Omaha(app_id, service_url) => {
            println!("recovery-ota: Creating Omaha OTA environment");
            // Check for testing override
            let service_url = service_url.unwrap_or(DEFAULT_OMAHA_SERVICE_URL.to_string());
            println!(
                "recovery-ota: trying Omaha OTA on channel '{}' against version '{}', with service URL '{}' and app id '{}'",
                &config.default_channel, &version, &service_url, &app_id
            );

            let ota_env = OtaEnvBuilder::new(outgoing_dir)
                .omaha_config(OmahaConfig { app_id: app_id, server_url: service_url })
                .storage_type(storage_type)
                .build()
                .await
                .context("Failed to create OTA env");

            match ota_env {
                Ok(ref _ota_env) => {
                    println!("got no error while creating OTA env...")
                }
                Err(ref e) => {
                    eprintln!("got error while creating OTA env: {:?}", e)
                }
            }

            println!(
                "recovery-ota: Starting Omaha OTA on channel '{}' against version '{}'",
                &config.default_channel, &version
            );
            let res = ota_env?.do_ota(&config.default_channel, &version).await;
            println!("recovery-ota: OTA result: {:?}", res);
            res
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum UpdateType {
    /// Designates an Omaha based update
    /// Parameters:
    ///     app_id: The omaha application id
    ///     omaha_service_url: Override the default omaha service to query
    Omaha(String, Option<String>),
    /// Designates a TUF based update
    Tuf,
}

#[derive(Debug, Clone, PartialEq, Eq, Deserialize)]
pub struct RecoveryUpdateConfig {
    default_channel: String,
    update_type: UpdateType,
    override_version: Option<String>,
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        blobfs_ramdisk::BlobfsRamdisk,
        fidl_fuchsia_pkg_ext::RepositoryKey,
        fuchsia_async as fasync,
        fuchsia_pkg_testing::{
            make_epoch_json, serve::HttpResponder, Package, PackageBuilder, RepositoryBuilder,
        },
        fuchsia_runtime::{take_startup_handle, HandleType},
        futures::future::{ready, BoxFuture},
        hyper::{header, Body, Request, Response, StatusCode},
        mock_paver::MockPaverServiceBuilder,
        std::{
            collections::{BTreeSet, HashMap},
            sync::{Arc, Mutex},
        },
    };

    /// Wrapper around a ramdisk blobfs and a temporary directory
    /// we pretend is minfs.
    struct FakeStorage {
        blobfs: BlobfsRamdisk,
        minfs: tempfile::TempDir,
    }

    impl FakeStorage {
        pub fn new() -> Result<Self, Error> {
            let minfs = tempfile::tempdir().context("making tempdir")?;
            let blobfs = BlobfsRamdisk::start().context("launching blobfs")?;
            Ok(FakeStorage { blobfs, minfs })
        }

        /// Get all the blobs inside the blobfs.
        pub fn list_blobs(&self) -> Result<BTreeSet<fuchsia_merkle::Hash>, Error> {
            self.blobfs.list_blobs()
        }

        /// Get the blobfs root directory.
        pub fn blobfs_root(&self) -> Result<ClientEnd<fio::DirectoryMarker>, Error> {
            self.blobfs.root_dir_handle()
        }

        /// Get the path to be used for minfs.
        pub fn minfs_path(&self) -> String {
            self.minfs.path().to_string_lossy().into_owned()
        }
    }

    /// This wraps a |FakeConfigHandler| in an |Arc|
    /// so that we can implement UriPathHandler for it.
    struct FakeConfigArc {
        pub arc: Arc<FakeConfigHandler>,
    }

    /// This class is used to provide the '/config.json' endpoint
    /// which the OTA process uses to discover information about the devhost repo.
    struct FakeConfigHandler {
        repo_keys: BTreeSet<RepositoryKey>,
        address: Mutex<String>,
    }

    impl FakeConfigHandler {
        pub fn new(repo_keys: BTreeSet<RepositoryKey>) -> Arc<Self> {
            Arc::new(FakeConfigHandler { repo_keys, address: Mutex::new("unknown".to_owned()) })
        }

        pub fn set_repo_address(self: Arc<Self>, addr: String) {
            let mut val = self.address.lock().unwrap();
            *val = addr;
        }
    }

    impl HttpResponder for FakeConfigArc {
        fn respond(
            &self,
            request: &Request<Body>,
            response: Response<Body>,
        ) -> BoxFuture<'_, Response<Body>> {
            if request.uri().path() != "/config.json" {
                return ready(response).boxed();
            }

            // We don't expect any contention on this lock: we only need it
            // because the test doesn't know the address of the server until it's running.
            let val = self.arc.address.lock().unwrap();
            if *val == "unknown" {
                panic!("Expected address to be set!");
            }

            // This emulates the format returned by `pm serve` running on a devhost.
            let config = json!({
                "ID": &*val,
                "RepoURL": &*val,
                "BlobRepoURL": format!("{}/blobs", val),
                "RatePeriod": 60,
                "RootKeys": self.arc.repo_keys,
                "StatusConfig": {
                    "Enabled": true
                },
                "Auto": true,
                "BlobKey": null,
            });

            let json_str = serde_json::to_string(&config).context("Serializing JSON").unwrap();
            let response = Response::builder()
                .status(StatusCode::OK)
                .header(header::CONTENT_LENGTH, json_str.len())
                .body(Body::from(json_str))
                .unwrap();

            ready(response).boxed()
        }
    }

    const EMPTY_REPO_PATH: &str = "/pkg/empty-repo";
    const TEST_SSL_CERTS: &str = "/pkg/data/ssl";

    /// Represents an OTA that is yet to be run.
    struct TestOtaEnv {
        authorized_keys: Option<String>,
        images: HashMap<String, Vec<u8>>,
        packages: Vec<Package>,
        storage: FakeStorage,
    }

    impl TestOtaEnv {
        pub fn new() -> Result<Self, Error> {
            Ok(TestOtaEnv {
                authorized_keys: None,
                images: HashMap::new(),
                packages: vec![],
                storage: FakeStorage::new().context("Starting fake storage")?,
            })
        }

        /// Add a package to be installed by this OTA.
        pub fn add_package(mut self, p: Package) -> Self {
            self.packages.push(p);
            self
        }

        /// Add an image to include in the update package for this OTA.
        pub fn add_image(mut self, name: &str, data: &str) -> Self {
            self.images.insert(name.to_owned(), data.to_owned().into_bytes());
            self
        }

        /// Set the authorized keys to be installed by the OTA.
        pub fn authorized_keys(mut self, keys: &str) -> Self {
            self.authorized_keys = Some(keys.to_owned());
            self
        }

        /// Generates the packages.json file for the update package.
        fn generate_packages_list(&self) -> String {
            let package_urls: Vec<String> = self
                .packages
                .iter()
                .map(|p| {
                    format!(
                        "fuchsia-pkg://fuchsia.com/{}/0?hash={}",
                        p.name(),
                        p.meta_far_merkle_root()
                    )
                })
                .collect();
            let packages = json!({
                "version": 1,
                "content": package_urls,
            });
            serde_json::to_string(&packages).unwrap()
        }

        /// Build an update package from the list of packages and images included
        /// in this update.
        async fn make_update_package(&self) -> Result<Package, Error> {
            let mut update = PackageBuilder::new("update")
                .add_resource_at("packages.json", self.generate_packages_list().as_bytes());

            for (name, data) in self.images.iter() {
                update = update.add_resource_at(name, data.as_slice());
            }

            update.build().await.context("Building update package")
        }

        /// Run the OTA.
        pub async fn run_ota(&mut self) -> Result<(), Error> {
            let update = self.make_update_package().await?;
            // Create the repo.
            let repo = Arc::new(
                self.packages
                    .iter()
                    .fold(
                        RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH).add_package(&update),
                        |repo, package| repo.add_package(package),
                    )
                    .build()
                    .await
                    .context("Building repo")?,
            );
            // We expect the update package to be in blobfs, so add it to the list of packages.
            self.packages.push(update);

            // Add a hook to handle the config.json file, which is exposed by
            // `pm serve` to enable autoconfiguration of repositories.
            let request_handler = FakeConfigHandler::new(repo.root_keys());
            let served_repo = Arc::clone(&repo)
                .server()
                .response_overrider(FakeConfigArc { arc: Arc::clone(&request_handler) })
                .start()
                .context("Starting repository")?;

            // Configure the address of the repository for config.json
            let url = served_repo.local_url();
            let config_url = format!("{}/config.json", url);
            request_handler.set_repo_address(url);

            // Set up the mock paver.
            let mock_paver = Arc::new(MockPaverServiceBuilder::new().build());
            let (paver_connector, remote) = fidl::endpoints::create_endpoints()?;
            let mut paver_fs = ServiceFs::new();
            let paver_clone = Arc::clone(&mock_paver);
            paver_fs.add_fidl_service(move |stream: fpaver::PaverRequestStream| {
                fasync::Task::spawn(
                    Arc::clone(&paver_clone)
                        .run_paver_service(stream)
                        .unwrap_or_else(|e| panic!("Failed to run paver: {:?}", e)),
                )
                .detach();
            });
            paver_fs.serve_connection(remote).context("serving paver svcfs")?;
            fasync::Task::spawn(paver_fs.collect()).detach();
            let paver_connector = ClientEnd::from(paver_connector);

            // Get the devhost config
            let cfg = DevhostConfig {
                url: config_url,
                authorized_keys: self
                    .authorized_keys
                    .as_ref()
                    .map(|p| p.clone())
                    .unwrap_or("".to_owned()),
            };

            let directory_handle = take_startup_handle(HandleType::DirectoryRequest.into())
                .expect("cannot take startup handle");
            let outgoing_dir = fuchsia_zircon::Channel::from(directory_handle).into();
            let outgoing_dir_vfs = vfs::mut_pseudo_directory! {};

            let scope = vfs::execution_scope::ExecutionScope::new();
            outgoing_dir_vfs.clone().open(
                scope.clone(),
                fio::OpenFlags::RIGHT_READABLE
                    | fio::OpenFlags::RIGHT_WRITABLE
                    | fio::OpenFlags::RIGHT_EXECUTABLE,
                0,
                vfs::path::Path::dot(),
                outgoing_dir,
            );
            fasync::Task::local(async move { scope.wait().await }).detach();

            // Build the environment, and do the OTA.
            let ota_env = OtaEnvBuilder::new(outgoing_dir_vfs)
                .board_name("x64")
                .storage_type(StorageType::Fake {
                    blobfs_root: self.storage.blobfs_root().context("Opening blobfs root")?,
                    minfs_path: Some(self.storage.minfs_path()),
                })
                .fake_paver(paver_connector)
                .ssl_certificates(TEST_SSL_CERTS)
                .devhost(cfg)
                .factory_reset(false)
                .build()
                .await
                .context("Building environment")?;

            ota_env.do_ota("devhost", "20200101.1.1").await.context("Running OTA")?;
            Ok(())
        }

        /// Check that the blobfs contains exactly the blobs we expect it to contain.
        pub async fn check_blobs(&self) {
            let written_blobs = self.storage.list_blobs().expect("Listing blobfs blobs");
            let mut all_package_blobs = BTreeSet::new();
            for package in self.packages.iter() {
                all_package_blobs.append(&mut package.list_blobs().expect("Listing package blobs"));
            }

            assert_eq!(written_blobs, all_package_blobs);
        }

        /// Check that the authorized keys file is what we expect it to be.
        pub async fn check_keys(&self) {
            let keys_path = format!("{}/ssh/authorized_keys", self.storage.minfs_path());
            if let Some(expected) = &self.authorized_keys {
                let result =
                    std::fs::read_to_string(keys_path).expect("Failed to read authorized keys!");
                assert_eq!(&result, expected);
            } else {
                assert_eq!(std::fs::read_to_string(keys_path).unwrap(), "");
            }
        }
    }

    #[ignore] //TODO(fxbug.dev/102239) Move to integration test
    #[fasync::run_singlethreaded(test)]
    async fn test_run_devhost_ota() -> Result<(), Error> {
        let package = PackageBuilder::new("test-package")
            .add_resource_at("data/file1", "Hello, world!".as_bytes())
            .build()
            .await
            .unwrap();
        let mut env = TestOtaEnv::new()?
            .add_package(package)
            .add_image("zbi.signed", "zbi image")
            .add_image("fuchsia.vbmeta", "fuchsia vbmeta")
            .add_image("epoch.json", &make_epoch_json(1))
            .authorized_keys("test authorized keys file!");

        env.run_ota().await?;
        env.check_blobs().await;
        env.check_keys().await;
        Ok(())
    }

    #[test]
    fn test_omaha_config_new_url() {
        let a = RecoveryUpdateConfig {
            default_channel: "some_channel".to_string(),
            override_version: None,
            update_type: UpdateType::Omaha(
                "app_id_here".to_string(),
                Some("https://override.google.com".to_string()),
            ),
        };
        let string_version = r#"{
            "default_channel": "some_channel",
            "update_type": {
                "omaha": [
                    "app_id_here",
                    "https://override.google.com"
                ]
            }
        }"#;
        assert_eq!(a, serde_json::from_str(string_version).unwrap());
    }

    #[test]
    fn test_omaha_config() {
        let a = RecoveryUpdateConfig {
            default_channel: "some_channel".to_string(),
            override_version: None,
            update_type: UpdateType::Omaha("app_id_here".to_string(), None),
        };
        let string_version = r#"{
            "default_channel": "some_channel",
            "update_type": {
                "omaha": [
                    "app_id_here", null
                ]
            }
        }"#;
        assert_eq!(a, serde_json::from_str(string_version).unwrap());
    }

    #[test]
    fn test_tuf_config() {
        let a = RecoveryUpdateConfig {
            default_channel: "another_channel".to_string(),
            override_version: Some("1.2.3.4".to_string()),
            update_type: UpdateType::Tuf,
        };
        let string_version = r#"
        {
            "default_channel": "another_channel",
            "override_version": "1.2.3.4",
            "update_type": "tuf"
        }"#;
        assert_eq!(a, serde_json::from_str(string_version).unwrap());
    }
}
