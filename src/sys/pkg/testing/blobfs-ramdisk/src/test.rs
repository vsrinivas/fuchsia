// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::*,
    assert_matches::assert_matches,
    fidl_fuchsia_io as fio,
    fuchsia_merkle::MerkleTree,
    fuchsia_zircon::Status,
    futures::StreamExt,
    maplit::btreeset,
    std::{io::Write, time::Duration},
};

// merkle root of b"Hello world!\n".
static BLOB_MERKLE: &str = "e5892a9b652ede2e19460a9103fd9cb3417f782a8d29f6c93ec0c31170a94af3";
static BLOB_CONTENTS: &[u8] = b"Hello world!\n";

fn ls_simple(d: openat::DirIter) -> Result<Vec<String>, Error> {
    Ok(d.map(|i| i.map(|entry| entry.file_name().to_string_lossy().into()))
        .collect::<Result<Vec<_>, _>>()?)
}

#[fuchsia_async::run_singlethreaded(test)]
async fn blobfs() -> Result<(), Error> {
    let blobfs_server = BlobfsRamdisk::start()?;

    let d = blobfs_server.root_dir().context("get root dir")?;
    assert_eq!(
        ls_simple(d.list_dir(".").expect("list dir")).expect("list dir contents"),
        Vec::<String>::new(),
    );

    let mut f = d.write_file(BLOB_MERKLE, 0).expect("open file 1");
    f.set_len(6_u64).expect("truncate");

    f.write_all(b"Hello").unwrap_or_else(|e| eprintln!("write 1 error: {}", e));
    drop(f);

    assert_eq!(
        ls_simple(d.list_dir(".").expect("list dir")).expect("list dir contents"),
        Vec::<String>::new(),
    );

    let mut f = d.write_file(BLOB_MERKLE, 0).expect("open file 2");
    f.set_len(BLOB_CONTENTS.len() as u64).expect("truncate");
    f.write_all(b"Hello ").expect("write file2.1");
    f.write_all(b"world!\n").expect("write file2.2");
    drop(f);

    assert_eq!(
        ls_simple(d.list_dir(".").expect("list dir")).expect("list dir contents"),
        vec![BLOB_MERKLE.to_string()],
    );
    assert_eq!(
        blobfs_server.list_blobs().expect("list blobs"),
        btreeset![BLOB_MERKLE.parse().unwrap()],
    );

    blobfs_server.stop().await?;

    Ok(())
}

async fn open_blob(
    blobfs: &fio::DirectoryProxy,
    merkle: &str,
    mut flags: fio::OpenFlags,
) -> Result<(fio::FileProxy, zx::Event), zx::Status> {
    let (file, server_end) = fidl::endpoints::create_proxy::<fio::FileMarker>().unwrap();
    let server_end = ServerEnd::new(server_end.into_channel());

    flags |= fio::OpenFlags::DESCRIBE;
    blobfs.open(flags, 0, merkle, server_end).expect("open blob");

    let mut events = file.take_event_stream();
    let event = match events
        .next()
        .await
        .expect("fio::FileEvent stream to be non-empty")
        .expect("fio::FileEvent stream not to FIDL error")
    {
        fio::FileEvent::OnOpen_ { s: status, info } => {
            Status::ok(status)?;
            match *info.expect("fio::FileEvent to have fio::NodeInfoDeprecated") {
                fio::NodeInfoDeprecated::File(fio::FileObject { event: Some(event), .. }) => event,
                other => panic!(
                    "fio::NodeInfoDeprecated from fio::FileEventStream to be File variant with event: {:?}",
                    other
                ),
            }
        }
        fio::FileEvent::OnRepresentation { payload } => match payload {
            fio::Representation::File(fio::FileInfo { observer: Some(event), .. }) => event,
            other => panic!(
                "ConnectionInfo from fio::FileEventStream to be File variant with event: {:?}",
                other
            ),
        },
    };
    Ok((file, event))
}

