// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::AsHandleRef,
    fidl_fuchsia_io as fio, fidl_fuchsia_io_test as io_test, fuchsia_async as fasync,
    fuchsia_zircon as zx,
    io_conformance_util::{test_harness::TestHarness, *},
};

#[fasync::run_singlethreaded(test)]
async fn file_get_readable_memory_with_sufficient_rights() {
    let harness = TestHarness::new().await;
    if !harness.config.supports_get_backing_memory.unwrap_or_default() {
        return;
    }

    for file_flags in harness.vmo_file_rights.valid_combos_with(fio::OpenFlags::RIGHT_READABLE) {
        // Should be able to get a readable VMO in default, exact, and private sharing modes.
        for sharing_mode in
            [fio::VmoFlags::empty(), fio::VmoFlags::SHARED_BUFFER, fio::VmoFlags::PRIVATE_CLONE]
        {
            let file = vmo_file(TEST_FILE, TEST_FILE_CONTENTS, 128 * 1024);
            let (vmo, _) = create_file_and_get_backing_memory(
                file,
                &harness,
                file_flags,
                fio::VmoFlags::READ | sharing_mode,
            )
            .await
            .expect("Failed to create file and obtain VMO");

            // Ensure that the returned VMO's rights are consistent with the expected flags.
            validate_vmo_rights(&vmo, fio::VmoFlags::READ);

            let size = vmo.get_content_size().expect("Failed to get vmo content size");

            // Check contents of buffer.
            let mut data = vec![0; size as usize];
            let () = vmo.read(&mut data, 0).expect("VMO read failed");
            assert_eq!(&data, TEST_FILE_CONTENTS);
        }
    }
}

#[fasync::run_singlethreaded(test)]
async fn file_get_readable_memory_with_insufficient_rights() {
    let harness = TestHarness::new().await;
    if !harness.config.supports_get_backing_memory.unwrap_or_default() {
        return;
    }

    for file_flags in harness.vmo_file_rights.valid_combos_without(fio::OpenFlags::RIGHT_READABLE) {
        let file = vmo_file(TEST_FILE, TEST_FILE_CONTENTS, 128 * 1024);
        assert_eq!(
            create_file_and_get_backing_memory(file, &harness, file_flags, fio::VmoFlags::READ)
                .await
                .expect_err("Error was expected"),
            zx::Status::ACCESS_DENIED
        );
    }
}

#[fasync::run_singlethreaded(test)]
async fn file_get_writable_memory_with_sufficient_rights() {
    let harness = TestHarness::new().await;
    if !harness.config.supports_get_backing_memory.unwrap_or_default() {
        return;
    }
    // Writable VMOs currently require private sharing mode.
    const VMO_FLAGS: fio::VmoFlags =
        fio::VmoFlags::empty().union(fio::VmoFlags::WRITE).union(fio::VmoFlags::PRIVATE_CLONE);

    for file_flags in harness.vmo_file_rights.valid_combos_with(fio::OpenFlags::RIGHT_WRITABLE) {
        let file = vmo_file(TEST_FILE, TEST_FILE_CONTENTS, 128 * 1024);
        let (vmo, _) = create_file_and_get_backing_memory(file, &harness, file_flags, VMO_FLAGS)
            .await
            .expect("Failed to create file and obtain VMO");

        // Ensure that the returned VMO's rights are consistent with the expected flags.
        validate_vmo_rights(&vmo, VMO_FLAGS);

        // Ensure that we can actually write to the VMO.
        let () = vmo.write("bbbbb".as_bytes(), 0).expect("vmo write failed");
    }
}

#[fasync::run_singlethreaded(test)]
async fn file_get_writable_memory_with_insufficient_rights() {
    let harness = TestHarness::new().await;
    if !harness.config.supports_get_backing_memory.unwrap_or_default() {
        return;
    }
    const VMO_FLAGS: fio::VmoFlags =
        fio::VmoFlags::empty().union(fio::VmoFlags::WRITE).union(fio::VmoFlags::PRIVATE_CLONE);

    for file_flags in harness.vmo_file_rights.valid_combos_without(fio::OpenFlags::RIGHT_WRITABLE) {
        let file = vmo_file(TEST_FILE, TEST_FILE_CONTENTS, 128 * 1024);
        assert_eq!(
            create_file_and_get_backing_memory(file, &harness, file_flags, VMO_FLAGS)
                .await
                .expect_err("Error was expected"),
            zx::Status::ACCESS_DENIED
        );
    }
}

