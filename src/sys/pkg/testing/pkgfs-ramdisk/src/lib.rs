// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Test utilities for starting a pkgfs server.

use {
    anyhow::{Context as _, Error},
    fdio::{SpawnAction, SpawnOptions},
    fidl::endpoints::ClientEnd,
    fidl_fuchsia_io::{DirectoryMarker, DirectoryProxy},
    fuchsia_merkle::Hash,
    fuchsia_runtime::{HandleInfo, HandleType},
    fuchsia_zircon::AsHandleRef,
    scoped_task::{self, Scoped},
    std::ffi::{CStr, CString},
};

pub use blobfs_ramdisk::{BlobfsRamdisk, BlobfsRamdiskBuilder};

const PKGSVR_PATH: &str = "/pkg/bin/pkgsvr";

#[derive(Debug, Default)]
pub struct PkgfsArgs {
    enforce_packages_non_static_allowlist: Option<bool>,
    enforce_non_base_executability_restrictions: Option<bool>,
    system_image_merkle: Option<Hash>,
}

impl PkgfsArgs {
    fn to_vec(&self) -> Vec<CString> {
        let mut argv = vec![CString::new(PKGSVR_PATH).unwrap()];

        if let Some(value) = &self.enforce_packages_non_static_allowlist {
            argv.push(
                CString::new(format!("--enforcePkgfsPackagesNonStaticAllowlist={}", value))
                    .unwrap(),
            );
        }

        if let Some(value) = &self.enforce_non_base_executability_restrictions {
            argv.push(
                CString::new(format!("--enforceNonBaseExecutabilityRestrictions={}", value))
                    .unwrap(),
            );
        }

        // golang's flag library is very specific about the fact that it wants positional args _last_,
        // so specify other_args first, and the system image arg last.
        if let Some(system_image_merkle) = &self.system_image_merkle {
            argv.push(CString::new(system_image_merkle.to_string().as_bytes()).unwrap());
        }

        argv
    }
}

/// A helper to construct PkgfsRamdisk instances.
pub struct PkgfsRamdiskBuilder {
    blobfs: Option<BlobfsRamdisk>,
    args: PkgfsArgs,
}

impl PkgfsRamdiskBuilder {
    /// Creates a new PkgfsRamdiskBuilder with no configured `blobfs` and default command line
    /// arguments.
    fn new() -> Self {
        Self { blobfs: None, args: PkgfsArgs::default() }
    }

    /// Use the given blobfs when constructing the PkgfsRamdisk.
    pub fn blobfs(mut self, blobfs: BlobfsRamdisk) -> Self {
        self.blobfs = Some(blobfs);
        self
    }

    /// Specify whether or not pkgfs should enforce the /pkgfs/packages non-static allowlist.
    pub fn enforce_packages_non_static_allowlist(mut self, value: impl Into<Option<bool>>) -> Self {
        self.args.enforce_packages_non_static_allowlist = value.into();
        self
    }

    /// Specify whether or not pkgfs should enforce the non-base executability restrictions.
    pub fn enforce_non_base_executability_restrictions(
        mut self,
        value: impl Into<Option<bool>>,
    ) -> Self {
        self.args.enforce_non_base_executability_restrictions = value.into();
        self
    }

    /// Use the given system_image_merkle when constructing the PkgfsRamdisk.
    pub fn system_image_merkle(mut self, system_image_merkle: &Hash) -> Self {
        self.args.system_image_merkle = Some(*system_image_merkle);
        self
    }

    /// Attempt to start the PkgfsRamdisk, consuming this builder.
    pub fn start(self) -> Result<PkgfsRamdisk, Error> {
        let blobfs = if let Some(blobfs) = self.blobfs { blobfs } else { BlobfsRamdisk::start()? };
        let args = self.args.to_vec();
        let argv = args.iter().map(AsRef::as_ref).collect::<Vec<&CStr>>();

        let pkgfs_root_handle_info = HandleInfo::new(HandleType::User0, 0);
        let (proxy, pkgfs_root_server_end) = fidl::endpoints::create_proxy::<DirectoryMarker>()?;

        let process = scoped_task::spawn_etc(
            scoped_task::job_default(),
            SpawnOptions::CLONE_ALL,
            &CString::new(PKGSVR_PATH).unwrap(),
            &argv,
            None,
            &mut [
                SpawnAction::add_handle(
                    pkgfs_root_handle_info,
                    pkgfs_root_server_end.into_channel().into(),
                ),
                SpawnAction::add_namespace_entry(
                    &CString::new("/blob").unwrap(),
                    blobfs.root_dir_handle().context("getting blobfs root dir handle")?.into(),
                ),
            ],
        )
        .map_err(|(status, _)| status)
        .context("spawning 'pkgsvr'")?;

        Ok(PkgfsRamdisk { blobfs, proxy, process, args: self.args })
    }
}

/// A running pkgfs server backed by a ramdisk-backed blobfs instance.
///
/// Make sure to call PkgfsRamdisk.stop() to shut it down properly and receive shutdown errors.
///
/// If dropped, only the ramdisk and dynamic index are deleted.
pub struct PkgfsRamdisk {
    blobfs: BlobfsRamdisk,
    proxy: DirectoryProxy,
    process: Scoped<fuchsia_zircon::Process>,
    args: PkgfsArgs,
}

