// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    fidl_fuchsia_io::{INO_UNKNOWN, MODE_TYPE_DIRECTORY, MODE_TYPE_FILE},
    futures::StreamExt,
    io_util::{directory, file, node, OPEN_RIGHT_EXECUTABLE, OPEN_RIGHT_READABLE},
    libc::{S_IRUSR, S_IXUSR},
};

// Since this is a system test, we're actually going to verify real system critical files. That
// means that these tests take a dependency on these files existing in the system, which may
// not forever be true. If any of the files listed here are removed, it's fine to update the set
// of checked files.
const SAMPLE_UTF8_READONLY_FILE: &str = "/boot/config/build_info/minimum_utc_stamp";
const SAMPLE_REQUIRED_DIRECTORY: &str = "/boot/lib";
const KERNEL_VDSO_DIRECTORY: &str = "/boot/kernel/vdso";
const BOOTFS_READONLY_FILES: &[&str] =
    &["/boot/config/component_manager", "/boot/meta/driver_manager.cm"];
const BOOTFS_DATA_DIRECTORY: &str = "/boot/data";
const BOOTFS_EXECUTABLE_LIB_FILES: &[&str] = &["ld.so.1", "libdriver_runtime.so"];
const BOOTFS_EXECUTABLE_NON_LIB_FILES: &[&str] =
    &["/boot/driver/fragment.so", "/boot/bin/component_manager"];

#[fuchsia::test]
async fn basic_filenode_test() -> Result<(), Error> {
    // Open the known good file as a node, and check its attributes.
    let node = node::open_in_namespace(SAMPLE_UTF8_READONLY_FILE, OPEN_RIGHT_READABLE)
        .context("failed to open as a readable node")?;

    // This node should be a readonly file, the inode should not be unknown,
    // and creation and modification times should be 0 since system UTC
    // isn't available or reliable in early boot.
    assert_eq!(node.get_attr().await?.1.mode, MODE_TYPE_FILE | S_IRUSR);
    assert_ne!(node.get_attr().await?.1.id, INO_UNKNOWN);
    assert_eq!(node.get_attr().await?.1.creation_time, 0);
    assert_eq!(node.get_attr().await?.1.modification_time, 0);

    node::close(node).await?;

    // Reopen the known good file as a file to make use of the helper functions.
    let file = file::open_in_namespace(SAMPLE_UTF8_READONLY_FILE, OPEN_RIGHT_READABLE)
        .context("failed to open as a readable file")?;

    // Check for data corruption. This file should contain a single utf-8 string which can
    // be converted into a non-zero unsigned integer.
    let file_contents =
        file::read_to_string(&file).await.context("failed to read utf-8 file to string")?;
    let parsed_time = file_contents
        .trim()
        .parse::<u64>()
        .context("failed to utf-8 string as a number (and it should be a number!)")?;
    assert_ne!(parsed_time, 0);

    file::close(file).await?;

    Ok(())
}

#[fuchsia::test]
async fn basic_directory_test() -> Result<(), Error> {
    // Open the known good file as a node, and check its attributes.
    let node = node::open_in_namespace(
        SAMPLE_REQUIRED_DIRECTORY,
        OPEN_RIGHT_READABLE | OPEN_RIGHT_EXECUTABLE,
    )
    .context("failed to open as a readable and executable node")?;

    // This node should be an immutable directory, the inode should not be unknown,
    // and creation and modification times should be 0 since system UTC isn't
    // available or reliable in early boot.
    assert_ne!(node.get_attr().await?.1.id, INO_UNKNOWN);
    assert_eq!(node.get_attr().await?.1.creation_time, 0);
    assert_eq!(node.get_attr().await?.1.modification_time, 0);

    // TODO(fxb/91610): The C++ bootfs VFS uses the wrong POSIX bits (needs S_IXUSR).
    let cpp_bootfs = MODE_TYPE_DIRECTORY | S_IRUSR;
    let rust_bootfs = MODE_TYPE_DIRECTORY | S_IRUSR | S_IXUSR;
    let actual_value = node.get_attr().await?.1.mode;
    assert!(actual_value == cpp_bootfs || actual_value == rust_bootfs);

    node::close(node).await?;

    Ok(())
}

