// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Test tools for building and serving TUF repositories containing Fuchsia packages.

use {
    crate::{package::Package, serve::ServedRepositoryBuilder},
    bytes::Buf,
    failure::{bail, format_err, Error, ResultExt},
    fidl_fuchsia_pkg_ext::{
        MirrorConfigBuilder, RepositoryBlobKey, RepositoryConfig, RepositoryConfigBuilder,
        RepositoryKey,
    },
    fidl_fuchsia_sys::LauncherProxy,
    fuchsia_async::{DurationExt, TimeoutExt},
    fuchsia_component::client::{launcher, App, AppBuilder, ExitStatus},
    fuchsia_merkle::Hash,
    fuchsia_url::pkg_url::RepoUrl,
    fuchsia_zircon::{self as zx, sys::zx_handle_t, DurationNum, Status},
    futures::{
        compat::{Future01CompatExt, Stream01CompatExt},
        future::BoxFuture,
        prelude::*,
    },
    hyper::{Body, Request, StatusCode},
    serde_derive::Deserialize,
    std::{
        collections::{BTreeMap, BTreeSet},
        fmt,
        fs::{self, File},
        io::{self, Cursor, Read, Write},
        os::unix::io::AsRawFd,
        path::PathBuf,
        sync::Arc,
        time::Duration,
    },
    tempfile::TempDir,
    walkdir::WalkDir,
};

/// A builder to simplify construction of TUF repositories containing Fuchsia packages.
#[derive(Debug)]
pub struct RepositoryBuilder<'a> {
    packages: Vec<PackageRef<'a>>,
    encryption_key: Option<BlobEncryptionKey>,
    repodir: Option<PathBuf>,
}

impl<'a> RepositoryBuilder<'a> {
    /// Creates a new `RepositoryBuilder`.
    pub fn new() -> Self {
        Self { packages: vec![], encryption_key: None, repodir: None }
    }

    /// Creates a new `RepositoryBuilder` from a template TUF repository dir.
    pub fn from_template_dir(path: impl Into<PathBuf>) -> Self {
        Self { packages: vec![], encryption_key: None, repodir: Some(path.into()) }
    }

    /// Adds a package (or a reference to one) to the repository.
    pub fn add_package(mut self, package: impl Into<PackageRef<'a>>) -> Self {
        self.packages.push(package.into());
        self
    }

    /// Encrypts blobs in the repository with the given key (default is to not encrypt blobs).
    pub fn set_encryption_key(mut self, key: BlobEncryptionKey) -> Self {
        self.encryption_key = Some(key);
        self
    }

    /// Builds the repository.
    pub async fn build(self) -> Result<Repository, Error> {
        let indir = tempfile::tempdir().context("create /in")?;
        let repodir = tempfile::tempdir().context("create /repo")?;

        {
            let mut manifest = File::create(indir.path().join("manifests.list"))?;
            for package in &self.packages {
                writeln!(manifest, "/packages/{}/manifest.json", package.get().name())?;
            }
        }

        // If configured to use a template repository directory, first copy it into the repo dir.
        if let Some(templatedir) = self.repodir {
            for entry in WalkDir::new(&templatedir) {
                let entry = entry?;
                if entry.path() == &templatedir {
                    continue;
                }
                let relative_entry_path = entry.path().strip_prefix(&templatedir)?;
                let target_path = repodir.path().join(relative_entry_path);
                if entry.file_type().is_dir() {
                    fs::create_dir(target_path)?;
                } else {
                    fs::copy(entry.path(), target_path)?;
                }
            }
        }

        let mut pm = AppBuilder::new("fuchsia-pkg://fuchsia.com/pm#meta/pm.cmx")
            .arg("publish")
            .arg("-lp")
            .arg("-f=/in/manifests.list")
            .arg("-repo=/repo")
            .add_dir_to_namespace("/in".to_owned(), File::open(indir.path()).context("open /in")?)?
            .add_dir_to_namespace(
                "/repo".to_owned(),
                File::open(repodir.path()).context("open /repo")?,
            )?;

        if let Some(ref key) = self.encryption_key {
            fs::write(indir.path().join("encryption.key"), key.as_bytes())?;
            pm = pm.arg("-e=/in/encryption.key");
        }

        for package in &self.packages {
            let package = package.get();
            pm = pm.add_dir_to_namespace(
                format!("/packages/{}", package.name()),
                File::open(package.artifacts()).context("open package dir")?,
            )?;
        }

        pm.output(&launcher()?)?.await?.ok()?;

        Ok(Repository { dir: repodir, encryption_key: self.encryption_key })
    }
}