async fn write_blob(blob: &fio::FileProxy, bytes: &[u8]) -> Result<(), Error> {
    let n = blob.write(bytes).await?.map_err(zx::Status::from_raw)?;
    assert_eq!(n, bytes.len() as u64);
    Ok(())
}

/// Verify the contents of a blob, or return any non-ok zx status encountered along the way.
async fn verify_blob(blob: &fio::FileProxy, expected_bytes: &[u8]) -> Result<(), Status> {
    let actual_bytes = blob
        .read_at(expected_bytes.len() as u64 + 1, 0)
        .await
        .unwrap()
        .map_err(Status::from_raw)?;
    assert_eq!(actual_bytes, expected_bytes);
    Ok(())
}

async fn create_blob(
    blobfs: &fio::DirectoryProxy,
    merkle: &str,
    contents: &[u8],
) -> Result<(), Error> {
    let (blob, _) =
        open_blob(blobfs, merkle, fio::OpenFlags::CREATE | fio::OpenFlags::RIGHT_WRITABLE).await?;
    let () = blob.resize(contents.len() as u64).await?.map_err(Status::from_raw)?;
    write_blob(&blob, contents).await?;
    blob.close().await?.map_err(Status::from_raw)?;

    let (blob, _) = open_blob(blobfs, merkle, fio::OpenFlags::RIGHT_READABLE).await?;
    verify_blob(&blob, contents).await?;
    Ok(())
}

// Dropping a FileProxy synchronously closes the zircon channel, but it is not guaranteed
// that blobfs will respond to the channel closing before it responds to a request on a
// separate channel to open the same blob. This means a test case that:
// 1. opens writable + resizes on channel 0
// 2. drops channel 0
// 3. opens writable on channel 1
// can fail with ACCESS_DENIED in step 3, unless we wait.
async fn wait_for_blob_to_be_creatable(blobfs: &fio::DirectoryProxy, merkle: &str) {
    for _ in 0..50 {
        let res =
            open_blob(blobfs, merkle, fio::OpenFlags::CREATE | fio::OpenFlags::RIGHT_WRITABLE)
                .await;
        match res {
            Err(zx::Status::ACCESS_DENIED) => {
                fuchsia_async::Timer::new(Duration::from_millis(10)).await;
                continue;
            }
            Err(err) => {
                panic!("unexpected error waiting for blob to become writable: {:?}", err);
            }
            Ok((blob, _)) => {
                // Explicitly close the blob so that when this function returns the blob
                // is in the state (creatable + not openable for read). If we just drop
                // the FileProxy instead of closing, the blob will be openable for read until
                // blobfs asynchronously cleans up.
                blob.close().await.unwrap().map_err(Status::from_raw).unwrap();
                return;
            }
        }
    }
    panic!("timeout waiting for blob to become creatable");
}

#[fuchsia_async::run_singlethreaded(test)]
async fn open_for_create_create() -> Result<(), Error> {
    let blobfs_server = BlobfsRamdisk::start()?;
    let root_dir = blobfs_server.root_dir_proxy()?;

    let (_blob, _) =
        open_blob(&root_dir, BLOB_MERKLE, fio::OpenFlags::CREATE | fio::OpenFlags::RIGHT_WRITABLE)
            .await?;

    create_blob(&root_dir, BLOB_MERKLE, BLOB_CONTENTS).await?;

    blobfs_server.stop().await
}

#[fuchsia_async::run_singlethreaded(test)]
async fn open_resize_drop_create() -> Result<(), Error> {
    let blobfs_server = BlobfsRamdisk::start()?;
    let root_dir = blobfs_server.root_dir_proxy()?;

    let (blob, _) =
        open_blob(&root_dir, BLOB_MERKLE, fio::OpenFlags::CREATE | fio::OpenFlags::RIGHT_WRITABLE)
            .await?;
    let () = blob.resize(BLOB_CONTENTS.len() as u64).await?.map_err(Status::from_raw)?;
    drop(blob);
    wait_for_blob_to_be_creatable(&root_dir, BLOB_MERKLE).await;

    create_blob(&root_dir, BLOB_MERKLE, BLOB_CONTENTS).await?;

    blobfs_server.stop().await
}

