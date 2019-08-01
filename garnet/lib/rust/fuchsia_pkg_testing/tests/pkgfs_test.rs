// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await)]
#![cfg(test)]
use {
    failure::Error,
    failure::ResultExt,
    fuchsia_async as fasync,
    fuchsia_pkg_testing::{pkgfs::TestPkgFs, PackageBuilder},
    std::io::{Read, Write},
};

fn ls_simple(d: openat::DirIter) -> Result<Vec<String>, Error> {
    Ok(d.map(|i| i.map(|entry| entry.file_name().to_string_lossy().into()))
        .collect::<Result<Vec<_>, _>>()?)
}

#[fasync::run_singlethreaded(test)]
async fn test_pkgfs_short_write() -> Result<(), Error> {
    let pkgfs = TestPkgFs::start(None).context("starting pkgfs")?;
    let blobfs_root_dir = pkgfs.blobfs_root_dir()?;
    let d = pkgfs.root_dir().context("getting pkgfs root dir")?;

    let pkg = PackageBuilder::new("example")
        .add_resource_at("a/b", "Hello world!\n".as_bytes())
        .expect("add resource")
        .build()
        .await
        .expect("build package");
    assert_eq!(
        pkg.meta_far_merkle_root(),
        &"b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93"
            .parse::<fuchsia_merkle::Hash>()
            .unwrap()
    );

    let mut meta_far = pkg.meta_far().expect("meta.far");
    {
        let mut to_write = d
            .new_file(
                "install/pkg/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93",
                0600,
            )
            .expect("create install file");
        to_write.set_len(meta_far.metadata().unwrap().len()).expect("truncate meta.far");
        std::io::copy(&mut meta_far, &mut to_write).expect("write meta.far");
    }
    assert_eq!(
        ls_simple(
            d.list_dir(
                "needs/packages/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93"
            )
            .expect("list dir")
        )
        .expect("list dir contents"),
        ["e5892a9b652ede2e19460a9103fd9cb3417f782a8d29f6c93ec0c31170a94af3"]
    );

    // Short blob write
    {
        let mut blob_install = d
            .new_file(
                "install/blob/e5892a9b652ede2e19460a9103fd9cb3417f782a8d29f6c93ec0c31170a94af3",
                0600,
            )
            .expect("create blob install file");
        let blob_contents = b"Hello world!\n";
        blob_install.set_len(blob_contents.len() as u64).expect("truncate blob");
        blob_install.write_all(b"Hello world!").expect("write blob");
    }

    // Blob still needed
    assert_eq!(
        ls_simple(
            d.list_dir(
                "needs/packages/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93"
            )
            .expect("list dir")
        )
        .expect("list dir contents"),
        ["e5892a9b652ede2e19460a9103fd9cb3417f782a8d29f6c93ec0c31170a94af3"]
    );

    // Full blob write
    {
        let mut blob_install = d
            .new_file(
                "install/blob/e5892a9b652ede2e19460a9103fd9cb3417f782a8d29f6c93ec0c31170a94af3",
                0600,
            )
            .expect("create blob install file");
        let blob_contents = b"Hello world!\n";
        blob_install.set_len(blob_contents.len() as u64).expect("truncate blob");
        blob_install.write_all(blob_contents).expect("write blob");
    }

    // Blob needs no more packages
    assert_eq!(
        d.list_dir(
            "needs/packages/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93"
        )
        .expect_err("check empty needs dir")
        .kind(),
        std::io::ErrorKind::NotFound
    );

    let mut file_contents = String::new();
    d.open_file("packages/example/0/a/b")
        .expect("read package file")
        .read_to_string(&mut file_contents)
        .expect("read package file");
    assert_eq!(&file_contents, "Hello world!\n");
    let mut file_contents = String::new();
    d.open_file("versions/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93/a/b")
        .expect("read package file")
        .read_to_string(&mut file_contents)
        .expect("read package file");
    assert_eq!(&file_contents, "Hello world!\n");

    assert_eq!(
        ls_simple(blobfs_root_dir.list_dir(".").expect("list dir")).expect("list dir contents"),
        [
            "b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93",
            "e5892a9b652ede2e19460a9103fd9cb3417f782a8d29f6c93ec0c31170a94af3"
        ],
    );

    drop(d);

    pkgfs.stop().await?;

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_pkgfs_restart_install() -> Result<(), Error> {
    let pkgfs = TestPkgFs::start(None).context("starting pkgfs")?;
    let blobfs_root_dir = pkgfs.blobfs_root_dir()?;
    let d = pkgfs.root_dir().context("getting pkgfs root dir")?;

    let pkg = PackageBuilder::new("example")
        .add_resource_at("a/b", "Hello world!\n".as_bytes())
        .expect("add resource")
        .build()
        .await
        .expect("build package");
    assert_eq!(
        pkg.meta_far_merkle_root(),
        &"b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93"
            .parse::<fuchsia_merkle::Hash>()
            .unwrap()
    );

    // Start package install
    // first, some checks to see if it's already installed
    assert_eq!(
        d.metadata("versions/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93")
            .map(|m| m.is_dir())
            .map_err(|e| e.kind()),
        Err(std::io::ErrorKind::NotFound)
    );
    assert_eq!(
        d.metadata(
            "needs/packages/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93"
        )
        .map(|m| m.is_dir())
        .map_err(|e| e.kind()),
        Err(std::io::ErrorKind::NotFound)
    );
    // Install the meta.far
    {
        let mut meta_far = pkg.meta_far().expect("meta.far");
        let mut to_write = d
            .new_file(
                "install/pkg/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93",
                0600,
            )
            .expect("create install file");
        to_write.set_len(meta_far.metadata().unwrap().len()).expect("truncate meta.far");
        std::io::copy(&mut meta_far, &mut to_write).expect("write meta.far");
    }
    assert_eq!(
        ls_simple(
            d.list_dir(
                "needs/packages/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93"
            )
            .expect("list dir")
        )
        .expect("list dir contents"),
        ["e5892a9b652ede2e19460a9103fd9cb3417f782a8d29f6c93ec0c31170a94af3"]
    );

    // Short blob write
    {
        let mut blob_install = d
            .new_file(
                "install/blob/e5892a9b652ede2e19460a9103fd9cb3417f782a8d29f6c93ec0c31170a94af3",
                0600,
            )
            .expect("create blob install file");
        let blob_contents = b"Hello world!\n";
        blob_install.set_len(blob_contents.len() as u64).expect("truncate blob");
        blob_install.write_all(b"Hello world!").expect("write blob");
    }

    // Blob still needed
    assert_eq!(
        ls_simple(
            d.list_dir(
                "needs/packages/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93"
            )
            .expect("list dir")
        )
        .expect("list dir contents"),
        ["e5892a9b652ede2e19460a9103fd9cb3417f782a8d29f6c93ec0c31170a94af3"]
    );

    // Restart pkgfs (without dynamic index)
    drop(d);
    let pkgfs = pkgfs.restart().context("restarting pkgfs")?;
    let d = pkgfs.root_dir().context("getting pkgfs root dir")?;

    // Restart package install
    assert_eq!(
        d.metadata("versions/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93")
            .map(|m| m.is_dir())
            .map_err(|e| e.kind()),
        Err(std::io::ErrorKind::NotFound)
    );
    assert_eq!(
        d.metadata(
            "needs/packages/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93"
        )
        .map(|m| m.is_dir())
        .map_err(|e| e.kind()),
        Err(std::io::ErrorKind::NotFound)
    );

    // Retry installing meta.far (fails with EEXIST)
    assert_eq!(
        d.new_file(
            "install/pkg/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93",
            0600,
        )
        .map(|_| ())
        .map_err(|e| e.kind()),
        Err(std::io::ErrorKind::AlreadyExists)
    );

    assert_eq!(
        d.metadata("versions/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93")
            .map(|m| m.is_dir())
            .map_err(|e| e.kind()),
        Err(std::io::ErrorKind::NotFound)
    );
    // Check needs again.
    assert_eq!(
        d.metadata(
            "needs/packages/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93"
        )
        .map(|m| m.is_dir())
        .map_err(|e| e.kind()),
        Ok(true)
    );

    // Needs exists, so we don't need to worry about meta.far write having failed.

    assert_eq!(
        ls_simple(
            d.list_dir(
                "needs/packages/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93"
            )
            .expect("list dir")
        )
        .expect("list dir contents"),
        ["e5892a9b652ede2e19460a9103fd9cb3417f782a8d29f6c93ec0c31170a94af3"]
    );

    // Full blob write
    {
        let mut blob_install = d
            .new_file(
                "install/blob/e5892a9b652ede2e19460a9103fd9cb3417f782a8d29f6c93ec0c31170a94af3",
                0600,
            )
            .expect("create blob install file");
        let blob_contents = b"Hello world!\n";
        blob_install.set_len(blob_contents.len() as u64).expect("truncate blob");
        blob_install.write_all(blob_contents).expect("write blob");
    }

    // Blob Needs no more packages
    assert_eq!(
        d.list_dir(
            "needs/packages/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93"
        )
        .expect_err("check empty needs dir")
        .kind(),
        std::io::ErrorKind::NotFound
    );

    let mut file_contents = String::new();
    d.open_file("packages/example/0/a/b")
        .expect("read package file")
        .read_to_string(&mut file_contents)
        .expect("read package file");
    assert_eq!(&file_contents, "Hello world!\n");
    let mut file_contents = String::new();
    d.open_file("versions/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93/a/b")
        .expect("read versions file")
        .read_to_string(&mut file_contents)
        .expect("read versions file");
    assert_eq!(&file_contents, "Hello world!\n");

    assert_eq!(
        ls_simple(blobfs_root_dir.list_dir(".").expect("list dir")).expect("list dir contents"),
        [
            "b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93",
            "e5892a9b652ede2e19460a9103fd9cb3417f782a8d29f6c93ec0c31170a94af3"
        ],
    );

    drop(d);

    pkgfs.stop().await?;

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_pkgfs_restart_install_already_done() -> Result<(), Error> {
    let pkgfs = TestPkgFs::start(None).context("starting pkgfs")?;
    let blobfs_root_dir = pkgfs.blobfs_root_dir()?;
    let d = pkgfs.root_dir().context("getting pkgfs root dir")?;

    let pkg = PackageBuilder::new("example")
        .add_resource_at("a/b", "Hello world!\n".as_bytes())
        .expect("add resource")
        .build()
        .await
        .expect("build package");
    assert_eq!(
        pkg.meta_far_merkle_root(),
        &"b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93"
            .parse::<fuchsia_merkle::Hash>()
            .unwrap()
    );

    // Start package install
    // first, some checks to see if it's already installed
    assert_eq!(
        d.metadata("versions/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93")
            .map(|m| m.is_dir())
            .map_err(|e| e.kind()),
        Err(std::io::ErrorKind::NotFound)
    );
    assert_eq!(
        d.metadata(
            "needs/packages/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93"
        )
        .map(|m| m.is_dir())
        .map_err(|e| e.kind()),
        Err(std::io::ErrorKind::NotFound)
    );
    // Install the meta.far
    {
        let mut meta_far = pkg.meta_far().expect("meta.far");
        let mut to_write = d
            .new_file(
                "install/pkg/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93",
                0600,
            )
            .expect("create install file");
        to_write.set_len(meta_far.metadata().unwrap().len()).expect("truncate meta.far");
        std::io::copy(&mut meta_far, &mut to_write).expect("write meta.far");
    }
    assert_eq!(
        ls_simple(
            d.list_dir(
                "needs/packages/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93"
            )
            .expect("list dir")
        )
        .expect("list dir contents"),
        ["e5892a9b652ede2e19460a9103fd9cb3417f782a8d29f6c93ec0c31170a94af3"]
    );

    // blob write direct to blobfs
    {
        let mut blob_install = blobfs_root_dir
            .new_file("e5892a9b652ede2e19460a9103fd9cb3417f782a8d29f6c93ec0c31170a94af3", 0600)
            .expect("create blob install file");
        let blob_contents = b"Hello world!\n";
        blob_install.set_len(blob_contents.len() as u64).expect("truncate blob");
        blob_install.write_all(blob_contents).expect("write blob");
    }

    // Blob still needed
    assert_eq!(
        ls_simple(
            d.list_dir(
                "needs/packages/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93"
            )
            .expect("list dir")
        )
        .expect("list dir contents"),
        ["e5892a9b652ede2e19460a9103fd9cb3417f782a8d29f6c93ec0c31170a94af3"]
    );

    // Restart pkgfs (without dynamic index)
    drop(d);
    let pkgfs = pkgfs.restart().context("restarting pkgfs")?;
    let d = pkgfs.root_dir().context("getting pkgfs root dir")?;

    // Restart package install
    assert_eq!(
        d.metadata("versions/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93")
            .map(|m| m.is_dir())
            .map_err(|e| e.kind()),
        Err(std::io::ErrorKind::NotFound)
    );
    assert_eq!(
        d.metadata(
            "needs/packages/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93"
        )
        .map(|m| m.is_dir())
        .map_err(|e| e.kind()),
        Err(std::io::ErrorKind::NotFound)
    );

    // Retry installing meta.far (fails with Invalid argument)
    d.new_file(
        "install/pkg/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93",
        0600,
    )
    .expect_err("already exists");

    // Recheck versions
    assert_eq!(
        d.metadata("versions/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93")
            .map(|m| m.is_dir())
            .map_err(|e| e.kind()),
        Ok(true)
    );

    // Check needs again.
    assert_eq!(
        d.metadata(
            "needs/packages/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93"
        )
        .map(|m| m.is_dir())
        .map_err(|e| e.kind()),
        Err(std::io::ErrorKind::NotFound)
    );

    let mut file_contents = String::new();
    d.open_file("packages/example/0/a/b")
        .expect("read package file")
        .read_to_string(&mut file_contents)
        .expect("read package file");
    assert_eq!(&file_contents, "Hello world!\n");
    let mut file_contents = String::new();
    d.open_file("versions/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93/a/b")
        .expect("read package file")
        .read_to_string(&mut file_contents)
        .expect("read package file");
    assert_eq!(&file_contents, "Hello world!\n");

    assert_eq!(
        ls_simple(blobfs_root_dir.list_dir(".").expect("list dir")).expect("list dir contents"),
        [
            "b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93",
            "e5892a9b652ede2e19460a9103fd9cb3417f782a8d29f6c93ec0c31170a94af3"
        ],
    );

    drop(d);

    pkgfs.stop().await?;

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_pkgfs_restart_install_failed_meta_far() -> Result<(), Error> {
    let pkgfs = TestPkgFs::start(None).context("starting pkgfs")?;
    let blobfs_root_dir = pkgfs.blobfs_root_dir()?;
    let d = pkgfs.root_dir().context("getting pkgfs root dir")?;

    let pkg = PackageBuilder::new("example")
        .add_resource_at("a/b", "Hello world!\n".as_bytes())
        .expect("add resource")
        .build()
        .await
        .expect("build package");
    assert_eq!(
        pkg.meta_far_merkle_root(),
        &"b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93"
            .parse::<fuchsia_merkle::Hash>()
            .unwrap()
    );

    // Start package install
    // first, some checks to see if it's already installed
    assert_eq!(
        d.metadata("versions/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93")
            .map(|m| m.is_dir())
            .map_err(|e| e.kind()),
        Err(std::io::ErrorKind::NotFound)
    );
    assert_eq!(
        d.metadata(
            "needs/packages/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93"
        )
        .map(|m| m.is_dir())
        .map_err(|e| e.kind()),
        Err(std::io::ErrorKind::NotFound)
    );

    // Create (but don't write) the meta.far
    d.new_file(
        "install/pkg/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93",
        0600,
    )
    .expect("create install file");

    assert_eq!(
        d.metadata("versions/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93")
            .map(|m| m.is_dir())
            .map_err(|e| e.kind()),
        Err(std::io::ErrorKind::NotFound)
    );

    assert_eq!(
        ls_simple(d.list_dir("needs/packages").expect("list dir")).expect("list dir contents"),
        ["b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93"]
    );

    assert_eq!(
        ls_simple(
            d.list_dir(
                "needs/packages/b5690901cd8664a742eb0a7d2a068eb0d4ff49c10a615cfa4c0044dd2eaccd93"
            )
            .expect("list dir")
        )
        .expect("list dir contents"),
        Vec::<&str>::new()
    );

    assert_eq!(
        ls_simple(blobfs_root_dir.list_dir(".").expect("list dir")).expect("list dir contents"),
        Vec::<&str>::new()
    );

    drop(d);

    pkgfs.stop().await?;

    Ok(())
}
