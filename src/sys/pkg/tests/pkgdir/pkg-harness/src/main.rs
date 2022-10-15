// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Error},
    blobfs_ramdisk::BlobfsRamdisk,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io as fio,
    fidl_test_fidl_pkg::{Backing, HarnessRequest, HarnessRequestStream},
    fuchsia_async::Task,
    fuchsia_component::server::ServiceFs,
    fuchsia_pkg_testing::{Package, PackageBuilder, SystemImageBuilder},
    futures::prelude::*,
    std::convert::TryInto,
    tracing::{error, info},
};

#[fuchsia::main(logging_tags = ["pkg-harness"])]
async fn main() -> Result<(), Error> {
    main_inner().await.map_err(|err| {
        // Use anyhow to print the error chain.
        let err = anyhow!(err);
        error!("error running pkg-harness: {:#}", err);
        err
    })
}

async fn main_inner() -> Result<(), Error> {
    info!("starting pkg-harness");

    // Spin up a blobfs and install the test package.
    let test_package = make_test_package().await;
    let system_image_package =
        SystemImageBuilder::new().static_packages(&[&test_package]).build().await;
    let blobfs = BlobfsRamdisk::start().expect("started blobfs");
    let blobfs_root_dir = blobfs.root_dir().expect("getting blobfs root dir");
    test_package.write_to_blobfs_dir(&blobfs_root_dir);
    system_image_package.write_to_blobfs_dir(&blobfs_root_dir);

    let blobfs_client = blobfs.client();

    let (pkgdir_backed_package, dir_request) = fidl::endpoints::create_proxy().unwrap();
    package_directory::serve(
        vfs::execution_scope::ExecutionScope::new(),
        blobfs_client,
        *test_package.meta_far_merkle_root(),
        fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_EXECUTABLE,
        dir_request,
    )
    .await
    .expect("serving test_package with pkgdir");

    // Set up serving FIDL to expose the test package.
    enum IncomingService {
        Harness(HarnessRequestStream),
    }
    let mut fs = ServiceFs::new();
    fs.take_and_serve_directory_handle().context("while serving directory handle")?;
    fs.dir("svc").add_fidl_service(IncomingService::Harness);
    let () = fs
        .for_each_concurrent(None, move |svc| {
            match svc {
                IncomingService::Harness(stream) => Task::spawn(
                    serve_harness(stream, Clone::clone(&pkgdir_backed_package))
                        .map(|res| res.context("while serving test.fidl.pkg.Harness")),
                ),
            }
            .unwrap_or_else(|e| {
                error!("error handling fidl connection: {:#}", anyhow!(e));
            })
        })
        .await;

    Ok(())
}

/// Serve test.fidl.pkg.Harness.
async fn serve_harness(
    mut stream: HarnessRequestStream,
    pkgdir_backed_package: fio::DirectoryProxy,
) -> Result<(), Error> {
    while let Some(event) = stream.try_next().await.context("while pulling next event")? {
        let HarnessRequest::ConnectPackage { backing, dir, responder } = event;
        let pkg = match backing {
            Backing::Pkgdir => &pkgdir_backed_package,
        };

        let () = pkg
            .clone(fio::OpenFlags::CLONE_SAME_RIGHTS, ServerEnd::new(dir.into_channel()))
            .expect("clone to succeed");

        responder.send(&mut Ok(())).context("while sending success response")?;
    }
    Ok(())
}

/// Constructs a test package to be used in the integration tests.
async fn make_test_package() -> Package {
    let exceeds_max_buf_contents = repeat_by_n('a', (fio::MAX_BUF + 1).try_into().unwrap());

    // Each file's contents is the file's path as bytes for testing simplicity.
    let mut builder = PackageBuilder::new("test-package")
        .add_resource_at("file", "file".as_bytes())
        .add_resource_at("meta/file", "meta/file".as_bytes())
        // For use in testing Directory.Open calls with segmented paths.
        .add_resource_at("dir/file", "dir/file".as_bytes())
        .add_resource_at("dir/dir/file", "dir/dir/file".as_bytes())
        .add_resource_at("dir/dir/dir/file", "dir/dir/dir/file".as_bytes())
        .add_resource_at("meta/dir/file", "meta/dir/file".as_bytes())
        .add_resource_at("meta/dir/dir/file", "meta/dir/dir/file".as_bytes())
        .add_resource_at("meta/dir/dir/dir/file", "meta/dir/dir/dir/file".as_bytes())
        // For use in testing File.Read calls where the file contents exceeds MAX_BUF.
        .add_resource_at("exceeds_max_buf", exceeds_max_buf_contents.as_bytes())
        .add_resource_at("meta/exceeds_max_buf", exceeds_max_buf_contents.as_bytes());

    // For use in testing File.GetBackingMemory on different file sizes.
    let file_sizes = [0, 1, 4095, 4096, 4097];
    for size in &file_sizes {
        builder = builder
            .add_resource_at(
                format!("file_{}", size),
                make_file_contents(*size).collect::<Vec<u8>>().as_slice(),
            )
            .add_resource_at(
                format!("meta/file_{}", size),
                make_file_contents(*size).collect::<Vec<u8>>().as_slice(),
            );
    }

    // Make directory nodes of each kind (root dir, non-meta subdir, meta dir, meta subdir)
    // that overflow the fuchsia.io/Directory.ReadDirents buffer.
    for base in ["", "dir_overflow_readdirents/", "meta/", "meta/dir_overflow_readdirents/"] {
        // In the integration tests, we'll want to be able to test calling ReadDirents on a
        // directory. Since ReadDirents returns `MAX_BUF` bytes worth of directory entries, we need
        // to have test coverage for the "overflow" case where the directory has more than
        // `MAX_BUF` bytes worth of directory entries.
        //
        // Through math, we determine that we can achieve this overflow with 31 files whose names
        // are length `MAX_FILENAME`. Here is this math:
        /*
           ReadDirents -> vector<uint8>:MAX_BUF

           MAX_BUF = 8192

           struct dirent {
            // Describes the inode of the entry.
            uint64 ino;
            // Describes the length of the dirent name in bytes.
            uint8 size;
            // Describes the type of the entry. Aligned with the
            // POSIX d_type values. Use `DIRENT_TYPE_*` constants.
            uint8 type;
            // Unterminated name of entry.
            char name[0];
           }

           sizeof(dirent) if name is MAX_FILENAME = 255 bytes long = 8 + 1 + 1 + 255 = 265 bytes

           8192 / 265 ~= 30.9

           => 31 directory entries of maximum size will trigger overflow
        */
        for seed in ('a'..='z').chain('A'..='E') {
            builder = builder.add_resource_at(
                format!("{}{}", base, repeat_by_n(seed, fio::MAX_FILENAME.try_into().unwrap())),
                &b""[..],
            )
        }
    }
    builder.build().await.expect("build package")
}

fn repeat_by_n(seed: char, n: usize) -> String {
    std::iter::repeat(seed).take(n).collect()
}

fn make_file_contents(size: usize) -> impl Iterator<Item = u8> {
    b"ABCD".iter().copied().cycle().take(size)
}
