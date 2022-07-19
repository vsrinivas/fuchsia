// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_dash as fdash;
use fuchsia_async as fasync;
use fuchsia_component::client::connect_to_protocol;
use fuchsia_zircon as zx;
use futures::prelude::*;

#[fuchsia::test]
pub async fn spawn_dash_self() {
    let (stdio, stdio_server) = zx::Socket::create(zx::SocketOpts::STREAM).unwrap();

    let launcher = connect_to_protocol::<fdash::LauncherMarker>().unwrap();
    launcher.launch_with_socket(".", stdio_server).await.unwrap().unwrap();

    let mut stdio = fasync::Socket::from_socket(stdio).unwrap();
    let mut buf = [0u8, 0u8];
    stdio.read_exact(&mut buf).await.unwrap();

    // The dash process prints the interactive prompt "$ "
    assert_eq!(buf, "$ ".as_bytes());

    stdio.write_all("ls".as_bytes()).await.unwrap();
    stdio.read_exact(&mut buf).await.unwrap();

    // The dash process prints back the typed characters "ls"
    assert_eq!(buf, "ls".as_bytes());
}

#[fuchsia::test]
pub async fn bad_moniker() {
    let (_stdio, stdio_server) = zx::Socket::create(zx::SocketOpts::STREAM).unwrap();

    let launcher = connect_to_protocol::<fdash::LauncherMarker>().unwrap();

    // Give a string that won't parse correctly as a moniker
    let err = launcher.launch_with_socket("!@#$%^&*(", stdio_server).await.unwrap().unwrap_err();
    assert_eq!(err, fdash::LauncherError::BadMoniker);
}

#[fuchsia::test]
pub async fn instance_not_found() {
    let (_stdio, stdio_server) = zx::Socket::create(zx::SocketOpts::STREAM).unwrap();

    let launcher = connect_to_protocol::<fdash::LauncherMarker>().unwrap();

    // Give a moniker to an instance that does not exist
    let err =
        launcher.launch_with_socket("./does_not_exist", stdio_server).await.unwrap().unwrap_err();
    assert_eq!(err, fdash::LauncherError::InstanceNotFound);
}
