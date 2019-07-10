// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    failure::{bail, Error, ResultExt},
    fdio::{SpawnAction, SpawnOptions},
    fidl::endpoints::{ClientEnd, ServerEnd},
    fidl_fuchsia_io::{DirectoryAdminMarker, DirectoryAdminProxy, DirectoryMarker, NodeProxy},
    fuchsia_async as fasync,
    fuchsia_runtime::{HandleInfo, HandleType},
    fuchsia_zircon as zx,
    ramdevice_client::RamdiskClient,
    std::ffi::CString,
    zx::prelude::*,
};

struct TestRamDisk {
    proxy: NodeProxy,
    client: RamdiskClient,
}

impl TestRamDisk {
    fn start() -> Result<Self, Error> {
        let client = RamdiskClient::create(512, 1 << 20)?;
        let proxy = NodeProxy::new(fasync::Channel::from_channel(client.open()?)?);
        Ok(TestRamDisk { proxy, client })
    }

    fn clone_channel(&self) -> Result<zx::Channel, Error> {
        let (result, server_end) = zx::Channel::create()?;
        self.proxy.clone(fidl_fuchsia_io::CLONE_FLAG_SAME_RIGHTS, ServerEnd::new(server_end))?;
        Ok(result)
    }

    fn stop(self) -> Result<(), Error> {
        Ok(self.client.destroy()?)
    }
}

fn mkblobfs_block(block_device: zx::Handle) -> Result<(), Error> {
    let block_device_handle_id = HandleInfo::new(HandleType::User0, 1);
    let p = fdio::spawn_etc(
        &fuchsia_runtime::job_default(),
        SpawnOptions::CLONE_ALL,
        &CString::new("/pkg/bin/blobfs").unwrap(),
        &[&CString::new("blobfs").unwrap(), &CString::new("mkfs").unwrap()],
        None,
        &mut [SpawnAction::add_handle(block_device_handle_id, block_device)],
    )
    .map_err(|(status, _)| status)
    .context("spawning 'blobfs mkfs'")?;
    p.wait_handle(zx::Signals::PROCESS_TERMINATED, zx::Time::after(zx::Duration::from_seconds(30)))
        .context("waiting for 'blobfs mkfs' to terminate")?;
    let ret = p.info().context("getting 'blobfs mkfs' process info")?.return_code;
    if ret != 0 {
        bail!("'blobfs mkfs' returned nonzero exit code {}", ret)
    }
    Ok(())
}

fn mkblobfs(ramdisk: &TestRamDisk) -> Result<(), Error> {
    mkblobfs_block(ramdisk.clone_channel().context("cloning ramdisk channel")?.into())
}

pub(crate) struct TestBlobFs {
    backing_ramdisk: TestRamDisk,
    process: zx::Process,
    proxy: DirectoryAdminProxy,
}

impl TestBlobFs {
    pub(crate) fn start() -> Result<Self, Error> {
        let test_ramdisk = TestRamDisk::start().context("creating backing ramdisk for blobfs")?;
        mkblobfs(&test_ramdisk)?;

        let block_device_handle_id = HandleInfo::new(HandleType::User0, 1);
        let fs_root_handle_id = HandleInfo::new(HandleType::User0, 0);

        let block_handle = test_ramdisk.clone_channel().context("cloning ramdisk channel")?;

        let (blobfs_root, blobfs_server_end) =
            fidl::endpoints::create_endpoints::<DirectoryAdminMarker>()?;
        let process = fdio::spawn_etc(
            &fuchsia_runtime::job_default(),
            SpawnOptions::CLONE_ALL,
            &CString::new("/pkg/bin/blobfs").unwrap(),
            &[&CString::new("blobfs").unwrap(), &CString::new("mount").unwrap()],
            None,
            &mut [
                SpawnAction::add_handle(block_device_handle_id, block_handle.into()),
                SpawnAction::add_handle(fs_root_handle_id, blobfs_server_end.into()),
            ],
        )
        .map_err(|(status, _)| status)
        .context("spawning 'blobfs mount'")?;
        let proxy = blobfs_root.into_proxy()?;

        Ok(Self { backing_ramdisk: test_ramdisk, process, proxy })
    }

    pub(crate) fn root_dir_handle(&self) -> Result<ClientEnd<DirectoryMarker>, Error> {
        let (root_clone, server_end) = zx::Channel::create()?;
        self.proxy.clone(fidl_fuchsia_io::CLONE_FLAG_SAME_RIGHTS, server_end.into())?;
        Ok(root_clone.into())
    }

    pub(crate) async fn stop(self) -> Result<(), Error> {
        zx::Status::ok(await!(self.proxy.unmount()).context("sending blobfs unmount")?)
            .context("unmounting blobfs")?;

        self.process
            .wait_handle(
                zx::Signals::PROCESS_TERMINATED,
                zx::Time::after(zx::Duration::from_seconds(30)),
            )
            .context("waiting for 'blobfs mount' to exit")?;
        let ret = self.process.info().context("getting 'blobfs mount' process info")?.return_code;
        if ret != 0 {
            bail!("'blobfs mount' returned nonzero exit code {}", ret)
        }

        self.backing_ramdisk.stop()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use {crate::as_dir, std::io::Write};

    fn ls_simple(d: openat::DirIter) -> Result<Vec<String>, Error> {
        Ok(d.map(|i| i.map(|entry| entry.file_name().to_string_lossy().into()))
            .collect::<Result<Vec<_>, _>>()?)
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_blobfs() -> Result<(), Error> {
        let blobfs_server = TestBlobFs::start()?;

        let blobfs_root =
            blobfs_server.root_dir_handle().context("getting blobfs root dir handle")?;
        let d = as_dir(blobfs_root);
        assert_eq!(
            ls_simple(d.list_dir(".").expect("list dir")).expect("list dir contents"),
            Vec::<String>::new(),
        );

        let mut f = d
            .write_file("e5892a9b652ede2e19460a9103fd9cb3417f782a8d29f6c93ec0c31170a94af3", 0)
            .expect("open file 1");
        let contents = b"Hello world!\n";
        f.set_len(6 as u64).expect("truncate");

        f.write_all(b"Hello").unwrap_or_else(|e| eprintln!("write 1 error: {}", e));
        drop(f);

        assert_eq!(
            ls_simple(d.list_dir(".").expect("list dir")).expect("list dir contents"),
            Vec::<String>::new(),
        );

        let mut f = d
            .write_file("e5892a9b652ede2e19460a9103fd9cb3417f782a8d29f6c93ec0c31170a94af3", 0)
            .expect("open file 2");
        f.set_len(contents.len() as u64).expect("truncate");
        f.write_all(b"Hello ").expect("write file2.1");
        f.write_all(b"world!\n").expect("write file2.2");
        drop(f);

        assert_eq!(
            ls_simple(d.list_dir(".").expect("list dir")).expect("list dir contents"),
            vec!["e5892a9b652ede2e19460a9103fd9cb3417f782a8d29f6c93ec0c31170a94af3".to_string()],
        );

        await!(blobfs_server.stop())?;

        Ok(())
    }
}
