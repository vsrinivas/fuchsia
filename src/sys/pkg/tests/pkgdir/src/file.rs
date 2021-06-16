// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::dirs_to_test,
    fidl_fuchsia_io::{DirectoryProxy, SeekOrigin, MAX_BUF, OPEN_RIGHT_READABLE},
    fuchsia_zircon as zx,
    io_util::directory::open_file,
    rand::Rng as _,
    std::{cmp, convert::TryInto},
};

#[fuchsia::test]
async fn read() {
    for dir in dirs_to_test().await {
        read_per_package_source(dir).await
    }
}

async fn read_per_package_source(root_dir: DirectoryProxy) {
    for path in [
        "file",
        "dir/file",
        "dir/dir/file",
        "dir/dir/dir/file",
        "meta/file",
        "meta/dir/file",
        "meta/dir/dir/file",
        "meta/dir/dir/dir/file",
    ] {
        assert_read_max_buffer_success(&root_dir, path).await;
        assert_read_buffer_success(&root_dir, path).await;
        assert_read_past_end(&root_dir, path).await;

        assert_read_at_max_buffer_success(&root_dir, path).await;
        assert_read_at_success(&root_dir, path).await;

        assert_seek_success(&root_dir, path, SeekOrigin::Start).await;
        assert_seek_success(&root_dir, path, SeekOrigin::Current).await;

        assert_seek_affects_read(&root_dir, path).await;

        assert_read_at_does_not_affect_seek(&root_dir, path, SeekOrigin::Start).await;
        assert_read_at_does_not_affect_seek(&root_dir, path, SeekOrigin::Current).await;
        assert_read_at_does_not_affect_seek_end_origin(&root_dir, path).await;

        assert_seek_past_end(&root_dir, path, SeekOrigin::Start).await;
        assert_seek_past_end(&root_dir, path, SeekOrigin::Current).await;
        assert_seek_past_end_end_origin(&root_dir, path).await;
    }

    assert_read_exceeds_buffer_success(&root_dir, "exceeds_max_buf").await;
    assert_read_exceeds_buffer_success(&root_dir, "meta/exceeds_max_buf").await;
}

async fn assert_read_max_buffer_success(root_dir: &DirectoryProxy, path: &str) {
    let file = open_file(&root_dir, path, OPEN_RIGHT_READABLE).await.unwrap();
    let (status, bytes) = file.read(MAX_BUF).await.unwrap();
    assert_eq!(zx::Status::ok(status), Ok(()));
    assert_eq!(std::str::from_utf8(&bytes).unwrap(), path);
}

async fn assert_read_buffer_success(root_dir: &DirectoryProxy, path: &str) {
    let mut rng = rand::thread_rng();
    let buffer_size = rng.gen_range(0, path.len());
    let expected_contents = &path[0..buffer_size];

    let file = open_file(&root_dir, path, OPEN_RIGHT_READABLE).await.unwrap();
    let (status, bytes) = file.read(buffer_size.try_into().unwrap()).await.unwrap();
    assert_eq!(zx::Status::ok(status), Ok(()));
    assert_eq!(std::str::from_utf8(&bytes).unwrap(), expected_contents);
}

async fn assert_read_past_end(root_dir: &DirectoryProxy, path: &str) {
    let file = open_file(&root_dir, path, OPEN_RIGHT_READABLE).await.unwrap();
    let (status, bytes) = file.read(MAX_BUF).await.unwrap();
    assert_eq!(zx::Status::ok(status), Ok(()));
    assert_eq!(std::str::from_utf8(&bytes).unwrap(), path);

    let (status, bytes) = file.read(MAX_BUF).await.unwrap();
    assert_eq!(zx::Status::ok(status), Ok(()));
    assert_eq!(bytes, &[]);
}

async fn assert_read_at_max_buffer_success(root_dir: &DirectoryProxy, path: &str) {
    let file = open_file(&root_dir, path, OPEN_RIGHT_READABLE).await.unwrap();
    let (status, bytes) = file.read_at(MAX_BUF, 0).await.unwrap();
    assert_eq!(zx::Status::ok(status), Ok(()));
    assert_eq!(std::str::from_utf8(&bytes).unwrap(), path);
}

async fn assert_read_at_success(root_dir: &DirectoryProxy, path: &str) {
    let mut rng = rand::thread_rng();
    let offset = rng.gen_range(0, path.len());
    let count = rng.gen_range(0, path.len());
    let end = cmp::min(count + offset, path.len());
    let expected_contents = &path[offset..end];

    let file = open_file(&root_dir, path, OPEN_RIGHT_READABLE).await.unwrap();
    let (status, bytes) =
        file.read_at(count.try_into().unwrap(), offset.try_into().unwrap()).await.unwrap();
    assert_eq!(zx::Status::ok(status), Ok(()));
    assert_eq!(std::str::from_utf8(&bytes).unwrap(), expected_contents);
}

async fn assert_seek_success(root_dir: &DirectoryProxy, path: &str, seek_origin: SeekOrigin) {
    let mut rng = rand::thread_rng();
    let expected_position = rng.gen_range(0, path.len());

    let file = open_file(&root_dir, path, OPEN_RIGHT_READABLE).await.unwrap();
    let (status, position) =
        file.seek(expected_position.try_into().unwrap(), seek_origin).await.unwrap();
    assert_eq!(zx::Status::ok(status), Ok(()));
    assert_eq!(position, expected_position as u64);
}

