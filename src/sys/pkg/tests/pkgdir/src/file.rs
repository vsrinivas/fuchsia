// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Tests for the fuchsia.io/File behavior of file, meta/file, and meta as file. To optimize for
//! simplicity and speed, we don't test e.g. dir/file or meta/dir/file because there aren't any
//! meaningful differences in File behavior.

use {
    crate::{dirs_to_test, repeat_by_n},
    fidl::endpoints::create_proxy,
    fidl::AsHandleRef,
    fidl_fuchsia_io::{
        DirectoryProxy, FileProxy, SeekOrigin, MAX_BUF, OPEN_RIGHT_READABLE, VMO_FLAG_EXACT,
        VMO_FLAG_EXEC, VMO_FLAG_PRIVATE, VMO_FLAG_READ, VMO_FLAG_WRITE,
    },
    fuchsia_zircon as zx,
    io_util::directory::open_file,
    matches::assert_matches,
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
    for (path, expected_contents) in [("file", "file"), ("meta/file", "meta/file")] {
        assert_read_max_buffer_success(&root_dir, path, expected_contents).await;
        assert_read_buffer_success(&root_dir, path, expected_contents).await;
        assert_read_past_end(&root_dir, path, expected_contents).await;
    }

    assert_read_exceeds_buffer_success(&root_dir, "exceeds_max_buf").await;
    assert_read_exceeds_buffer_success(&root_dir, "meta/exceeds_max_buf").await;
}

async fn assert_read_max_buffer_success(
    root_dir: &DirectoryProxy,
    path: &str,
    expected_contents: &str,
) {
    let file = open_file(&root_dir, path, OPEN_RIGHT_READABLE).await.unwrap();
    let (status, bytes) = file.read(MAX_BUF).await.unwrap();
    let () = zx::Status::ok(status).unwrap();
    assert_eq!(std::str::from_utf8(&bytes).unwrap(), expected_contents);
}

async fn assert_read_buffer_success(
    root_dir: &DirectoryProxy,
    path: &str,
    expected_contents: &str,
) {
    let mut rng = rand::thread_rng();
    let buffer_size = rng.gen_range(0, expected_contents.len());
    let expected_contents = &expected_contents[0..buffer_size];

    let file = open_file(&root_dir, path, OPEN_RIGHT_READABLE).await.unwrap();
    let (status, bytes) = file.read(buffer_size.try_into().unwrap()).await.unwrap();
    let () = zx::Status::ok(status).unwrap();
    assert_eq!(std::str::from_utf8(&bytes).unwrap(), expected_contents);
}

async fn assert_read_past_end(root_dir: &DirectoryProxy, path: &str, expected_contents: &str) {
    let file = open_file(&root_dir, path, OPEN_RIGHT_READABLE).await.unwrap();
    let (status, bytes) = file.read(MAX_BUF).await.unwrap();
    let () = zx::Status::ok(status).unwrap();
    assert_eq!(std::str::from_utf8(&bytes).unwrap(), expected_contents);

    let (status, bytes) = file.read(MAX_BUF).await.unwrap();
    let () = zx::Status::ok(status).unwrap();
    assert_eq!(bytes, &[]);
}

async fn assert_read_exceeds_buffer_success(root_dir: &DirectoryProxy, path: &str) {
    let file = open_file(&root_dir, path, OPEN_RIGHT_READABLE).await.unwrap();

    // Read the first MAX_BUF contents.
    let (status, bytes) = file.read(MAX_BUF).await.unwrap();
    let () = zx::Status::ok(status).unwrap();
    assert_eq!(
        std::str::from_utf8(&bytes).unwrap(),
        &repeat_by_n('a', fidl_fuchsia_io::MAX_BUF.try_into().unwrap())
    );

    // There should be one remaining "a".
    let (status, bytes) = file.read(MAX_BUF).await.unwrap();
    let () = zx::Status::ok(status).unwrap();
    assert_eq!(std::str::from_utf8(&bytes).unwrap(), "a");

    // Since we are now at the end of the file, bytes should be empty.
    let (status, bytes) = file.read(MAX_BUF).await.unwrap();
    let () = zx::Status::ok(status).unwrap();
    assert_eq!(bytes, &[]);
}

#[fuchsia::test]
async fn read_at() {
    for dir in dirs_to_test().await {
        read_at_per_package_source(dir).await
    }
}

async fn read_at_per_package_source(root_dir: DirectoryProxy) {
    for path in ["file", "meta/file"] {
        assert_read_at_max_buffer_success(&root_dir, path).await;
        assert_read_at_success(&root_dir, path).await;

        assert_read_at_does_not_affect_seek(&root_dir, path, SeekOrigin::Start).await;
        assert_read_at_does_not_affect_seek(&root_dir, path, SeekOrigin::Current).await;
        assert_read_at_does_not_affect_seek_end_origin(&root_dir, path).await;
    }
}

