// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Test utilities for starting a pkgfs server.

use {
    anyhow::{Context as _, Error},
    fdio::{SpawnAction, SpawnOptions},
    fidl::endpoints::ClientEnd,
    fidl_fuchsia_io::{DirectoryMarker, DirectoryProxy},
    fuchsia_runtime::{HandleInfo, HandleType},
    fuchsia_zircon::{AsHandleRef, Task},
    std::ffi::{CStr, CString},
};

pub use blobfs_ramdisk::BlobfsRamdisk;

/// A helper to construct PkgfsRamdisk instances.
pub struct PkgfsRamdiskBuilder {
    blobfs: Option<BlobfsRamdisk>,
    system_image_merkle: Option<String>,
}

impl PkgfsRamdiskBuilder {
    /// Creates a new PkgfsRamdiskBuilder with no configured `blobfs` instance or `system_image_merkle`.
    fn new() -> Self {
        Self { blobfs: None, system_image_merkle: None }
    }

    /// Use the given blobfs when constructing the PkgfsRamdisk.
    pub fn blobfs(mut self, blobfs: BlobfsRamdisk) -> Self {
        self.blobfs = Some(blobfs);
        self
    }

    /// Use the given system_image_merkle when constructing the PkgfsRamdisk.
    pub fn system_image_merkle(mut self, system_image_merkle: impl Into<String>) -> Self {
        self.system_image_merkle = Some(system_image_merkle.into());
        self
    }

    /// Attempt to start the PkgfsRamdisk, consumign this builder.
    pub fn start(self) -> Result<PkgfsRamdisk, Error> {
        let blobfs = if let Some(blobfs) = self.blobfs { blobfs } else { BlobfsRamdisk::start()? };

        PkgfsRamdisk::start_with_blobfs(blobfs, self.system_image_merkle)
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
    process: KillOnDrop<fuchsia_zircon::Process>,
    system_image_merkle: Option<String>,
}

impl PkgfsRamdisk {
    /// Creates a new PkgfsRamdiskBuilder with no configured `blobfs` instance or `system_image_merkle`.
    pub fn builder() -> PkgfsRamdiskBuilder {
        PkgfsRamdiskBuilder::new()
    }

    /// Start a pkgfs server.
    pub fn start() -> Result<Self, Error> {
        Self::builder().start()
    }

    /// Starts a package server backed by the provided blobfs.
    ///
    /// If system_image_merkle is Some, uses that as the starting system_image package.
    pub fn start_with_blobfs(
        blobfs: BlobfsRamdisk,
        system_image_merkle: Option<impl Into<String>>,
    ) -> Result<Self, Error> {
        let system_image_merkle = system_image_merkle.map(|m| m.into());

        let pkgfs_root_handle_info = HandleInfo::new(HandleType::User0, 0);
        let (proxy, pkgfs_root_server_end) = fidl::endpoints::create_proxy::<DirectoryMarker>()?;

        let pkgsvr_bin = CString::new("/pkg/bin/pkgsvr").unwrap();
        let system_image_flag =
            system_image_merkle.as_ref().map(|s| CString::new(s.clone()).unwrap());

        let mut argv: Vec<&CStr> = vec![&pkgsvr_bin];
        if let Some(system_image_flag) = system_image_flag.as_ref() {
            argv.push(system_image_flag)
        }

        let process = fdio::spawn_etc(
            &fuchsia_runtime::job_default(),
            SpawnOptions::CLONE_ALL,
            &pkgsvr_bin,
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

        Ok(PkgfsRamdisk { blobfs, proxy, process: process.into(), system_image_merkle })
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
        use std::os::unix::io::{FromRawFd, IntoRawFd};

        let f = fdio::create_fd(self.root_dir_handle()?.into()).unwrap();

        let dir = {
            let fd = f.into_raw_fd();
            // Convert our raw file descriptor into an openat::Dir. This is enclosed in an unsafe
            // block because a RawFd may or may not be owned, and it is only safe to construct an
            // owned handle from an owned RawFd, which this does.
            unsafe { openat::Dir::from_raw_fd(fd) }
        };
        Ok(dir)
    }

    /// Kills the pkgfs process and waits for it to terminate.
    fn kill_pkgfs(&self) -> Result<(), Error> {
        self.process.kill().context("killing pkgfs")?;

        self.process
            .wait_handle(
                fuchsia_zircon::Signals::PROCESS_TERMINATED,
                fuchsia_zircon::Time::after(fuchsia_zircon::Duration::from_seconds(30)),
            )
            .context("waiting for 'pkgfs' to terminate")?;

        Ok(())
    }

    /// Restarts pkgfs with the same backing blobfs.
    pub fn restart(self) -> Result<Self, Error> {
        self.kill_pkgfs()?;
        drop(self.proxy);
        Self::start_with_blobfs(self.blobfs, self.system_image_merkle)
    }

    /// Shuts down the pkgfs server and all the backing infrastructure.
    ///
    /// This also shuts down blobfs and deletes the backing ramdisk.
    pub async fn stop(self) -> Result<(), Error> {
        self.kill_pkgfs()?;
        self.blobfs.stop().await
    }
}

/// An owned zircon job or process that is silently killed when dropped.
struct KillOnDrop<T: Task> {
    task: T,
}

impl<T: Task> Drop for KillOnDrop<T> {
    fn drop(&mut self) {
        let _ = self.task.kill();
    }
}

impl<T: Task> std::ops::Deref for KillOnDrop<T> {
    type Target = T;
    fn deref(&self) -> &Self::Target {
        &self.task
    }
}

impl<T: Task> From<T> for KillOnDrop<T> {
    fn from(task: T) -> Self {
        Self { task }
    }
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
        assert_eq!(list_dir(&root.sub_dir("versions").unwrap()), vec![package_merkle]);

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
        assert_eq!(list_dir(&root.sub_dir("versions").unwrap()), vec![package_merkle]);

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
            .system_image_merkle(system_image_merkle_root.clone())
            .start()
            .unwrap();

        let expected_active_merkles = hashset![system_image_merkle_root, package_merkle];

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
    fn install_test_package(pkgfs: &openat::Dir) -> String {
        let (meta_far, blobs) = make_test_package();

        let meta_far_merkle = write_blob(&pkgfs, "install/pkg", meta_far.as_slice());
        for blob in blobs {
            write_blob(&pkgfs, "install/blob", blob.as_slice());
        }

        meta_far_merkle
    }

    /// Writes a blob in the given directory and path, returning the computed merkle root of the blob.
    fn write_blob(dir: &openat::Dir, subdir: impl AsRef<Path>, mut payload: impl Read) -> String {
        let mut buf = vec![];
        payload.read_to_end(&mut buf).unwrap();
        let merkle =
            fuchsia_merkle::MerkleTree::from_reader(buf.as_slice()).unwrap().root().to_string();

        let mut f = dir.new_file(&subdir.as_ref().join(&merkle), 0600).unwrap();
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