/// An owned [`Package`] or a reference to one.
#[derive(Debug)]
pub enum PackageRef<'a> {
    Owned(Package),
    Ref(&'a Package),
}

impl PackageRef<'_> {
    fn get(&self) -> &Package {
        match *self {
            PackageRef::Owned(ref p) => p,
            PackageRef::Ref(p) => p,
        }
    }
}

impl From<Package> for PackageRef<'_> {
    fn from(p: Package) -> Self {
        PackageRef::Owned(p)
    }
}

impl<'a> From<&'a Package> for PackageRef<'a> {
    fn from(p: &'a Package) -> Self {
        PackageRef::Ref(p)
    }
}

/// A repository blob encryption key.
pub struct BlobEncryptionKey([u8; 32]);

impl BlobEncryptionKey {
    /// Returns a slice of all bytes in the key.
    pub fn as_bytes(&self) -> &[u8] {
        &self.0[..]
    }
}

impl fmt::Debug for BlobEncryptionKey {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_tuple("BlobEncryptionKey").field(&hex::encode(self.as_bytes())).finish()
    }
}

/// Metadata for a package contained within a [`Repository`].
#[derive(Debug, PartialOrd, Ord, PartialEq, Eq)]
pub struct PackageEntry {
    path: String,
    meta_far_merkle: Hash,
    meta_far_size: usize,
}

fn iter_packages(
    reader: impl Read,
) -> Result<impl Iterator<Item = Result<PackageEntry, Error>>, Error> {
    // TODO when metadata is compatible, use rust-tuf instead.
    #[derive(Debug, Deserialize)]
    struct TargetsJson {
        signed: Targets,
    }
    #[derive(Debug, Deserialize)]
    struct Targets {
        targets: BTreeMap<String, Target>,
    }
    #[derive(Debug, Deserialize)]
    struct Target {
        custom: TargetCustom,
    }
    #[derive(Debug, Deserialize)]
    struct TargetCustom {
        merkle: String,
        size: usize,
    }

    let targets_json: TargetsJson = serde_json::from_reader(reader)?;

    Ok(targets_json.signed.targets.into_iter().map(|(path, target)| {
        Ok(PackageEntry {
            path,
            meta_far_merkle: target.custom.merkle.parse()?,
            meta_far_size: target.custom.size,
        })
    }))
}

/// A TUF repository generated by a [`RepositoryBuilder`].
#[derive(Debug)]
pub struct Repository {
    dir: TempDir,
    encryption_key: Option<BlobEncryptionKey>,
}

impl Repository {
    /// Returns an iterator over all blobs contained in this repository.
    pub fn iter_blobs(&self) -> Result<impl Iterator<Item = Result<Hash, Error>>, io::Error> {
        Ok(fs::read_dir(self.dir.path().join("repository/blobs"))?.map(|entry| {
            Ok(entry?
                .file_name()
                .to_str()
                .ok_or_else(|| format_err!("non-utf8 file path"))?
                .parse()?)
        }))
    }

    /// Returns a set of all blobs contained in this repository.
    pub fn list_blobs(&self) -> Result<BTreeSet<Hash>, Error> {
        self.iter_blobs()?.collect()
    }

    /// Reads the contents of requested blob from the repository.
    pub fn read_blob(&self, merkle_root: &Hash) -> Result<Vec<u8>, io::Error> {
        fs::read(self.dir.path().join(format!("repository/blobs/{}", merkle_root)))
    }

    /// Returns the path of the base of the repository.
    pub fn path(&self) -> PathBuf {
        self.dir.path().join("repository")
    }

    /// Returns an iterator over all packages contained in this repository.
    pub fn iter_packages(
        &self,
    ) -> Result<impl Iterator<Item = Result<PackageEntry, Error>>, Error> {
        iter_packages(io::BufReader::new(File::open(
            self.dir.path().join("repository/targets.json"),
        )?))
    }

    /// Returns a sorted vector of all packages contained in this repository.
    pub fn list_packages(&self) -> Result<Vec<PackageEntry>, Error> {
        let mut packages = self.iter_packages()?.collect::<Result<Vec<_>, _>>()?;
        packages.sort_unstable();
        Ok(packages)
    }