async fn assert_read_at_max_buffer_success(root_dir: &DirectoryProxy, path: &str) {
    let file = open_file(&root_dir, path, OPEN_RIGHT_READABLE).await.unwrap();
    let (status, bytes) = file.read_at(MAX_BUF, 0).await.unwrap();
    let () = zx::Status::ok(status).unwrap();
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
    let () = zx::Status::ok(status).unwrap();
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
    let () = zx::Status::ok(status).unwrap();
    assert_eq!(position, seek_offset as u64);

    let (status, bytes) = file.read_at(MAX_BUF, 0).await.unwrap();
    let () = zx::Status::ok(status).unwrap();
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
    let () = zx::Status::ok(status).unwrap();
    assert_eq!(position, (path.len() as i64 + seek_offset) as u64);

    let (status, bytes) = file.read_at(MAX_BUF, 0).await.unwrap();
    let () = zx::Status::ok(status).unwrap();

    assert_eq!(std::str::from_utf8(&bytes).unwrap(), path);
}

#[fuchsia::test]
async fn seek() {
    for dir in dirs_to_test().await {
        seek_per_package_source(dir).await
    }
}

async fn seek_per_package_source(root_dir: DirectoryProxy) {
    for path in ["file", "meta/file"] {
        assert_seek_success(&root_dir, path, SeekOrigin::Start).await;
        assert_seek_success(&root_dir, path, SeekOrigin::Current).await;

        assert_seek_affects_read(&root_dir, path).await;

        assert_seek_past_end(&root_dir, path, SeekOrigin::Start).await;
        assert_seek_past_end(&root_dir, path, SeekOrigin::Current).await;
        assert_seek_past_end_end_origin(&root_dir, path).await;
    }
}

async fn assert_seek_success(root_dir: &DirectoryProxy, path: &str, seek_origin: SeekOrigin) {
    let mut rng = rand::thread_rng();
    let expected_position = rng.gen_range(0, path.len());

    let file = open_file(&root_dir, path, OPEN_RIGHT_READABLE).await.unwrap();
    let (status, position) =
        file.seek(expected_position.try_into().unwrap(), seek_origin).await.unwrap();
    let () = zx::Status::ok(status).unwrap();
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
    let () = zx::Status::ok(status).unwrap();

    assert_eq!(position, (path.len() - seek_offset) as u64);

    let (status, bytes) = file.read(MAX_BUF).await.unwrap();
    let () = zx::Status::ok(status).unwrap();
    assert_eq!(std::str::from_utf8(&bytes).unwrap(), expected_contents);
}

async fn assert_seek_past_end(root_dir: &DirectoryProxy, path: &str, seek_origin: SeekOrigin) {
    let file = open_file(&root_dir, path, OPEN_RIGHT_READABLE).await.unwrap();
    let (status, position) = file.seek(path.len() as i64 + 1, seek_origin).await.unwrap();
    let () = zx::Status::ok(status).unwrap();
    assert_eq!(path.len() as u64 + 1, position);

    let (status, bytes) = file.read(MAX_BUF).await.unwrap();
    let () = zx::Status::ok(status).unwrap();
    assert_eq!(bytes, &[]);
}

// The difference between this test and `assert_seek_past_end` is that the offset is 1
// so that the position is evaluated to path.len() + 1 like in `assert_seek_past_end`.
async fn assert_seek_past_end_end_origin(root_dir: &DirectoryProxy, path: &str) {
    let file = open_file(&root_dir, path, OPEN_RIGHT_READABLE).await.unwrap();
    let (status, position) = file.seek(1, SeekOrigin::End).await.unwrap();
    let () = zx::Status::ok(status).unwrap();
    assert_eq!(path.len() as u64 + 1, position);

    let (status, bytes) = file.read(MAX_BUF).await.unwrap();
    let () = zx::Status::ok(status).unwrap();
    assert_eq!(bytes, &[]);
}

#[fuchsia::test]
async fn get_buffer() {
    for dir in dirs_to_test().await {
        get_buffer_per_package_source(dir).await
    }
}