#[fuchsia_async::run_singlethreaded(test)]
async fn open_partial_write_drop_create() -> Result<(), Error> {
    let blobfs_server = BlobfsRamdisk::start()?;
    let root_dir = blobfs_server.root_dir_proxy()?;

    let (blob, _) =
        open_blob(&root_dir, BLOB_MERKLE, fio::OpenFlags::CREATE | fio::OpenFlags::RIGHT_WRITABLE)
            .await?;
    let () = blob.resize(BLOB_CONTENTS.len() as u64).await?.map_err(Status::from_raw)?;
    write_blob(&blob, &BLOB_CONTENTS[0..1]).await?;
    drop(blob);
    wait_for_blob_to_be_creatable(&root_dir, BLOB_MERKLE).await;

    create_blob(&root_dir, BLOB_MERKLE, BLOB_CONTENTS).await?;

    blobfs_server.stop().await
}

#[fuchsia_async::run_singlethreaded(test)]
async fn open_partial_write_close_create() -> Result<(), Error> {
    let blobfs_server = BlobfsRamdisk::start()?;
    let root_dir = blobfs_server.root_dir_proxy()?;

    let (blob, _) =
        open_blob(&root_dir, BLOB_MERKLE, fio::OpenFlags::CREATE | fio::OpenFlags::RIGHT_WRITABLE)
            .await?;
    let () = blob.resize(BLOB_CONTENTS.len() as u64).await?.map_err(Status::from_raw)?;
    write_blob(&blob, &BLOB_CONTENTS[0..1]).await?;
    blob.close().await?.map_err(Status::from_raw)?;

    create_blob(&root_dir, BLOB_MERKLE, BLOB_CONTENTS).await?;

    blobfs_server.stop().await
}

#[fuchsia_async::run_singlethreaded(test)]
async fn open_resize_open_for_create_fails() -> Result<(), Error> {
    let blobfs_server = BlobfsRamdisk::start()?;
    let root_dir = blobfs_server.root_dir_proxy()?;

    let (blob, _) =
        open_blob(&root_dir, BLOB_MERKLE, fio::OpenFlags::CREATE | fio::OpenFlags::RIGHT_WRITABLE)
            .await?;
    let () = blob.resize(BLOB_CONTENTS.len() as u64).await?.map_err(Status::from_raw)?;

    let res =
        open_blob(&root_dir, BLOB_MERKLE, fio::OpenFlags::CREATE | fio::OpenFlags::RIGHT_WRITABLE)
            .await;

    assert_matches!(res, Err(zx::Status::ACCESS_DENIED));

    blobfs_server.stop().await
}

#[fuchsia_async::run_singlethreaded(test)]
async fn open_open_resize_resize_fails() -> Result<(), Error> {
    let blobfs_server = BlobfsRamdisk::start()?;
    let root_dir = blobfs_server.root_dir_proxy()?;

    let (blob0, _) =
        open_blob(&root_dir, BLOB_MERKLE, fio::OpenFlags::CREATE | fio::OpenFlags::RIGHT_WRITABLE)
            .await?;
    let (blob1, _) =
        open_blob(&root_dir, BLOB_MERKLE, fio::OpenFlags::CREATE | fio::OpenFlags::RIGHT_WRITABLE)
            .await?;
    let () = blob0.resize(BLOB_CONTENTS.len() as u64).await?.map_err(Status::from_raw)?;
    let result = blob1.resize(BLOB_CONTENTS.len() as u64).await?.map_err(Status::from_raw);
    assert_matches!(result, Err(zx::Status::BAD_STATE));

    blobfs_server.stop().await
}