async fn assert_seek_affects_read(root_dir: &DirectoryProxy, path: &str) {
    let file = open_file(&root_dir, path, OPEN_RIGHT_READABLE).await.unwrap();
    let (_, bytes) = file.read(MAX_BUF).await.unwrap();
    assert_eq!(std::str::from_utf8(&bytes).unwrap(), path);

    let (_, bytes) = file.read(MAX_BUF).await.unwrap();
    assert_eq!(bytes, &[]);

    let mut rng = rand::thread_rng();
    let seek_offset = rng.gen_range(0, path.len());
    let expected_contents = &path[path.len() - seek_offset..];
    let (status, position) = file.seek((seek_offset as i64) * -1, SeekOrigin::End).await.unwrap();
    assert_eq!(zx::Status::ok(status), Ok(()));

    assert_eq!(position, (path.len() - seek_offset) as u64);

    let (status, bytes) = file.read(MAX_BUF).await.unwrap();
    assert_eq!(zx::Status::ok(status), Ok(()));
    assert_eq!(std::str::from_utf8(&bytes).unwrap(), expected_contents);
}

async fn assert_read_at_does_not_affect_seek(
    root_dir: &DirectoryProxy,
    path: &str,
    seek_origin: SeekOrigin,
) {
    let file = open_file(&root_dir, path, OPEN_RIGHT_READABLE).await.unwrap();

    let (_, bytes) = file.read_at(MAX_BUF, 0).await.unwrap();
    assert_eq!(std::str::from_utf8(&bytes).unwrap(), path);

    let mut rng = rand::thread_rng();
    let seek_offset = rng.gen_range(0, path.len()) as i64;

    let (status, position) = file.seek(seek_offset, seek_origin).await.unwrap();
    assert_eq!(zx::Status::ok(status), Ok(()));
    assert_eq!(position, seek_offset as u64);

    let (status, bytes) = file.read_at(MAX_BUF, 0).await.unwrap();
    assert_eq!(zx::Status::ok(status), Ok(()));
    assert_eq!(std::str::from_utf8(&bytes).unwrap(), path);
}

// The difference between this test and `assert_read_at_does_not_affect_seek`  is that the SeekOrigin
// is set to SeekOrigin::End and the offset is a negative value.
async fn assert_read_at_does_not_affect_seek_end_origin(root_dir: &DirectoryProxy, path: &str) {
    let file = open_file(&root_dir, path, OPEN_RIGHT_READABLE).await.unwrap();

    let (_, bytes) = file.read_at(MAX_BUF, 0).await.unwrap();
    assert_eq!(std::str::from_utf8(&bytes).unwrap(), path);

    let mut rng = rand::thread_rng();
    let seek_offset = rng.gen_range(0, path.len()) as i64 * -1;

    let (status, position) = file.seek(seek_offset, SeekOrigin::End).await.unwrap();
    assert_eq!(zx::Status::ok(status), Ok(()));
    assert_eq!(position, (path.len() as i64 + seek_offset) as u64);

    let (status, bytes) = file.read_at(MAX_BUF, 0).await.unwrap();
    assert_eq!(zx::Status::ok(status), Ok(()));

    assert_eq!(std::str::from_utf8(&bytes).unwrap(), path);
}

async fn assert_read_exceeds_buffer_success(root_dir: &DirectoryProxy, path: &str) {
    let file = open_file(&root_dir, path, OPEN_RIGHT_READABLE).await.unwrap();

    // Read the first MAX_BUF contents.
    let (status, bytes) = file.read(MAX_BUF).await.unwrap();
    assert_eq!(zx::Status::ok(status), Ok(()));
    assert_eq!(
        std::str::from_utf8(&bytes).unwrap(),
        &std::iter::repeat('a').take(MAX_BUF.try_into().unwrap()).collect::<String>()
    );

    // There should be one remaining "a".
    let (status, bytes) = file.read(MAX_BUF).await.unwrap();
    assert_eq!(zx::Status::ok(status), Ok(()));
    assert_eq!(std::str::from_utf8(&bytes).unwrap(), "a");

    // Since we are now at the end of the file, bytes should be empty.
    let (status, bytes) = file.read(MAX_BUF).await.unwrap();
    assert_eq!(zx::Status::ok(status), Ok(()));
    assert_eq!(bytes, &[]);
}

async fn assert_seek_past_end(root_dir: &DirectoryProxy, path: &str, seek_origin: SeekOrigin) {
    let file = open_file(&root_dir, path, OPEN_RIGHT_READABLE).await.unwrap();
    let (status, position) = file.seek(path.len() as i64 + 1, seek_origin).await.unwrap();
    assert_eq!(zx::Status::ok(status), Ok(()));
    assert_eq!(path.len() as u64 + 1, position);

    let (status, bytes) = file.read(MAX_BUF).await.unwrap();
    assert_eq!(zx::Status::ok(status), Ok(()));
    assert_eq!(bytes, &[]);
}

// The difference between this test and `assert_seek_past_end` is that the offset is 1
// so that the position is evaluated to path.len() + 1 like in `assert_seek_past_end`.
async fn assert_seek_past_end_end_origin(root_dir: &DirectoryProxy, path: &str) {
    let file = open_file(&root_dir, path, OPEN_RIGHT_READABLE).await.unwrap();
    let (status, position) = file.seek(1, SeekOrigin::End).await.unwrap();
    assert_eq!(zx::Status::ok(status), Ok(()));
    assert_eq!(path.len() as u64 + 1, position);

    let (status, bytes) = file.read(MAX_BUF).await.unwrap();
    assert_eq!(zx::Status::ok(status), Ok(()));
    assert_eq!(bytes, &[]);
}