// Ported over version of TestMapFileForRead, TestMapFileForReadPrivate, TestMapFileForReadExact,
// TestMapFilePrivateAndExact, TestMapFileForWrite, and TestMapFileForExec from pkgfs_test.go.
async fn get_buffer_per_package_source(root_dir: DirectoryProxy) {
    // For non-meta files, GetBuffer() calls with supported flags should succeed.
    for path in ["file", "meta/file"] {
        let file = open_file(&root_dir, path, OPEN_RIGHT_READABLE).await.unwrap();

        let _ = test_get_buffer_success(&file, path, VMO_FLAG_READ).await;

        let buffer0 = test_get_buffer_success(&file, path, VMO_FLAG_READ | VMO_FLAG_PRIVATE).await;
        let buffer1 = test_get_buffer_success(&file, path, VMO_FLAG_READ | VMO_FLAG_PRIVATE).await;
        assert_ne!(
            buffer0.vmo.as_handle_ref().get_koid().unwrap(),
            buffer1.vmo.as_handle_ref().get_koid().unwrap(),
            "We should receive our own clone each time we invoke GetBuffer() with the VmoFlagPrivate field set"
        );
    }

    // For "meta as file", GetBuffer() should be unsupported.
    let file = open_file(&root_dir, "meta", OPEN_RIGHT_READABLE).await.unwrap();
    let (status, _buffer) = file.get_buffer(VMO_FLAG_READ).await.unwrap();
    assert_eq!(zx::Status::ok(status), Err(zx::Status::NOT_SUPPORTED));

    // For files NOT under meta, GetBuffer() calls with unsupported flags should successfully return
    // the FIDL call with a failure status.
    let file = open_file(&root_dir, "file", OPEN_RIGHT_READABLE).await.unwrap();
    let (status, _buffer) = file.get_buffer(VMO_FLAG_EXEC).await.unwrap();
    assert_eq!(zx::Status::ok(status), Err(zx::Status::ACCESS_DENIED));
    let (status, _buffer) = file.get_buffer(VMO_FLAG_EXACT).await.unwrap();
    assert_eq!(zx::Status::ok(status), Err(zx::Status::NOT_SUPPORTED));
    let (status, _buffer) = file.get_buffer(VMO_FLAG_PRIVATE | VMO_FLAG_EXACT).await.unwrap();
    assert_eq!(zx::Status::ok(status), Err(zx::Status::INVALID_ARGS));
    let (status, _buffer) = file.get_buffer(VMO_FLAG_WRITE).await.unwrap();
    assert_eq!(zx::Status::ok(status), Err(zx::Status::ACCESS_DENIED));

    // For files under meta, GetBuffer() calls with unsupported flags should fail the FIDL call
    // because the file connection should terminate.
    for flags in [VMO_FLAG_EXEC, VMO_FLAG_EXACT, VMO_FLAG_EXACT | VMO_FLAG_PRIVATE, VMO_FLAG_WRITE]
    {
        let file = open_file(&root_dir, "meta/file", OPEN_RIGHT_READABLE).await.unwrap();
        assert_matches!(file.get_buffer(flags).await, Err(fidl::Error::ClientChannelClosed { .. }));
    }
}

async fn test_get_buffer_success(
    file: &FileProxy,
    path: &str,
    get_buffer_flags: u32,
) -> Box<fidl_fuchsia_mem::Buffer> {
    let (status, buffer) = file.get_buffer(get_buffer_flags).await.unwrap();
    let () = zx::Status::ok(status).unwrap();
    let buffer = buffer.unwrap();

    let vmo_size = buffer.vmo.get_size().unwrap().try_into().unwrap();
    let mut actual_contents = vec![0u8; vmo_size];
    let () = buffer.vmo.read(actual_contents.as_mut_slice(), 0).unwrap();

    assert!(
        path.as_bytes()
            .iter()
            .copied()
            // VMOs should be zero-padded to 4096 bytes.
            .chain(std::iter::repeat(b'\0'))
            .take(vmo_size)
            .eq(actual_contents.iter().copied()),
        "vmo content mismatch for file size {}",
        vmo_size
    );

    buffer
}

#[fuchsia::test]
async fn clone() {
    for dir in dirs_to_test().await {
        clone_per_package_source(dir).await
    }
}

// TODO(fxbug.dev/81447) test Clones for meta as file. Currently, if we try and test Cloning
// meta as file, it will hang.
async fn clone_per_package_source(root_dir: DirectoryProxy) {
    assert_clone_success(&root_dir, "file").await;
    assert_clone_success(&root_dir, "meta/file").await;
}

async fn assert_clone_success(package_root: &DirectoryProxy, path: &str) {
    let parent =
        open_file(package_root, path, OPEN_RIGHT_READABLE).await.expect("open parent directory");
    let (clone, server_end) = create_proxy::<fidl_fuchsia_io::FileMarker>().expect("create_proxy");
    let node_request = fidl::endpoints::ServerEnd::new(server_end.into_channel());
    parent.clone(OPEN_RIGHT_READABLE, node_request).expect("cloned node");

    let (status, bytes) = clone.read(MAX_BUF).await.unwrap();
    let () = zx::Status::ok(status).unwrap();
    assert_eq!(std::str::from_utf8(&bytes).unwrap(), path);
}
