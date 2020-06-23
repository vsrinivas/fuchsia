// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]
use {
    blobfs_ramdisk::BlobfsRamdisk,
    fuchsia_async as fasync,
    fuchsia_pkg_testing::{PackageBuilder, SystemImageBuilder},
    fuchsia_zircon::Status,
    pkgfs_ramdisk::PkgfsRamdisk,
    std::convert::{TryFrom as _, TryInto as _},
};

fn make_file_contents(size: usize) -> impl Iterator<Item = u8> {
    b"ABCD".iter().copied().cycle().take(size)
}

// zero is a multiple
fn round_up_to_4096_multiple(val: usize) -> usize {
    (val + 4095) & !4095
}

// meta far file VMOs are zero-padded to the smallest multiple of 4096
fn validate_vmo_contents(file_size: usize, vmo_contents: &[u8]) {
    let vmo_size = round_up_to_4096_multiple(file_size);
    assert!(
        make_file_contents(file_size)
            .chain(std::iter::repeat(b'\0'))
            .take(vmo_size)
            .eq(vmo_contents.iter().copied()),
        "vmo content mismatch for file size {}",
        file_size
    );
}

#[fasync::run_singlethreaded(test)]
async fn meta_far_file() {
    let file_sizes = [0, 1, 4095, 4096, 4097];
    let mut base_pkg_with_meta_files = PackageBuilder::new("example");
    for size in &file_sizes {
        base_pkg_with_meta_files = base_pkg_with_meta_files.add_resource_at(
            format!("meta/{}", size),
            make_file_contents(*size).collect::<Vec<u8>>().as_slice(),
        );
    }
    let base_pkg_with_meta_files = base_pkg_with_meta_files.build().await.expect("build package");
    let system_image =
        SystemImageBuilder::new().static_packages(&[&base_pkg_with_meta_files]).build().await;
    let blobfs = BlobfsRamdisk::start().unwrap();
    system_image.write_to_blobfs_dir(&blobfs.root_dir().unwrap());
    base_pkg_with_meta_files.write_to_blobfs_dir(&blobfs.root_dir().unwrap());
    let pkgfs = PkgfsRamdisk::builder()
        .blobfs(blobfs)
        .system_image_merkle(system_image.meta_far_merkle_root())
        .start()
        .unwrap();
    let d = pkgfs.root_dir_proxy().expect("getting pkgfs root dir");

    for size in &file_sizes {
        let meta_far_file = io_util::directory::open_file(
            &d,
            &format!("versions/{}/meta/{}", base_pkg_with_meta_files.meta_far_merkle_root(), size),
            io_util::OPEN_RIGHT_READABLE,
        )
        .await
        .unwrap();

        let (status, buffer) = meta_far_file
            .get_buffer(fidl_fuchsia_io::VMO_FLAG_READ | fidl_fuchsia_io::VMO_FLAG_PRIVATE)
            .await
            .unwrap();
        Status::ok(status).unwrap();
        let buffer = buffer.unwrap();

        assert_eq!(buffer.size, u64::try_from(*size).unwrap());
        let vmo_size = buffer.vmo.get_size().unwrap().try_into().unwrap();
        let mut actual_contents = vec![0u8; vmo_size];
        let () = buffer.vmo.read(actual_contents.as_mut_slice(), 0).unwrap();
        validate_vmo_contents(*size, &actual_contents);
    }

    // Drop the directory before we shutdown the server that's serving it.
    // In practice, this probably doesn't make a difference.
    drop(d);

    pkgfs.stop().await.expect("stopping pkgfs");
}