#[fuchsia::test]
async fn check_kernel_vmos() -> Result<(), Error> {
    let directory = directory::open_in_namespace(
        KERNEL_VDSO_DIRECTORY,
        OPEN_RIGHT_READABLE | OPEN_RIGHT_EXECUTABLE,
    )
    .context("failed to open kernel vdso directory")?;
    let vdsos =
        files_async::readdir(&directory).await.context("failed to read kernel vdso directory")?;

    // We should have added at least the default VDSO.
    assert_ne!(vdsos.len(), 0);
    directory::close(directory).await?;

    // All VDSOs should have execution rights.
    for vdso in vdsos {
        let name = format!("{}/{}", KERNEL_VDSO_DIRECTORY, vdso.name);
        let file = file::open_in_namespace(&name, OPEN_RIGHT_READABLE | OPEN_RIGHT_EXECUTABLE)
            .context("failed to open file")?;
        let data = file::read_num_bytes(&file, 1).await.context(format!(
            "failed to read a single byte from a vdso opened as read-execute: {}",
            name
        ))?;
        assert_ne!(data.len(), 0);
        file::close(file).await?;
    }

    Ok(())
}

#[fuchsia::test]
async fn check_executable_files() -> Result<(), Error> {
    // Sanitizers nest lib files within '/boot/lib/asan' or '/boot/lib/asan-ubsan' etc., so
    // we need to just search recursively for these files instead.
    let directory =
        directory::open_in_namespace("/boot/lib", OPEN_RIGHT_READABLE | OPEN_RIGHT_EXECUTABLE)
            .context("failed to open /boot/lib directory")?;
    let lib_paths = files_async::readdir_recursive(&directory, None)
        .filter_map(|result| async {
            assert!(result.is_ok());
            let entry = result.unwrap();
            for file in BOOTFS_EXECUTABLE_LIB_FILES {
                if entry.name.ends_with(file) {
                    return Some(format!("/boot/lib/{}", entry.name));
                }
            }

            None
        })
        .collect::<Vec<String>>()
        .await;
    directory::close(directory).await?;

    // Should have found all of the library files.
    assert_eq!(lib_paths.len(), BOOTFS_EXECUTABLE_LIB_FILES.len());
    let paths = [
        lib_paths,
        BOOTFS_EXECUTABLE_NON_LIB_FILES.iter().map(|val| val.to_string()).collect::<Vec<_>>(),
    ]
    .concat();

    for path in paths {
        let file = file::open_in_namespace(&path, OPEN_RIGHT_READABLE | OPEN_RIGHT_EXECUTABLE)
            .context("failed to open file")?;
        let data = file::read_num_bytes(&file, 1).await.context(format!(
            "failed to read a single byte from a file opened as read-execute: {}",
            path
        ))?;
        assert_ne!(data.len(), 0);
        file::close(file).await?;
    }

    Ok(())
}

#[fuchsia::test]
async fn check_readonly_files() -> Result<(), Error> {
    // There is a large variation in what different products have in the data directory, so
    // just search it during the test time and find some files. Every file in the data directory
    // should be readonly.
    let directory = directory::open_in_namespace(
        BOOTFS_DATA_DIRECTORY,
        OPEN_RIGHT_READABLE | OPEN_RIGHT_EXECUTABLE,
    )
    .context("failed to open data directory")?;
    let data_paths = files_async::readdir_recursive(&directory, None)
        .filter_map(|result| async {
            assert!(result.is_ok());
            Some(format!("{}/{}", BOOTFS_DATA_DIRECTORY, result.unwrap().name))
        })
        .collect::<Vec<String>>()
        .await;
    directory::close(directory).await?;

    let paths =
        [data_paths, BOOTFS_READONLY_FILES.iter().map(|val| val.to_string()).collect::<Vec<_>>()]
            .concat();

    for path in paths {
        // A readonly file should not be usable when opened as executable.
        let mut file = file::open_in_namespace(&path, OPEN_RIGHT_READABLE | OPEN_RIGHT_EXECUTABLE)
            .context("failed to open file")?;
        let result = file::read_num_bytes(&file, 1).await;
        assert!(result.is_err());
        // Don't close the file proxy -- the access error above has already closed the channel.

        // Reopen as readonly, and confirm that it can be read from.
        file =
            file::open_in_namespace(&path, OPEN_RIGHT_READABLE).context("failed to open file")?;
        let data = file::read_num_bytes(&file, 1).await.context(format!(
            "failed to read a single byte from a file opened as readonly: {}",
            path
        ))?;
        assert_ne!(data.len(), 0);
        file::close(file).await?;
    }

    Ok(())
}
