// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Tests for the fuchsia.io/File behavior of file, meta/file, and meta as file. To optimize for
//! simplicity and speed, we don't test e.g. dir/file or meta/dir/file because there aren't any
//! meaningful differences in File behavior.

use {
    crate::{dirs_to_test, repeat_by_n, PackageSource},
    fidl::endpoints::create_proxy,
    fidl::AsHandleRef,
    fidl_fuchsia_io::{
        DirectoryProxy, FileProxy, SeekOrigin, MAX_BUF, OPEN_FLAG_APPEND,
        OPEN_FLAG_CREATE_IF_ABSENT, OPEN_FLAG_DESCRIBE, OPEN_FLAG_DIRECTORY,
        OPEN_FLAG_NODE_REFERENCE, OPEN_FLAG_NOT_DIRECTORY, OPEN_FLAG_NO_REMOTE, OPEN_FLAG_POSIX,
        OPEN_RIGHT_ADMIN, OPEN_RIGHT_EXECUTABLE, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE,
        VMO_FLAG_EXACT, VMO_FLAG_EXEC, VMO_FLAG_PRIVATE, VMO_FLAG_READ, VMO_FLAG_WRITE,
    },
    fuchsia_zircon as zx,
    io_util::directory::open_file,
    matches::assert_matches,
    rand::Rng as _,
    std::{cmp, convert::TryInto},
};

const TEST_PKG_HASH: &str = "4679b8a4d2853fa935f4c63511f402ab387f1afbc26cf9addec3a24f2c5dc598";

#[fuchsia::test]
async fn read() {
    for source in dirs_to_test().await {
        read_per_package_source(source).await
    }
}

