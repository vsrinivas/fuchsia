// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Test utilities for starting a blobfs server.

use {
    crate::{as_dir, ProcessKillGuard},
    failure::{bail, format_err, Error, ResultExt},
    fdio::{SpawnAction, SpawnOptions},
    fidl::endpoints::{ClientEnd, ServerEnd},
    fidl_fuchsia_io::{DirectoryAdminMarker, DirectoryAdminProxy, DirectoryMarker, NodeProxy},
    fuchsia_async as fasync,
    fuchsia_merkle::Hash,
    fuchsia_runtime::{HandleInfo, HandleType},
    fuchsia_zircon as zx,
    openat::Dir,
    ramdevice_client::RamdiskClient,
    std::{collections::BTreeSet, ffi::CString},
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

/// A running BlobFs server
pub struct TestBlobFs {
    backing_ramdisk: TestRamDisk,
    process: ProcessKillGuard,
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

        Ok(Self { backing_ramdisk: test_ramdisk, process: process.into(), proxy })
    }

    pub(crate) fn root_dir_handle(&self) -> Result<ClientEnd<DirectoryMarker>, Error> {
        let (root_clone, server_end) = zx::Channel::create()?;
        self.proxy.clone(fidl_fuchsia_io::CLONE_FLAG_SAME_RIGHTS, server_end.into())?;
        Ok(root_clone.into())
    }

    pub(crate) async fn stop(self) -> Result<(), Error> {
        zx::Status::ok(self.proxy.unmount().await.context("sending blobfs unmount")?)
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

    /// Opens the root of this blobfs instance as a directory.
    pub fn as_dir(&self) -> Result<Dir, Error> {
        Ok(as_dir(self.root_dir_handle()?))
    }

    /// Returns a sorted list of all blobs present in this blobfs instance.
    pub fn list_blobs(&self) -> Result<BTreeSet<Hash>, Error> {
        self.as_dir()?
            .list_dir(".")?
            .map(|entry| {
                Ok(entry?
                    .file_name()
                    .to_str()
                    .ok_or_else(|| format_err!("expected valid utf-8"))?
                    .parse()?)
            })
            .collect()
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::as_dir,
        fidl_fuchsia_io::{DirectoryProxy, FileEvent, FileMarker, FileObject, FileProxy, NodeInfo},
        fuchsia_zircon::Status,
        futures::StreamExt,
        maplit::btreeset,
        matches::assert_matches,
        std::io::Write,
    };

    fn ls_simple(d: openat::DirIter) -> Result<Vec<String>, Error> {
        Ok(d.map(|i| i.map(|entry| entry.file_name().to_string_lossy().into()))
            .collect::<Result<Vec<_>, _>>()?)
    }

    impl TestBlobFs {
        fn root_dir_proxy(&self) -> DirectoryProxy {
            let root_dir_handle = self.root_dir_handle().expect("root_dir_handle");
            DirectoryProxy::new(
                fasync::Channel::from_channel(root_dir_handle.into_channel())
                    .expect("async channel from zircon channel"),
            )
        }
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
        assert_eq!(
            blobfs_server.list_blobs().expect("list blobs"),
            btreeset!["e5892a9b652ede2e19460a9103fd9cb3417f782a8d29f6c93ec0c31170a94af3"
                .parse()
                .unwrap()],
        );

        blobfs_server.stop().await?;

        Ok(())
    }

    static BLOB_MERKLE: &str = "e5892a9b652ede2e19460a9103fd9cb3417f782a8d29f6c93ec0c31170a94af3";
    static BLOB_CONTENTS: &[u8] = b"Hello world!\n";

    async fn open_blob(
        blobfs: &DirectoryProxy,
        merkle: &str,
        mut flags: u32,
    ) -> Result<(FileProxy, zx::Event), zx::Status> {
        let (file, server_end) = fidl::endpoints::create_proxy::<FileMarker>().unwrap();
        let server_end = ServerEnd::new(server_end.into_channel());

        flags = flags | fidl_fuchsia_io::OPEN_FLAG_DESCRIBE;
        blobfs.open(flags, 0, merkle, server_end).expect("open blob");

        let mut events = file.take_event_stream();
        let FileEvent::OnOpen_ { s: status, info } = events
            .next()
            .await
            .expect("FileEvent stream to be non-empty")
            .expect("FileEvent stream not to FIDL error");
        Status::ok(status)?;

        let event = match *info.expect("FileEvent to have NodeInfo") {
            NodeInfo::File(FileObject { event: Some(event) }) => event,
            other => {
                panic!("NodeInfo from FileEventStream to be File variant with event: {:?}", other)
            }
        };
        Ok((file, event))
    }

    async fn write_blob(blob: &FileProxy, bytes: &[u8]) -> Result<(), Error> {
        let mut bytes_iter = bytes.to_owned().into_iter();
        let (status, n) = blob.write(&mut bytes_iter).await?;
        Status::ok(status)?;
        assert_eq!(n, bytes.len() as u64);
        Ok(())
    }

    async fn verify_blob(blob: &FileProxy, expected_bytes: &[u8]) -> Result<(), Error> {
        let (status, actual_bytes) = blob.read_at(expected_bytes.len() as u64 + 1, 0).await?;
        Status::ok(status)?;
        assert_eq!(actual_bytes, expected_bytes);
        Ok(())
    }

    async fn create_blob(
        blobfs: &DirectoryProxy,
        merkle: &str,
        contents: &[u8],
    ) -> Result<(), Error> {
        let (blob, _) = open_blob(
            &blobfs,
            merkle,
            fidl_fuchsia_io::OPEN_FLAG_CREATE | fidl_fuchsia_io::OPEN_RIGHT_WRITABLE,
        )
        .await?;
        Status::ok(blob.truncate(contents.len() as u64).await?)?;
        write_blob(&blob, contents).await?;
        Status::ok(blob.close().await?)?;

        let (blob, _) = open_blob(&blobfs, merkle, fidl_fuchsia_io::OPEN_RIGHT_READABLE).await?;
        verify_blob(&blob, contents).await
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_open_for_create_drop_create() -> Result<(), Error> {
        let blobfs_server = TestBlobFs::start()?;
        let root_dir = blobfs_server.root_dir_proxy();

        let (blob, _) = open_blob(
            &root_dir,
            BLOB_MERKLE,
            fidl_fuchsia_io::OPEN_FLAG_CREATE | fidl_fuchsia_io::OPEN_RIGHT_WRITABLE,
        )
        .await?;
        drop(blob);

        create_blob(&root_dir, BLOB_MERKLE, BLOB_CONTENTS).await?;

        blobfs_server.stop().await
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_open_truncate_drop_create() -> Result<(), Error> {
        let blobfs_server = TestBlobFs::start()?;
        let root_dir = blobfs_server.root_dir_proxy();

        let (blob, _) = open_blob(
            &root_dir,
            BLOB_MERKLE,
            fidl_fuchsia_io::OPEN_FLAG_CREATE | fidl_fuchsia_io::OPEN_RIGHT_WRITABLE,
        )
        .await?;
        Status::ok(blob.truncate(BLOB_CONTENTS.len() as u64).await?)?;
        drop(blob);

        create_blob(&root_dir, BLOB_MERKLE, BLOB_CONTENTS).await?;

        blobfs_server.stop().await
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_open_partial_write_drop_create() -> Result<(), Error> {
        let blobfs_server = TestBlobFs::start()?;
        let root_dir = blobfs_server.root_dir_proxy();

        let (blob, _) = open_blob(
            &root_dir,
            BLOB_MERKLE,
            fidl_fuchsia_io::OPEN_FLAG_CREATE | fidl_fuchsia_io::OPEN_RIGHT_WRITABLE,
        )
        .await?;
        Status::ok(blob.truncate(BLOB_CONTENTS.len() as u64).await?)?;
        write_blob(&blob, &BLOB_CONTENTS[0..1]).await?;
        drop(blob);

        create_blob(&root_dir, BLOB_MERKLE, BLOB_CONTENTS).await?;

        blobfs_server.stop().await
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_open_partial_write_close_create() -> Result<(), Error> {
        let blobfs_server = TestBlobFs::start()?;
        let root_dir = blobfs_server.root_dir_proxy();

        let (blob, _) = open_blob(
            &root_dir,
            BLOB_MERKLE,
            fidl_fuchsia_io::OPEN_FLAG_CREATE | fidl_fuchsia_io::OPEN_RIGHT_WRITABLE,
        )
        .await?;
        Status::ok(blob.truncate(BLOB_CONTENTS.len() as u64).await?)?;
        write_blob(&blob, &BLOB_CONTENTS[0..1]).await?;
        Status::ok(blob.close().await?)?;

        create_blob(&root_dir, BLOB_MERKLE, BLOB_CONTENTS).await?;

        blobfs_server.stop().await
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_open_truncate_open_for_create_fails() -> Result<(), Error> {
        let blobfs_server = TestBlobFs::start()?;
        let root_dir = blobfs_server.root_dir_proxy();

        let (blob, _) = open_blob(
            &root_dir,
            BLOB_MERKLE,
            fidl_fuchsia_io::OPEN_FLAG_CREATE | fidl_fuchsia_io::OPEN_RIGHT_WRITABLE,
        )
        .await?;
        Status::ok(blob.truncate(BLOB_CONTENTS.len() as u64).await?)?;

        let res = open_blob(
            &root_dir,
            BLOB_MERKLE,
            fidl_fuchsia_io::OPEN_FLAG_CREATE | fidl_fuchsia_io::OPEN_RIGHT_WRITABLE,
        )
        .await;

        assert_matches!(res, Err(zx::Status::ACCESS_DENIED));

        blobfs_server.stop().await
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_open_open_truncate_truncate_fails() -> Result<(), Error> {
        let blobfs_server = TestBlobFs::start()?;
        let root_dir = blobfs_server.root_dir_proxy();

        let (blob0, _) = open_blob(
            &root_dir,
            BLOB_MERKLE,
            fidl_fuchsia_io::OPEN_FLAG_CREATE | fidl_fuchsia_io::OPEN_RIGHT_WRITABLE,
        )
        .await?;
        let (blob1, _) = open_blob(
            &root_dir,
            BLOB_MERKLE,
            fidl_fuchsia_io::OPEN_FLAG_CREATE | fidl_fuchsia_io::OPEN_RIGHT_WRITABLE,
        )
        .await?;
        Status::ok(blob0.truncate(BLOB_CONTENTS.len() as u64).await?)?;

        let res = Status::ok(blob1.truncate(BLOB_CONTENTS.len() as u64).await?);

        assert_matches!(res, Err(zx::Status::BAD_STATE));

        blobfs_server.stop().await
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_open_truncate_open_read_fails() -> Result<(), Error> {
        let blobfs_server = TestBlobFs::start()?;
        let root_dir = blobfs_server.root_dir_proxy();

        let (blob0, _) = open_blob(
            &root_dir,
            BLOB_MERKLE,
            fidl_fuchsia_io::OPEN_FLAG_CREATE | fidl_fuchsia_io::OPEN_RIGHT_WRITABLE,
        )
        .await?;
        Status::ok(blob0.truncate(BLOB_CONTENTS.len() as u64).await?)?;
        let (blob1, _) =
            open_blob(&root_dir, BLOB_MERKLE, fidl_fuchsia_io::OPEN_RIGHT_READABLE).await?;

        let (status, _actual_bytes) = blob1.read_at(1, 0).await?;

        assert_eq!(Status::from_raw(status), zx::Status::BAD_STATE);

        blobfs_server.stop().await
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_open_for_create_wait_for_signal() -> Result<(), Error> {
        let blobfs_server = TestBlobFs::start()?;
        let root_dir = blobfs_server.root_dir_proxy();

        let (blob0, _) = open_blob(
            &root_dir,
            BLOB_MERKLE,
            fidl_fuchsia_io::OPEN_FLAG_CREATE | fidl_fuchsia_io::OPEN_RIGHT_WRITABLE,
        )
        .await?;
        let (blob1, event) =
            open_blob(&root_dir, BLOB_MERKLE, fidl_fuchsia_io::OPEN_RIGHT_READABLE).await?;
        Status::ok(blob0.truncate(BLOB_CONTENTS.len() as u64).await?)?;
        assert_matches!(
            event.wait_handle(zx::Signals::all(), zx::Time::after(zx::Duration::from_seconds(0))),
            Err(zx::Status::TIMED_OUT)
        );
        write_blob(&blob0, &BLOB_CONTENTS[..]).await?;

        assert_eq!(
            event
                .wait_handle(zx::Signals::all(), zx::Time::after(zx::Duration::from_seconds(0)))?,
            zx::Signals::USER_0
        );
        verify_blob(&blob1, BLOB_CONTENTS).await?;

        blobfs_server.stop().await
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_open_truncate_wait_for_signal() -> Result<(), Error> {
        let blobfs_server = TestBlobFs::start()?;
        let root_dir = blobfs_server.root_dir_proxy();

        let (blob0, _) = open_blob(
            &root_dir,
            BLOB_MERKLE,
            fidl_fuchsia_io::OPEN_FLAG_CREATE | fidl_fuchsia_io::OPEN_RIGHT_WRITABLE,
        )
        .await?;
        Status::ok(blob0.truncate(BLOB_CONTENTS.len() as u64).await?)?;
        let (blob1, event) =
            open_blob(&root_dir, BLOB_MERKLE, fidl_fuchsia_io::OPEN_RIGHT_READABLE).await?;
        assert_matches!(
            event.wait_handle(zx::Signals::all(), zx::Time::after(zx::Duration::from_seconds(0))),
            Err(zx::Status::TIMED_OUT)
        );
        write_blob(&blob0, &BLOB_CONTENTS[..]).await?;

        assert_eq!(
            event
                .wait_handle(zx::Signals::all(), zx::Time::after(zx::Duration::from_seconds(0)))?,
            zx::Signals::USER_0
        );
        verify_blob(&blob1, BLOB_CONTENTS).await?;

        blobfs_server.stop().await
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_open_missing_fails() -> Result<(), Error> {
        let blobfs_server = TestBlobFs::start()?;
        let root_dir = blobfs_server.root_dir_proxy();

        let res = open_blob(&root_dir, BLOB_MERKLE, fidl_fuchsia_io::OPEN_RIGHT_READABLE).await;

        assert_matches!(res, Err(zx::Status::NOT_FOUND));

        blobfs_server.stop().await
    }
}