impl PkgfsRamdisk {
    /// Creates a new [`PkgfsRamdiskBuilder`] with no configured `blobfs` instance or command line
    /// arguments.
    pub fn builder() -> PkgfsRamdiskBuilder {
        PkgfsRamdiskBuilder::new()
    }

    /// Start a pkgfs server.
    pub fn start() -> Result<Self, Error> {
        Self::builder().start()
    }

    /// Returns a reference to the [`BlobfsRamdisk`] backing this pkgfs.
    pub fn blobfs(&self) -> &BlobfsRamdisk {
        &self.blobfs
    }

    /// Returns a new connection to pkgfs's root directory as a raw zircon channel.
    pub fn root_dir_handle(&self) -> Result<ClientEnd<DirectoryMarker>, Error> {
        let (root_clone, server_end) = fuchsia_zircon::Channel::create()?;
        self.proxy.clone(fidl_fuchsia_io::CLONE_FLAG_SAME_RIGHTS, server_end.into())?;
        Ok(root_clone.into())
    }

    /// Returns a new connection to pkgfs's root directory as a DirectoryProxy.
    pub fn root_dir_proxy(&self) -> Result<DirectoryProxy, Error> {
        Ok(self.root_dir_handle()?.into_proxy()?)
    }

    /// Returns a new connetion to pkgfs's root directory as a openat::Dir.
    pub fn root_dir(&self) -> Result<openat::Dir, Error> {
        fdio::create_fd(self.root_dir_handle()?.into()).context("failed to create fd")
    }

    /// Shuts down the pkgfs server, returning a [`PkgfsRamdiskBuilder`] configured with the same
    /// backing blobfs and command line arguments.
    pub fn into_builder(self) -> Result<PkgfsRamdiskBuilder, Error> {
        kill_pkgfs(self.process)?;
        drop(self.proxy);

        Ok(PkgfsRamdiskBuilder { blobfs: Some(self.blobfs), args: self.args })
    }

    /// Restarts pkgfs with the same backing blobfs.
    pub fn restart(self) -> Result<Self, Error> {
        self.into_builder()?.start()
    }

    /// Shuts down the pkgfs server and all the backing infrastructure.
    ///
    /// This also shuts down blobfs and deletes the backing ramdisk.
    pub async fn stop(self) -> Result<(), Error> {
        kill_pkgfs(self.process)?;
        self.blobfs.stop().await
    }
}