    /// Generate a [`RepositoryConfig`] suitable for configuring a package resolver to use this
    /// repository when it is served at the given URL.
    pub fn make_repo_config(&self, url: RepoUrl, mirror_url: String) -> RepositoryConfig {
        let mut builder = RepositoryConfigBuilder::new(url);

        for key in self.root_keys() {
            builder = builder.add_root_key(key);
        }

        let mut mirror = MirrorConfigBuilder::new(mirror_url).subscribe(false);
        if let Some(ref key) = self.encryption_key {
            mirror = mirror.blob_key(RepositoryBlobKey::Aes(key.as_bytes().to_vec()))
        }
        builder.add_mirror(mirror.build()).build()
    }

    fn root_keys(&self) -> BTreeSet<RepositoryKey> {
        // TODO when metadata is compatible, use rust-tuf instead.
        #[derive(Debug, Deserialize)]
        struct RootJson {
            signed: Root,
        }
        #[derive(Debug, Deserialize)]
        struct Root {
            roles: BTreeMap<String, Role>,
            keys: BTreeMap<String, Key>,
        }
        #[derive(Debug, Deserialize)]
        struct Role {
            keyids: Vec<String>,
        }
        #[derive(Debug, Deserialize)]
        struct Key {
            keyval: KeyVal,
        }
        #[derive(Debug, Deserialize)]
        struct KeyVal {
            public: String,
        }

        let root_json: RootJson = serde_json::from_reader(io::BufReader::new(
            File::open(self.dir.path().join("repository/root.json")).unwrap(),
        ))
        .unwrap();
        let root = root_json.signed;

        root.roles["root"]
            .keyids
            .iter()
            .map(|keyid| {
                RepositoryKey::Ed25519(hex::decode(root.keys[keyid].keyval.public.clone()).unwrap())
            })
            .collect()
    }

    /// Serves the repository over HTTP using hyper.
    pub fn build_server(self: Arc<Self>) -> ServedRepositoryBuilder {
        ServedRepositoryBuilder::new(self)
    }

    /// Serves the repository over HTTP.
    pub async fn serve<'a>(
        &'a self,
        launcher: &'_ LauncherProxy,
    ) -> Result<ServedRepository<'a>, Error> {
        let indir = tempfile::tempdir().context("create /in")?;
        let port_file_dir = tempfile::tempdir().unwrap();
        let mut pm = AppBuilder::new("fuchsia-pkg://fuchsia.com/pm#meta/pm.cmx")
            .stdout(stdout_handle())
            .stderr(stdout_handle())
            .arg("serve")
            .arg("-l=127.0.0.1:0")
            .arg("-f=/port-file-dir/port-file")
            .add_dir_to_namespace("/port-file-dir".to_owned(), File::open(port_file_dir.path())?)?
            .arg("-repo=/repo")
            .add_dir_to_namespace(
                "/repo".to_owned(),
                File::open(self.dir.path()).context("open /repo")?,
            )?;

        if let Some(ref key) = self.encryption_key {
            fs::write(indir.path().join("encryption.key"), key.as_bytes())?;
            pm = pm.arg("-e=/in/encryption.key").add_dir_to_namespace(
                "/in".to_owned(),
                File::open(indir.path()).context("open /in")?,
            )?;
        }

        let pm = pm.spawn(launcher)?;

        // Wait for "pm serve" to either create the port file (giving up after a 20 seconds) or
        // exit, whichever happens first.

        let wait_pm_down = ExitStatus::from_event_stream(pm.controller().take_event_stream());

        // Under high load, a fast retry timeout can prevent pm from starting up. Start out fast,
        // but slow down if pm doesn't come up quickly.
        let backoff = std::iter::repeat(Duration::from_millis(250))
            .take(4)
            .chain(std::iter::repeat(Duration::from_millis(500)).take(4))
            .chain(std::iter::repeat(Duration::from_millis(1000)));

        let port_file_dir_path = port_file_dir.path();
        let wait_pm_up = fuchsia_backoff::retry_or_last_error(backoff, || async move {
            match fs::read_to_string(port_file_dir_path.join("port-file")) {
                Ok(port) => {
                    return Ok(port
                        .parse::<u16>()
                        .unwrap_or_else(|e| panic!("invalid port string {:?}: {:?}", port, e)));
                }
                Err(e) => {
                    return Err(e.into());
                }
            }
        })
        .on_timeout(20.seconds().after_now(), || {
            bail!("timed out waiting for 'pm serve' to create port file")
        })
        .boxed();

        let (wait_pm_down, port) = match future::select(wait_pm_up, wait_pm_down).await {
            future::Either::Left((res, wait_pm_down)) => match res {
                Err(e) => {
                    panic!("'pm serve' took too long to create the port file {:?}", e,);
                }
                Ok(port) => (wait_pm_down.boxed(), port),
            },
            future::Either::Right((exit_status, _)) => {
                panic!("'pm serve' exited too soon: {:?}", exit_status);
            }
        };

        Ok(ServedRepository { repo: self, port, _indir: indir, pm, wait_pm_down })
    }
}

