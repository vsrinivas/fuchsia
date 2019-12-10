// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::*,
    fidl_fuchsia_io::{DirectoryProxy, FileEvent, FileMarker, FileObject, FileProxy, NodeInfo},
    fuchsia_zircon::Status,
    futures::StreamExt,
    maplit::btreeset,
    matches::assert_matches,
    std::io::Write,
};

// merkle root of b"Hello world!\n".
static BLOB_MERKLE: &str = "e5892a9b652ede2e19460a9103fd9cb3417f782a8d29f6c93ec0c31170a94af3";
static BLOB_CONTENTS: &[u8] = b"Hello world!\n";

static EMPTY_BLOB_MERKLE: &str = "15ec7bf0b50732b49f8228e07d24365338f9e3ab994b00af08e5a3bffe55fd8b";
static EMPTY_BLOB_CONTENTS: &[u8] = b"";

fn ls_simple(d: openat::DirIter) -> Result<Vec<String>, Error> {
    Ok(d.map(|i| i.map(|entry| entry.file_name().to_string_lossy().into()))
        .collect::<Result<Vec<_>, _>>()?)
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_blobfs() -> Result<(), Error> {
    let blobfs_server = BlobfsRamdisk::start()?;

    let d = blobfs_server.root_dir().context("get root dir")?;
    assert_eq!(
        ls_simple(d.list_dir(".").expect("list dir")).expect("list dir contents"),
        Vec::<String>::new(),
    );

    let mut f = d.write_file(BLOB_MERKLE, 0).expect("open file 1");
    f.set_len(6 as u64).expect("truncate");

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
        other => panic!("NodeInfo from FileEventStream to be File variant with event: {:?}", other),
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

async fn create_blob(blobfs: &DirectoryProxy, merkle: &str, contents: &[u8]) -> Result<(), Error> {
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

// Dropping a FileProxy synchronously closes the zircon channel, but it is not guaranteed
// that blobfs will respond to the channel closing before it responds to a request on a
// separate channel to open the same blob. This means a test case that:
// 1. opens writable + truncates on channel 0
// 2. drops channel 0
// 3. opens writable on channel 1
// can fail with ACCESS_DENIED in step 3, unless we wait.
async fn wait_for_blob_to_be_creatable(blobfs: &DirectoryProxy, merkle: &str) {
    for _ in 0..50 {
        let res = open_blob(
            &blobfs,
            merkle,
            fidl_fuchsia_io::OPEN_FLAG_CREATE | fidl_fuchsia_io::OPEN_RIGHT_WRITABLE,
        )
        .await;
        match res {
            Err(zx::Status::ACCESS_DENIED) => {
                fuchsia_async::Timer::new(fuchsia_async::Time::after(zx::Duration::from_millis(
                    10,
                )))
                .await;
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
                Status::ok(blob.close().await.unwrap()).unwrap();
                return;
            }
        }
    }
    panic!("timeout waiting for blob to become creatable");
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_open_for_create_create() -> Result<(), Error> {
    let blobfs_server = BlobfsRamdisk::start()?;
    let root_dir = blobfs_server.root_dir_proxy()?;

    let (_blob, _) = open_blob(
        &root_dir,
        BLOB_MERKLE,
        fidl_fuchsia_io::OPEN_FLAG_CREATE | fidl_fuchsia_io::OPEN_RIGHT_WRITABLE,
    )
    .await?;

    create_blob(&root_dir, BLOB_MERKLE, BLOB_CONTENTS).await?;

    blobfs_server.stop().await
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_open_truncate_drop_create() -> Result<(), Error> {
    let blobfs_server = BlobfsRamdisk::start()?;
    let root_dir = blobfs_server.root_dir_proxy()?;

    let (blob, _) = open_blob(
        &root_dir,
        BLOB_MERKLE,
        fidl_fuchsia_io::OPEN_FLAG_CREATE | fidl_fuchsia_io::OPEN_RIGHT_WRITABLE,
    )
    .await?;
    Status::ok(blob.truncate(BLOB_CONTENTS.len() as u64).await?)?;
    drop(blob);
    wait_for_blob_to_be_creatable(&root_dir, BLOB_MERKLE).await;

    create_blob(&root_dir, BLOB_MERKLE, BLOB_CONTENTS).await?;

    blobfs_server.stop().await
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_open_partial_write_drop_create() -> Result<(), Error> {
    let blobfs_server = BlobfsRamdisk::start()?;
    let root_dir = blobfs_server.root_dir_proxy()?;

    let (blob, _) = open_blob(
        &root_dir,
        BLOB_MERKLE,
        fidl_fuchsia_io::OPEN_FLAG_CREATE | fidl_fuchsia_io::OPEN_RIGHT_WRITABLE,
    )
    .await?;
    Status::ok(blob.truncate(BLOB_CONTENTS.len() as u64).await?)?;
    write_blob(&blob, &BLOB_CONTENTS[0..1]).await?;
    drop(blob);
    wait_for_blob_to_be_creatable(&root_dir, BLOB_MERKLE).await;

    create_blob(&root_dir, BLOB_MERKLE, BLOB_CONTENTS).await?;

    blobfs_server.stop().await
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_open_partial_write_close_create() -> Result<(), Error> {
    let blobfs_server = BlobfsRamdisk::start()?;
    let root_dir = blobfs_server.root_dir_proxy()?;

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

#[fuchsia_async::run_singlethreaded(test)]
async fn test_create_present_blob_fails() -> Result<(), Error> {
    let blobfs_server = BlobfsRamdisk::start()?;
    let root_dir = blobfs_server.root_dir_proxy()?;

    create_blob(&root_dir, BLOB_MERKLE, BLOB_CONTENTS).await?;

    let res = open_blob(
        &root_dir,
        BLOB_MERKLE,
        fidl_fuchsia_io::OPEN_FLAG_CREATE | fidl_fuchsia_io::OPEN_RIGHT_WRITABLE,
    )
    .await;
    assert_matches!(res, Err(zx::Status::ACCESS_DENIED));

    blobfs_server.stop().await
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_create_present_empty_blob_fails() -> Result<(), Error> {
    let blobfs_server = BlobfsRamdisk::start()?;
    let root_dir = blobfs_server.root_dir_proxy()?;

    create_blob(&root_dir, EMPTY_BLOB_MERKLE, EMPTY_BLOB_CONTENTS).await?;

    let res = open_blob(
        &root_dir,
        EMPTY_BLOB_MERKLE,
        fidl_fuchsia_io::OPEN_FLAG_CREATE | fidl_fuchsia_io::OPEN_RIGHT_WRITABLE,
    )
    .await;
    assert_matches!(res, Err(zx::Status::ACCESS_DENIED));

    blobfs_server.stop().await
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_open_truncate_open_for_create_fails() -> Result<(), Error> {
    let blobfs_server = BlobfsRamdisk::start()?;
    let root_dir = blobfs_server.root_dir_proxy()?;

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

#[fuchsia_async::run_singlethreaded(test)]
async fn test_open_open_truncate_truncate_fails() -> Result<(), Error> {
    let blobfs_server = BlobfsRamdisk::start()?;
    let root_dir = blobfs_server.root_dir_proxy()?;

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

#[fuchsia_async::run_singlethreaded(test)]
async fn test_open_truncate_open_read_fails() -> Result<(), Error> {
    let blobfs_server = BlobfsRamdisk::start()?;
    let root_dir = blobfs_server.root_dir_proxy()?;

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

#[fuchsia_async::run_singlethreaded(test)]
async fn test_open_for_create_wait_for_signal() -> Result<(), Error> {
    let blobfs_server = BlobfsRamdisk::start()?;
    let root_dir = blobfs_server.root_dir_proxy()?;

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
        event.wait_handle(zx::Signals::all(), zx::Time::after(zx::Duration::from_seconds(0)))?,
        zx::Signals::USER_0
    );
    verify_blob(&blob1, BLOB_CONTENTS).await?;

    blobfs_server.stop().await
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_open_truncate_wait_for_signal() -> Result<(), Error> {
    let blobfs_server = BlobfsRamdisk::start()?;
    let root_dir = blobfs_server.root_dir_proxy()?;

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
        event.wait_handle(zx::Signals::all(), zx::Time::after(zx::Duration::from_seconds(0)))?,
        zx::Signals::USER_0
    );
    verify_blob(&blob1, BLOB_CONTENTS).await?;

    blobfs_server.stop().await
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_open_missing_fails() -> Result<(), Error> {
    let blobfs_server = BlobfsRamdisk::start()?;
    let root_dir = blobfs_server.root_dir_proxy()?;

    let res = open_blob(&root_dir, BLOB_MERKLE, fidl_fuchsia_io::OPEN_RIGHT_READABLE).await;

    assert_matches!(res, Err(zx::Status::NOT_FOUND));

    blobfs_server.stop().await
}