/// Kills the pkgfs process and waits for it to terminate.
fn kill_pkgfs(process: Scoped<fuchsia_zircon::Process>) -> Result<(), Error> {
    process
        .kill()
        .context("killing pkgfs")?
        .wait_handle(
            fuchsia_zircon::Signals::PROCESS_TERMINATED,
            fuchsia_zircon::Time::after(fuchsia_zircon::Duration::from_seconds(30)),
        )
        .context("waiting for 'pkgfs' to terminate")?;

    Ok(())
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fuchsia_pkg::{CreationManifest, MetaPackage},
        maplit::{btreemap, hashset},
        matches::assert_matches,
        std::{
            collections::HashSet,
            fs,
            io::{Read, Write},
            path::Path,
        },
    };

    #[fuchsia_async::run_singlethreaded(test)]
    async fn clean_start_and_stop() {
        let pkgfs = PkgfsRamdisk::start().unwrap();

        let proxy = pkgfs.root_dir_proxy().unwrap();
        drop(proxy);

        pkgfs.stop().await.unwrap();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn activate_package() {
        let pkgfs = PkgfsRamdisk::builder().start().unwrap();
        let root = pkgfs.root_dir().unwrap();

        let package_merkle = install_test_package(&root);
        assert_eq!(list_dir(&root.sub_dir("versions").unwrap()), vec![package_merkle.to_string()]);

        drop(root);
        pkgfs.stop().await.unwrap();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn restart_forgets_ephemeral_packages() {
        let mut pkgfs = PkgfsRamdisk::start().unwrap();
        let package_merkle = install_test_package(&pkgfs.root_dir().unwrap());
        pkgfs = pkgfs.restart().unwrap();

        // after a restart, there are no known packages (since this pkgfs did not have a system
        // image containing a static or cache index).
        let root = pkgfs.root_dir().unwrap();
        assert_eq!(list_dir(&root.sub_dir("versions").unwrap()), Vec::<String>::new());

        // but the backing blobfs is the same, so attempting to create the package will re-import
        // it without having to write any data.
        assert_matches!(
            root.new_file(&format!("install/pkg/{}", package_merkle), 0600),
            Err(e) if e.kind() == std::io::ErrorKind::AlreadyExists
        );
        assert_eq!(list_dir(&root.sub_dir("versions").unwrap()), vec![package_merkle.to_string()]);

        drop(root);
        pkgfs.stop().await.unwrap();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn start_with_system_image_exposes_base_package() {
        let (pkg_meta_far, pkg_blobs) = make_test_package();
        let package_merkle = fuchsia_merkle::MerkleTree::from_reader(pkg_meta_far.as_slice())
            .unwrap()
            .root()
            .to_string();
        let (system_image_far, system_image_blobs) =
            make_system_image_package(format!("pkgfs-ramdisk-tests/0={}", package_merkle));

        let blobfs = BlobfsRamdisk::start().unwrap();
        let blobfs_root = blobfs.root_dir().unwrap();
        write_blob(&blobfs_root, ".", pkg_meta_far.as_slice());
        for blob in pkg_blobs {
            write_blob(&blobfs_root, ".", blob.as_slice());
        }
        let system_image_merkle_root = write_blob(&blobfs_root, ".", system_image_far.as_slice());
        for blob in system_image_blobs {
            write_blob(&blobfs_root, ".", blob.as_slice());
        }
        drop(blobfs_root);

        let mut pkgfs = PkgfsRamdisk::builder()
            .blobfs(blobfs)
            .system_image_merkle(&system_image_merkle_root)
            .start()
            .unwrap();

        let expected_active_merkles =
            hashset![system_image_merkle_root.to_string(), package_merkle.to_string()];

        // both packages appear in versions
        assert_eq!(
            list_dir(&pkgfs.root_dir().unwrap().sub_dir("versions").unwrap())
                .into_iter()
                .collect::<HashSet<_>>(),
            expected_active_merkles
        );

        // even after a restart
        pkgfs = pkgfs.restart().unwrap();
        assert_eq!(
            list_dir(&pkgfs.root_dir().unwrap().sub_dir("versions").unwrap())
                .into_iter()
                .collect::<HashSet<_>>(),
            expected_active_merkles
        );

        pkgfs.stop().await.unwrap();
    }

    /// Makes a test package, producing a tuple of the meta far bytes and a vec of content blob
    /// bytes.
    fn make_test_package() -> (Vec<u8>, Vec<Vec<u8>>) {
        let mut meta_far = vec![];
        fuchsia_pkg::build(
            &CreationManifest::from_external_and_far_contents(
                btreemap! {
                    "test/pkgfs-ramdisk-lib-test".to_string() =>
                        "/pkg/test/pkgfs-ramdisk-lib-test".to_string(),
                },
                btreemap! {
                    "meta/pkgfs-ramdisk-lib-test.cmx".to_string() =>
                        "/pkg/meta/pkgfs-ramdisk-lib-test.cmx".to_string(),
                },
            )
            .unwrap(),
            &MetaPackage::from_name_and_variant("pkgfs-ramdisk-tests", "0").unwrap(),
            &mut meta_far,
        )
        .unwrap();

        (meta_far, vec![fs::read("/pkg/test/pkgfs-ramdisk-lib-test").unwrap()])
    }

    /// Makes a test system_image package containing the given literal static_index contents,
    /// producing a tuple of the meta far bytes and a vec of content blob bytes.
    fn make_system_image_package(static_index: String) -> (Vec<u8>, Vec<Vec<u8>>) {
        let tmp = tempfile::tempdir().unwrap();
        std::fs::write(tmp.path().join("static_index"), static_index.as_bytes()).unwrap();

        let mut meta_far = vec![];
        fuchsia_pkg::build(
            &CreationManifest::from_external_and_far_contents(
                btreemap! {
                    "data/static_packages".to_string() =>
                        tmp.path().join("static_index").into_os_string().into_string().unwrap(),
                },
                btreemap! {},
            )
            .unwrap(),
            &MetaPackage::from_name_and_variant("system_image", "0").unwrap(),
            &mut meta_far,
        )
        .unwrap();

        (meta_far, vec![static_index.into_bytes()])
    }

    /// Installs the test package (see make_test_package) to a pkgfs instance.
    fn install_test_package(pkgfs: &openat::Dir) -> Hash {
        let (meta_far, blobs) = make_test_package();

        let meta_far_merkle = write_blob(&pkgfs, "install/pkg", meta_far.as_slice());
        for blob in blobs {
            write_blob(&pkgfs, "install/blob", blob.as_slice());
        }

        meta_far_merkle
    }

    /// Writes a blob in the given directory and path, returning the computed merkle root of the blob.
    fn write_blob(dir: &openat::Dir, subdir: impl AsRef<Path>, mut payload: impl Read) -> Hash {
        let mut buf = vec![];
        payload.read_to_end(&mut buf).unwrap();
        let merkle = fuchsia_merkle::MerkleTree::from_reader(buf.as_slice()).unwrap().root();

        let mut f = dir.new_file(&subdir.as_ref().join(&merkle.to_string()), 0600).unwrap();
        f.set_len(buf.len() as u64).unwrap();
        f.write_all(&buf).unwrap();

        merkle
    }

    /// Returns an unsorted list of nodes in the given dir.
    fn list_dir(dir: &openat::Dir) -> Vec<String> {
        dir.list_dir(".")
            .unwrap()
            .map(|entry| entry.unwrap().file_name().to_owned().into_string().unwrap())
            .collect()
    }
}
