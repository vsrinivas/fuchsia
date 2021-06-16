// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::endpoints::create_proxy,
    fidl_fuchsia_io::{DirectoryMarker, DirectoryProxy},
    fidl_test_fidl_pkg::{Backing, ConnectError, HarnessMarker},
    fuchsia_component::client::connect_to_protocol,
};

mod directory;
mod file;

fn repeat_by_n(seed: char, n: usize) -> String {
    std::iter::repeat(seed).take(n).collect()
}

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
