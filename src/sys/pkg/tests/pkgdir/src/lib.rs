// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::endpoints::create_proxy,
    fidl_fuchsia_io::{DirectoryMarker, DirectoryProxy},
    fidl_test_fidl_pkg::{Backing, ConnectError, HarnessMarker},
    fuchsia_component::client::connect_to_protocol,
    fuchsia_zircon as zx,
};

mod open;
mod read;

async fn dirs_to_test() -> impl Iterator<Item = DirectoryProxy> {
    let proxy = connect_to_protocol::<HarnessMarker>().unwrap();
    let (pkgfs_package, server) = create_proxy::<DirectoryMarker>().unwrap();
    let () = proxy.connect_package(Backing::Pkgfs, server).await.unwrap().unwrap();
    // TODO(fxbug.dev/75481): include a pkgdir backed package as well
    vec![pkgfs_package].into_iter()
}

// TODO(fxbug.dev/75481): support pkgdir-backed packages and delete this test.
#[fuchsia::test]
async fn unsupported_backing() {
    let harness = connect_to_protocol::<HarnessMarker>().unwrap();
    let (_dir, server) = create_proxy::<DirectoryMarker>().unwrap();

    let res = harness.connect_package(Backing::Pkgdir, server).await.unwrap();

    assert_eq!(res, Err(ConnectError::UnsupportedBacking));
}

/// Verify the overflow case is being hit on ReadDirents.
/// Note: we considered making this a unit test for pkg-harness, but opted to include this in the
/// integration tests so all the test cases are in one place.
#[fuchsia::test]
async fn overflow() {
    let harness = connect_to_protocol::<HarnessMarker>().unwrap();
    let (dir, server) = create_proxy::<DirectoryMarker>().unwrap();

    let () = harness.connect_package(Backing::Pkgfs, server).await.unwrap().unwrap();

    // Verify it takes three ReadDirents calls to read the directory entries.
    let (status, buf) = dir.read_dirents(fidl_fuchsia_io::MAX_BUF).await.unwrap();
    zx::Status::ok(status).expect("status ok");
    assert!(!buf.is_empty(), "first call should yield non-empty buffer");

    let (status, buf) = dir.read_dirents(fidl_fuchsia_io::MAX_BUF).await.unwrap();
    zx::Status::ok(status).expect("status ok");
    assert!(!buf.is_empty(), "second call should yield non-empty buffer");

    let (status, buf) = dir.read_dirents(fidl_fuchsia_io::MAX_BUF).await.unwrap();
    zx::Status::ok(status).expect("status ok");
    assert!(buf.is_empty(), "third call should yield empty buffer");
}