#[fuchsia_async::run_singlethreaded(test)]
async fn open0_open1_resize1_write1_succeeds() {
    let blobfs_server = BlobfsRamdisk::start().unwrap();
    let root_dir = blobfs_server.root_dir_proxy().unwrap();

    let (_blob0, _) =
        open_blob(&root_dir, BLOB_MERKLE, fio::OpenFlags::CREATE | fio::OpenFlags::RIGHT_WRITABLE)
            .await
            .unwrap();

    let (blob1, _) =
        open_blob(&root_dir, BLOB_MERKLE, fio::OpenFlags::CREATE | fio::OpenFlags::RIGHT_WRITABLE)
            .await
            .unwrap();
    let () = blob1.resize(BLOB_CONTENTS.len() as u64).await.unwrap().unwrap();
    let () = write_blob(&blob1, BLOB_CONTENTS).await.unwrap();

    let (blob2, _) =
        open_blob(&root_dir, BLOB_MERKLE, fio::OpenFlags::RIGHT_READABLE).await.unwrap();
    let () = verify_blob(&blob2, BLOB_CONTENTS).await.unwrap();

    let () = blobfs_server.stop().await.unwrap();
}

#[fuchsia_async::run_singlethreaded(test)]
async fn open0_open1_resize0_write0_succeeds() {
    let blobfs_server = BlobfsRamdisk::start().unwrap();
    let root_dir = blobfs_server.root_dir_proxy().unwrap();

    let (blob0, _) =
        open_blob(&root_dir, BLOB_MERKLE, fio::OpenFlags::CREATE | fio::OpenFlags::RIGHT_WRITABLE)
            .await
            .unwrap();

    let (_blob1, _) =
        open_blob(&root_dir, BLOB_MERKLE, fio::OpenFlags::CREATE | fio::OpenFlags::RIGHT_WRITABLE)
            .await
            .unwrap();

    let () = blob0.resize(BLOB_CONTENTS.len() as u64).await.unwrap().unwrap();
    let () = write_blob(&blob0, BLOB_CONTENTS).await.unwrap();

    let (blob2, _) =
        open_blob(&root_dir, BLOB_MERKLE, fio::OpenFlags::RIGHT_READABLE).await.unwrap();
    let () = verify_blob(&blob2, BLOB_CONTENTS).await.unwrap();

    let () = blobfs_server.stop().await.unwrap();
}

#[fuchsia_async::run_singlethreaded(test)]
async fn open_resize_open_read_fails() -> Result<(), Error> {
    let blobfs_server = BlobfsRamdisk::start()?;
    let root_dir = blobfs_server.root_dir_proxy()?;

    let (blob0, _) =
        open_blob(&root_dir, BLOB_MERKLE, fio::OpenFlags::CREATE | fio::OpenFlags::RIGHT_WRITABLE)
            .await?;
    let () = blob0.resize(BLOB_CONTENTS.len() as u64).await?.map_err(Status::from_raw)?;
    let (blob1, _) = open_blob(&root_dir, BLOB_MERKLE, fio::OpenFlags::RIGHT_READABLE).await?;

    let result = blob1.read_at(1, 0).await?.map_err(zx::Status::from_raw);

    assert_eq!(result, Err(zx::Status::BAD_STATE));

    blobfs_server.stop().await
}

#[fuchsia_async::run_singlethreaded(test)]
async fn open_for_create_wait_for_signal() -> Result<(), Error> {
    let blobfs_server = BlobfsRamdisk::start()?;
    let root_dir = blobfs_server.root_dir_proxy()?;

    let (blob0, _) =
        open_blob(&root_dir, BLOB_MERKLE, fio::OpenFlags::CREATE | fio::OpenFlags::RIGHT_WRITABLE)
            .await?;
    let (blob1, event) = open_blob(&root_dir, BLOB_MERKLE, fio::OpenFlags::RIGHT_READABLE).await?;
    let () = blob0.resize(BLOB_CONTENTS.len() as u64).await?.map_err(Status::from_raw)?;
    assert_matches!(
        event.wait_handle(zx::Signals::all(), zx::Time::after(zx::Duration::from_seconds(0))),
        Err(zx::Status::TIMED_OUT)
    );
    write_blob(&blob0, BLOB_CONTENTS).await?;

    assert_eq!(
        event.wait_handle(zx::Signals::all(), zx::Time::after(zx::Duration::from_seconds(0)))?,
        zx::Signals::USER_0
    );
    verify_blob(&blob1, BLOB_CONTENTS).await?;

    blobfs_server.stop().await
}

