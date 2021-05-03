// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::endpoints::create_endpoints,
    fidl_fuchsia_io::DirectoryMarker,
    fidl_test_fidl_pkg::{Backing, ConnectError, HarnessMarker},
    fuchsia_component::client::connect_to_protocol,
};

fn ls_simple(d: openat::DirIter) -> Result<Vec<String>, std::io::Error> {
    Ok(d.map(|i| i.map(|entry| entry.file_name().to_string_lossy().into()))
        .collect::<Result<Vec<_>, _>>()?)
}

#[fuchsia::test]
async fn success() {
    let proxy = connect_to_protocol::<HarnessMarker>().unwrap();
    let (client, server) = create_endpoints::<DirectoryMarker>().unwrap();

    let () = proxy.connect_package(Backing::Pkgfs, server).await.unwrap().unwrap();

    let d: openat::Dir = fdio::create_fd(client.into()).expect("while creating fd");
    assert_eq!(
        ls_simple(d.list_dir(".").expect("list dir")).unwrap(),
        vec!["meta".to_string(), "data".to_string()]
    );
}

// TODO(fxbug.dev/75481): support pkgdir-backed packages and delete this test.
#[fuchsia::test]
async fn unsupported_backing() {
    let proxy = connect_to_protocol::<HarnessMarker>().unwrap();
    let (_client, server) = create_endpoints::<DirectoryMarker>().unwrap();

    let res = proxy.connect_package(Backing::Pkgdir, server).await.unwrap();

    assert_eq!(res, Err(ConnectError::UnsupportedBacking));
}