/// A repository that is being served over HTTP. When dropped, the server will be stopped.
pub struct ServedRepository<'a> {
    repo: &'a Repository,
    port: u16,
    _indir: TempDir,
    pm: App,
    wait_pm_down: BoxFuture<'a, Result<ExitStatus, Error>>,
}

impl<'a> ServedRepository<'a> {
    /// Request the given path served by the repository over HTTP.
    pub async fn get(&self, path: impl AsRef<str>) -> Result<Vec<u8>, Error> {
        let url = format!("http://127.0.0.1:{}/{}", self.port, path.as_ref());
        get(url).await
    }

    /// Returns a sorted vector of all packages contained in this repository.
    pub async fn list_packages(&self) -> Result<Vec<PackageEntry>, Error> {
        let targets_json = self.get("targets.json").await?;
        let mut packages =
            iter_packages(Cursor::new(targets_json))?.collect::<Result<Vec<_>, _>>()?;
        packages.sort_unstable();
        Ok(packages)
    }

    /// Returns the URL that can be used to connect to this repository from this device.
    pub fn local_url(&self) -> String {
        format!("http://127.0.0.1:{}", self.port)
    }

    /// Generate a [`RepositoryConfig`] suitable for configuring a package resolver to use this
    /// served repository.
    pub fn make_repo_config(&self, url: RepoUrl) -> RepositoryConfig {
        self.repo.make_repo_config(url, self.local_url())
    }

    /// Kill the pm component and wait for it to exit.
    pub async fn stop(mut self) {
        self.pm.kill().expect("pm to have been running");
        self.wait_pm_down.await.expect("pm to exit");
    }
}

pub(crate) async fn get(url: impl AsRef<str>) -> Result<Vec<u8>, Error> {
    let request = Request::get(url.as_ref()).body(Body::empty()).map_err(|e| Error::from(e))?;
    let client = fuchsia_hyper::new_client();
    let response = client.request(request).compat().await?;

    if response.status() != StatusCode::OK {
        bail!("unexpected status code: {:?}", response.status());
    }

    let body = response.into_body().compat().try_concat().await?.collect();

    Ok(body)
}

fn clone_fd(raw_fd: impl AsRawFd) -> Result<zx::Handle, zx::Status> {
    let raw_fd = raw_fd.as_raw_fd();
    let mut handle: zx_handle_t = zx::sys::ZX_HANDLE_INVALID;
    let handle_ptr: *mut zx_handle_t = &mut handle;
    // `handle_ptr` is the only reference to `handle`.
    Status::ok(unsafe { fdio::fdio_sys::fdio_fd_clone(raw_fd, handle_ptr) })?;
    // No other value owns `handle`.
    Ok(unsafe { zx::Handle::from_raw(handle) })
}

fn stdout_handle() -> zx::Handle {
    clone_fd(io::stdout()).unwrap()
}