#[fuchsia_async::run_singlethreaded(test)]
async fn open_resize_wait_for_signal() -> Result<(), Error> {
    let blobfs_server = BlobfsRamdisk::start()?;
    let root_dir = blobfs_server.root_dir_proxy()?;

    let (blob0, _) =
        open_blob(&root_dir, BLOB_MERKLE, fio::OpenFlags::CREATE | fio::OpenFlags::RIGHT_WRITABLE)
            .await?;
    let () = blob0.resize(BLOB_CONTENTS.len() as u64).await?.map_err(Status::from_raw)?;
    let (blob1, event) = open_blob(&root_dir, BLOB_MERKLE, fio::OpenFlags::RIGHT_READABLE).await?;
    assert_matches!(
        event.wait_handle(zx::Signals::all(), zx::Time::after(zx::Duration::from_seconds(0))),
        Err(zx::Status::TIMED_OUT)
    );
    write_blob(&blob0, BLOB_CONTENTS).await?;

    assert_eq!(
        event.wait_handle(zx::Signals::all(), zx::Time::after(zx::Duration::from_seconds(0)))?,
        zx::Signals::USER_0
    );
    verify_blob(&blob1, BLOB_CONTENTS).await?;

    blobfs_server.stop().await
}

#[fuchsia_async::run_singlethreaded(test)]
async fn open_missing_fails() -> Result<(), Error> {
    let blobfs_server = BlobfsRamdisk::start()?;
    let root_dir = blobfs_server.root_dir_proxy()?;

    let res = open_blob(&root_dir, BLOB_MERKLE, fio::OpenFlags::RIGHT_READABLE).await;

    assert_matches!(res, Err(zx::Status::NOT_FOUND));

    blobfs_server.stop().await
}

struct TestBlob {
    merkle: Hash,
    contents: &'static [u8],
}

impl TestBlob {
    fn new(contents: &'static [u8]) -> Self {
        Self { merkle: MerkleTree::from_reader(contents).unwrap().root(), contents }
    }
}

impl BlobfsRamdisk {
    async fn write_blob(&self, blob: &TestBlob) {
        let proxy = self.root_dir_proxy().unwrap();
        create_blob(&proxy, &blob.merkle.to_string(), blob.contents).await.unwrap();
    }

    async fn verify_blob(&self, blob: &TestBlob) -> Result<(), Status> {
        let proxy = self.root_dir_proxy().unwrap();
        let (file, _) =
            open_blob(&proxy, &blob.merkle.to_string(), fio::OpenFlags::RIGHT_READABLE).await?;
        verify_blob(&file, blob.contents).await?;
        file.close().await.unwrap().map_err(Status::from_raw).unwrap();
        Ok(())
    }
}

#[fuchsia_async::run_singlethreaded(test)]
async fn corrupt_blob() {
    let blobfs = BlobfsRamdisk::start().unwrap();

    // write a few blobs and verify they are valid
    let first = TestBlob::new(b"corrupt me bro");
    let second = TestBlob::new(b"don't corrupt me bro");
    blobfs.write_blob(&first).await;
    assert_eq!(blobfs.verify_blob(&first).await, Ok(()));
    blobfs.write_blob(&second).await;
    assert_eq!(blobfs.verify_blob(&second).await, Ok(()));

    // unmount blobfs, corrupt the first blob, and restart blobfs
    let ramdisk = blobfs.unmount().await.unwrap();
    ramdisk.corrupt_blob(&first.merkle).await;
    let blobfs = BlobfsRamdisk::builder().ramdisk(ramdisk).start().unwrap();

    // verify the first blob is now corrupt and the second is still not
    assert_eq!(blobfs.verify_blob(&first).await, Err(Status::IO_DATA_INTEGRITY));
    assert_eq!(blobfs.verify_blob(&second).await, Ok(()));

    blobfs.stop().await.unwrap();
}