#[fasync::run_singlethreaded(test)]
async fn file_get_executable_memory_with_sufficient_rights() {
    let harness = TestHarness::new().await;
    if !harness.config.supports_get_backing_memory.unwrap_or_default()
        || !harness.config.supports_executable_file.unwrap_or_default()
    {
        return;
    }

    // We should be able to get an executable VMO in default, exact, and private sharing modes. Note
    // that the fuchsia.io interface requires the connection to have OPEN_RIGHT_READABLE in addition
    // to OPEN_RIGHT_EXECUTABLE if passing VmoFlags::EXECUTE to the GetBackingMemory method.
    for sharing_mode in
        [fio::VmoFlags::empty(), fio::VmoFlags::SHARED_BUFFER, fio::VmoFlags::PRIVATE_CLONE]
    {
        let file = executable_file(TEST_FILE);
        let vmo_flags = fio::VmoFlags::READ | fio::VmoFlags::EXECUTE | sharing_mode;
        let (vmo, _) = create_file_and_get_backing_memory(
            file,
            &harness,
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_EXECUTABLE,
            vmo_flags,
        )
        .await
        .expect("Failed to create file and obtain VMO");
        // Ensure that the returned VMO's rights are consistent with the expected flags.
        validate_vmo_rights(&vmo, vmo_flags);
    }
}

#[fasync::run_singlethreaded(test)]
async fn file_get_executable_memory_with_insufficient_rights() {
    let harness = TestHarness::new().await;
    if !harness.config.supports_get_backing_memory.unwrap_or_default()
        || !harness.config.supports_executable_file.unwrap_or_default()
    {
        return;
    }
    // We should fail to get the backing memory if the connection lacks execute rights.
    for file_flags in
        harness.executable_file_rights.valid_combos_without(fio::OpenFlags::RIGHT_EXECUTABLE)
    {
        let file = executable_file(TEST_FILE);
        assert_eq!(
            create_file_and_get_backing_memory(file, &harness, file_flags, fio::VmoFlags::EXECUTE)
                .await
                .expect_err("Error was expected"),
            zx::Status::ACCESS_DENIED
        );
    }
    // The fuchsia.io interface additionally specifies that GetBackingMemory should fail if
    // VmoFlags::EXECUTE is specified but connection lacks OPEN_RIGHT_READABLE.
    for file_flags in
        harness.executable_file_rights.valid_combos_without(fio::OpenFlags::RIGHT_READABLE)
    {
        let file = executable_file(TEST_FILE);
        assert_eq!(
            create_file_and_get_backing_memory(file, &harness, file_flags, fio::VmoFlags::EXECUTE)
                .await
                .expect_err("Error was expected"),
            zx::Status::ACCESS_DENIED
        );
    }
}

// Ensure that passing VmoFlags::SHARED_BUFFER to GetBackingMemory returns the same KOID as the
// backing VMO.
#[fasync::run_singlethreaded(test)]
async fn file_get_backing_memory_exact_same_koid() {
    let harness = TestHarness::new().await;
    if !harness.config.supports_get_backing_memory.unwrap_or_default() {
        return;
    }

    let vmo = zx::Vmo::create(1).expect("Cannot create VMO");
    let original_koid = vmo.get_koid();
    let vmofile_object = io_test::DirectoryEntry::VmoFile(io_test::VmoFile {
        name: Some(TEST_FILE.to_string()),
        vmo: Some(vmo),
        ..io_test::VmoFile::EMPTY
    });

    let (vmo, _) = create_file_and_get_backing_memory(
        vmofile_object,
        &harness,
        fio::OpenFlags::RIGHT_READABLE,
        fio::VmoFlags::READ | fio::VmoFlags::SHARED_BUFFER,
    )
    .await
    .expect("Failed to create file and obtain VMO");

    assert_eq!(original_koid, vmo.get_koid());
}