#[cfg(test)]
mod tests {
    use {super::*, crate::package::PackageBuilder, fuchsia_merkle::MerkleTree, serde_json::Value};

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_repo_builder() -> Result<(), Error> {
        let same_contents = "same contents";
        let repo = RepositoryBuilder::new()
            .add_package(
                PackageBuilder::new("rolldice")
                    .add_resource_at("bin/rolldice", "#!/boot/bin/sh\necho 4\n".as_bytes())
                    .add_resource_at(
                        "meta/rolldice.cmx",
                        r#"{"program":{"binary":"bin/rolldice"}}"#.as_bytes(),
                    )
                    .add_resource_at("data/duplicate_a", "same contents".as_bytes())
                    .build()
                    .await?,
            )
            .add_package(
                PackageBuilder::new("fortune")
                    .add_resource_at(
                        "bin/fortune",
                        "#!/boot/bin/sh\necho ask again later\n".as_bytes(),
                    )
                    .add_resource_at(
                        "meta/fortune.cmx",
                        r#"{"program":{"binary":"bin/fortune"}}"#.as_bytes(),
                    )
                    .add_resource_at("data/duplicate_b", same_contents.as_bytes())
                    .add_resource_at("data/duplicate_c", same_contents.as_bytes())
                    .build()
                    .await?,
            )
            .build()
            .await?;

        let blobs = repo.list_blobs()?;
        // 2 meta FARs, 2 binaries, and 1 duplicated resource
        assert_eq!(blobs.len(), 5);

        // Spot check the contents of a blob in the repo.
        let same_contents_merkle = MerkleTree::from_reader(same_contents.as_bytes())?.root();
        assert_eq!(repo.read_blob(&same_contents_merkle)?.as_slice(), same_contents.as_bytes());

        let packages = repo.list_packages()?;
        assert_eq!(packages.len(), 2);
        assert_eq!(
            packages.into_iter().map(|pkg| pkg.path).collect::<Vec<_>>(),
            vec!["fortune/0".to_owned(), "rolldice/0".to_owned()]
        );

        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_repo_encryption() -> Result<(), Error> {
        let message = "Hello World!".as_bytes();
        let repo = RepositoryBuilder::new()
            .add_package(
                PackageBuilder::new("tiny")
                    .add_resource_at("data/message", message)
                    .build()
                    .await?,
            )
            .set_encryption_key(BlobEncryptionKey([
                0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd,
                0xee, 0xff, 0xff, 0xee, 0xdd, 0xcc, 0xbb, 0xaa, 0x99, 0x88, 0x77, 0x66, 0x55, 0x44,
                0x33, 0x22, 0x11, 0x00,
            ]))
            .build()
            .await?;

        // No blob in the repo should contain `message`.
        for blob in repo.iter_blobs()? {
            let blob = blob?;
            assert_ne!(repo.read_blob(&blob)?, message);
        }

        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_repo_builder_template() -> Result<(), Error> {
        let repodir = tempfile::tempdir().context("create tempdir")?;

        // Populate repodir with a freshly created repository.
        AppBuilder::new("fuchsia-pkg://fuchsia.com/pm#meta/pm.cmx")
            .arg("newrepo")
            .arg("-repo=/repo")
            .add_dir_to_namespace(
                "/repo".to_owned(),
                File::open(repodir.path()).context("open /repo")?,
            )?
            .output(&launcher()?)?
            .await?
            .ok()?;

        // Build a repo from the template.
        let repo = RepositoryBuilder::from_template_dir(repodir.path())
            .add_package(PackageBuilder::new("test").build().await?)
            .build()
            .await?;

        // Ensure the repository used the generated keys.
        for path in &["root.json", "snapshot.json", "timestamp.json", "targets.json"] {
            assert_eq!(
                fs::read(repodir.path().join("keys").join(path))?,
                fs::read(repo.dir.path().join("keys").join(path))?,
            );
        }

        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_serve_empty() -> Result<(), Error> {
        let repo = RepositoryBuilder::new().build().await?;
        let launcher = launcher().unwrap();
        let served_repo = repo.serve(&launcher).await?;

        let packages = served_repo.list_packages().await?;
        assert_eq!(packages, vec![]);

        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_serve_packages() -> Result<(), Error> {
        let same_contents = "same contents";
        let repo = RepositoryBuilder::new()
            .add_package(
                PackageBuilder::new("rolldice")
                    .add_resource_at("bin/rolldice", "#!/boot/bin/sh\necho 4\n".as_bytes())
                    .add_resource_at(
                        "meta/rolldice.cmx",
                        r#"{"program":{"binary":"bin/rolldice"}}"#.as_bytes(),
                    )
                    .add_resource_at("data/duplicate_a", "same contents".as_bytes())
                    .build()
                    .await?,
            )
            .add_package(
                PackageBuilder::new("fortune")
                    .add_resource_at(
                        "bin/fortune",
                        "#!/boot/bin/sh\necho ask again later\n".as_bytes(),
                    )
                    .add_resource_at(
                        "meta/fortune.cmx",
                        r#"{"program":{"binary":"bin/fortune"}}"#.as_bytes(),
                    )
                    .add_resource_at("data/duplicate_b", same_contents.as_bytes())
                    .add_resource_at("data/duplicate_c", same_contents.as_bytes())
                    .build()
                    .await?,
            )
            .build()
            .await?;

        let launcher = launcher().unwrap();
        let served_repository = repo.serve(&launcher).await?;

        let local_packages = repo.list_packages()?;

        let served_packages = served_repository.list_packages().await?;
        assert_eq!(local_packages, served_packages);

        let config_json = String::from_utf8(served_repository.get("config.json").await?)?;
        let config: Value = serde_json::from_str(config_json.as_str())?;

        let base_url = format!("http://127.0.0.1:{}", served_repository.port);
        assert_eq!(
            config.get("id").or_else(|| config.get("ID")),
            Some(Value::String(base_url)).as_ref()
        );

        Ok(())
    }
}