#[fuchsia_async::run_singlethreaded(test)]
async fn corrupt_blob_with_many_blobs() {
    let blobfs = BlobfsRamdisk::start().unwrap();

    const LIPSUM: &[u8] = b"Lorem ipsum dolor sit amet, consectetur adipiscing elit. Pellentesque ultrices pharetra ullamcorper. Duis vestibulum nulla eget porta lacinia. Nulla nunc nibh, dictum nec risus aliquam, accumsan aliquet tellus. Sed eget lectus sit amet odio ultrices maximus. Vestibulum eget mi ut eros porta consequat. Quisque a risus id purus cursus faucibus pulvinar et mi. Etiam vel scelerisque risus, eget ullamcorper quam.";

    let mut ls = BTreeSet::new();
    let mut valid = vec![];

    // write many blobs to force blobfs to utilize more than 1 block of inodes
    for i in 1..LIPSUM.len() {
        let blob = TestBlob::new(&LIPSUM[..i]);
        blobfs.write_blob(&blob).await;
        assert_eq!(blobfs.verify_blob(&blob).await, Ok(()));
        ls.insert(blob.merkle);
        valid.push(blob);
    }

    // write the blob to corrupt
    let corrupt = TestBlob::new(b"corrupt me bro");
    blobfs.write_blob(&corrupt).await;
    assert_eq!(blobfs.verify_blob(&corrupt).await, Ok(()));
    ls.insert(corrupt.merkle);

    // write many more blobs after the blob to corrupt
    for i in 1..(LIPSUM.len() - 1) {
        let blob = TestBlob::new(&LIPSUM[i..]);
        blobfs.write_blob(&blob).await;
        assert_eq!(blobfs.verify_blob(&blob).await, Ok(()));
        ls.insert(blob.merkle);
        valid.push(blob);
    }

    // unmount blobfs, corrupt the blob, and restart blobfs
    let ramdisk = blobfs.unmount().await.unwrap();
    ramdisk.corrupt_blob(&corrupt.merkle).await;
    let blobfs = BlobfsRamdisk::builder().ramdisk(ramdisk).start().unwrap();

    // verify all the blobs are still considered present
    assert_eq!(blobfs.list_blobs().unwrap(), ls);

    // verify the corrupt blob is now corrupt and all the rest aren't
    assert_eq!(blobfs.verify_blob(&corrupt).await, Err(Status::IO_DATA_INTEGRITY));
    for blob in valid {
        assert_eq!(blobfs.verify_blob(&blob).await, Ok(()));
    }

    blobfs.stop().await.unwrap();
}

#[fuchsia_async::run_singlethreaded(test)]
async fn corrupt_create_fails_on_last_byte_write() -> Result<(), Error> {
    let blobfs_server = BlobfsRamdisk::start()?;
    let root_dir = blobfs_server.root_dir_proxy()?;

    let (blob, _) =
        open_blob(&root_dir, BLOB_MERKLE, fio::OpenFlags::CREATE | fio::OpenFlags::RIGHT_WRITABLE)
            .await?;
    let () = blob.resize(BLOB_CONTENTS.len() as u64).await?.map_err(Status::from_raw)?;

    write_blob(&blob, &BLOB_CONTENTS[..BLOB_CONTENTS.len() - 1]).await?;
    let wrong_trailing_byte = !BLOB_CONTENTS.last().unwrap();
    assert_matches!(
        write_blob(&blob, &[wrong_trailing_byte]).await,
        Err(e) if *e.downcast_ref::<zx::Status>().unwrap() == zx::Status::IO_DATA_INTEGRITY
    );

    blobfs_server.stop().await
}