async fn read_per_package_source(source: PackageSource) {
    let root_dir = source.dir;
    for (path, expected_contents) in
        [("file", "file"), ("meta/file", "meta/file"), ("meta", TEST_PKG_HASH)]
    {
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
    for source in dirs_to_test().await {
        read_at_per_package_source(source).await
    }
}

async fn read_at_per_package_source(source: PackageSource) {
    let root_dir = source.dir;
    for path in ["file", "meta/file"] {
        assert_read_at_max_buffer_success(&root_dir, path).await;
        assert_read_at_success(&root_dir, path).await;

        assert_read_at_does_not_affect_seek(&root_dir, path, SeekOrigin::Start).await;
        assert_read_at_does_not_affect_seek(&root_dir, path, SeekOrigin::Current).await;
        assert_read_at_does_not_affect_seek_end_origin(&root_dir, path).await;
    }

    // ReadAt for "meta as file" is unsupported.
    let file = open_file(&root_dir, "meta", OPEN_RIGHT_READABLE).await.unwrap();
    let (status, bytes) = file.read_at(MAX_BUF, 0).await.unwrap();
    assert_eq!(zx::Status::ok(status), Err(zx::Status::NOT_SUPPORTED));
    assert_eq!(bytes, &[]);
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

    let (status, bytes) = file.read_at(MAX_BUF, 0).await.unwrap();
    let () = zx::Status::ok(status).unwrap();
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

    let (status, bytes) = file.read_at(MAX_BUF, 0).await.unwrap();
    let () = zx::Status::ok(status).unwrap();
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
    for source in dirs_to_test().await {
        seek_per_package_source(source).await
    }
}

async fn seek_per_package_source(source: PackageSource) {
    let root_dir = source.dir;
    for path in ["file", "meta/file"] {
        assert_seek_success(&root_dir, path, SeekOrigin::Start).await;
        assert_seek_success(&root_dir, path, SeekOrigin::Current).await;

        assert_seek_affects_read(&root_dir, path).await;

        assert_seek_past_end(&root_dir, path, SeekOrigin::Start).await;
        assert_seek_past_end(&root_dir, path, SeekOrigin::Current).await;
        assert_seek_past_end_end_origin(&root_dir, path).await;
    }

    // Seek for "meta as file" is unsupported.
    let file = open_file(&root_dir, "meta", OPEN_RIGHT_READABLE).await.unwrap();
    let mut rng = rand::thread_rng();
    let seek_offset = rng.gen_range(0, TEST_PKG_HASH.len()) as i64;
    let (status, position) = file.seek(seek_offset, SeekOrigin::Current).await.unwrap();
    assert_eq!(zx::Status::ok(status), Err(zx::Status::NOT_SUPPORTED));
    assert_eq!(position, 0);
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
    let (status, bytes) = file.read(MAX_BUF).await.unwrap();
    let () = zx::Status::ok(status).unwrap();
    assert_eq!(std::str::from_utf8(&bytes).unwrap(), path);

    let (status, bytes) = file.read(MAX_BUF).await.unwrap();
    let () = zx::Status::ok(status).unwrap();
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
    for source in dirs_to_test().await {
        get_buffer_per_package_source(source).await
    }
}

// Ported over version of TestMapFileForRead, TestMapFileForReadPrivate, TestMapFileForReadExact,
// TestMapFilePrivateAndExact, TestMapFileForWrite, and TestMapFileForExec from pkgfs_test.go.
async fn get_buffer_per_package_source(source: PackageSource) {
    let root_dir = source.dir;
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
    for source in dirs_to_test().await {
        clone_per_package_source(source).await
    }
}

// TODO(fxbug.dev/81447) test Clones for meta as file. Currently, if we try and test Cloning
// meta as file, it will hang.
async fn clone_per_package_source(source: PackageSource) {
    let root_dir = source.dir;
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

#[fuchsia::test]
async fn get_flags() {
    for source in dirs_to_test().await {
        get_flags_per_package_source(source).await
    }
}

async fn get_flags_per_package_source(source: PackageSource) {
    let root_dir = source.dir;
    assert_get_flags_content_file(&root_dir).await;
    assert_get_flags_meta_file(&root_dir, "meta").await;
    assert_get_flags_meta_file(&root_dir, "meta/file").await;
}

/// Opens a file and verifies the result of GetFlags() and NodeGetFlags().
async fn assert_get_flags(
    root_dir: &DirectoryProxy,
    path: &str,
    open_flag: u32,
    status_flags: u32,
    right_flags: u32,
) {
    let file = open_file(&root_dir, path, open_flag).await.unwrap();

    // The flags returned by GetFlags() do NOT always match the flags the file is opened with
    // because File servers each AND the open flag with some other flags. The `status_flags` and
    // `right_flags` parameters are meant to account for these implementation differences.
    let expected_flags = open_flag & (status_flags | right_flags);

    // Verify GetFlags() produces the expected result.
    let (status, flags) = file.get_flags().await.unwrap();
    let () = zx::Status::ok(status).unwrap();
    assert_eq!(flags, expected_flags);

    // NodeGetFlags() and GetFlags() should have the same result.
    let (status, flags) = file.node_get_flags().await.unwrap();
    let () = zx::Status::ok(status).unwrap();
    assert_eq!(flags, expected_flags);
}

async fn assert_get_flags_content_file(root_dir: &DirectoryProxy) {
    // Content files are served by blobfs, which uses the VFS library to filter out some of the
    // open flags before returning.
    // https://cs.opensource.google/fuchsia/fuchsia/+/main:src/lib/storage/vfs/cpp/file_connection.cc;l=125;drc=6a01adba247f273496ee8b9227c24252a459a534
    let status_flags = OPEN_FLAG_APPEND | OPEN_FLAG_NODE_REFERENCE;
    let right_flags =
        OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | OPEN_RIGHT_ADMIN | OPEN_RIGHT_EXECUTABLE;

    for open_flag in [
        OPEN_RIGHT_READABLE,
        OPEN_RIGHT_READABLE | OPEN_FLAG_CREATE_IF_ABSENT,
        OPEN_RIGHT_READABLE | OPEN_FLAG_NO_REMOTE,
        OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE,
        OPEN_RIGHT_READABLE | OPEN_FLAG_POSIX,
        OPEN_RIGHT_READABLE | OPEN_FLAG_NOT_DIRECTORY,
        OPEN_RIGHT_EXECUTABLE,
        OPEN_RIGHT_EXECUTABLE | OPEN_FLAG_CREATE_IF_ABSENT,
        OPEN_RIGHT_EXECUTABLE | OPEN_FLAG_NO_REMOTE,
        OPEN_RIGHT_EXECUTABLE | OPEN_FLAG_DESCRIBE,
        OPEN_RIGHT_EXECUTABLE | OPEN_FLAG_POSIX,
        OPEN_RIGHT_EXECUTABLE | OPEN_FLAG_NOT_DIRECTORY,
    ] {
        assert_get_flags(&root_dir, "file", open_flag, status_flags, right_flags).await;
    }
}

async fn assert_get_flags_meta_file(root_dir: &DirectoryProxy, path: &str) {
    // Meta files are served by thinfs, which filter out some of the open flags before returning.
    // https://cs.opensource.google/fuchsia/fuchsia/+/main:src/lib/thinfs/zircon/rpc/rpc.go;l=562-565;drc=fb0bf809980ed37f43a15e4292fb10bf943cb00e
    let status_flags = OPEN_FLAG_APPEND;
    let right_flags = OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | OPEN_FLAG_NODE_REFERENCE;

    for open_flag in [
        0,
        OPEN_RIGHT_READABLE,
        OPEN_RIGHT_ADMIN,
        OPEN_FLAG_CREATE_IF_ABSENT,
        OPEN_FLAG_DIRECTORY,
        OPEN_FLAG_NO_REMOTE,
        OPEN_FLAG_NODE_REFERENCE,
        OPEN_FLAG_DESCRIBE,
        OPEN_FLAG_POSIX,
        OPEN_FLAG_NOT_DIRECTORY,
    ] {
        assert_get_flags(&root_dir, path, open_flag, status_flags, right_flags).await;
    }
}

#[fuchsia::test]
async fn unsupported() {
    for source in dirs_to_test().await {
        unsupported_per_package_source(source).await
    }
}

async fn unsupported_per_package_source(source: PackageSource) {
    let root_dir = source.dir;
    async fn verify_unsupported_calls(
        root_dir: &DirectoryProxy,
        path: &str,
        expected_status: zx::Status,
    ) {
        let file = open_file(root_dir, path, OPEN_RIGHT_READABLE).await.unwrap();

        // Verify write() fails.
        let (status, bytes_written) = file.write(b"potato").await.unwrap();
        assert_eq!(zx::Status::from_raw(status), expected_status);
        assert_eq!(bytes_written, 0);

        // Verify writeAt() fails.
        let (status, bytes_written) = file.write_at(b"potato", 0).await.unwrap();
        assert_eq!(zx::Status::from_raw(status), expected_status);
        assert_eq!(bytes_written, 0);

        // Verify setAttr() fails.
        assert_eq!(
            zx::Status::from_raw(
                file.set_attr(
                    0,
                    &mut fidl_fuchsia_io::NodeAttributes {
                        mode: 0,
                        id: 0,
                        content_size: 0,
                        storage_size: 0,
                        link_count: 0,
                        creation_time: 0,
                        modification_time: 0
                    }
                )
                .await
                .unwrap()
            ),
            expected_status
        );

        // Verify truncate() fails.
        assert_eq!(zx::Status::from_raw(file.truncate(0).await.unwrap()), expected_status);
    }

    // The name of this test is slightly misleading because files not under meta will yield
    // BAD_HANDLE for the unsupported file APIs. This is actually consistent with the fuchsia.io
    // documentation because files without WRITE permissions *should* yield BAD_HANDLE for these
    // methods.
    verify_unsupported_calls(&root_dir, "file", zx::Status::BAD_HANDLE).await;

    verify_unsupported_calls(&root_dir, "meta/file", zx::Status::NOT_SUPPORTED).await;
    verify_unsupported_calls(&root_dir, "meta", zx::Status::NOT_SUPPORTED).await;
}
