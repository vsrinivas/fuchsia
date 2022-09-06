// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_dash as fdash;
use fuchsia_async as fasync;
use fuchsia_component::client::connect_to_protocol;
use fuchsia_zircon as zx;
use futures::prelude::*;

#[fuchsia::test]
pub async fn execute_inline_dash_command_that_succeeds() {
    let (stdio, stdio_server) = zx::Socket::create(zx::SocketOpts::STREAM).unwrap();

    let launcher = connect_to_protocol::<fdash::LauncherMarker>().unwrap();
    launcher
        .launch_with_socket(
            ".",
            stdio_server,
            None,
            Some("ls"),
            fdash::DashNamespaceLayout::NestAllInstanceDirs,
        )
        .await
        .unwrap()
        .unwrap();

    let mut stdio = fasync::Socket::from_socket(stdio).unwrap();
    let mut buf = [0u8; 140];
    stdio.read_exact(&mut buf).await.unwrap();

    let ls_output = std::str::from_utf8(&buf).unwrap();
    assert!(ls_output.contains("."));
    assert!(ls_output.contains(".dash"));
    assert!(ls_output.contains("out"));
    assert!(ls_output.contains("runtime"));
    assert!(ls_output.contains("ns"));
    assert!(ls_output.contains("exposed"));
    assert!(ls_output.contains("svc"));
}

#[fuchsia::test]
pub async fn execute_inline_dash_command_that_succeeds_namespace_layout() {
    let (stdio, stdio_server) = zx::Socket::create(zx::SocketOpts::STREAM).unwrap();

    let launcher = connect_to_protocol::<fdash::LauncherMarker>().unwrap();
    launcher
        .launch_with_socket(
            ".",
            stdio_server,
            None,
            Some("ls"),
            fdash::DashNamespaceLayout::InstanceNamespaceIsRoot,
        )
        .await
        .unwrap()
        .unwrap();

    let mut stdio = fasync::Socket::from_socket(stdio).unwrap();
    let mut buf = [0u8; 76];
    stdio.read_exact(&mut buf).await.unwrap();

    let ls_output = std::str::from_utf8(&buf).unwrap();
    assert!(ls_output.contains("."));
    assert!(ls_output.contains(".dash"));
    assert!(ls_output.contains("pkg"));
    assert!(ls_output.contains("svc"));
}

#[fuchsia::test]
pub async fn execute_inline_dash_command_that_errors() {
    let (stdio, stdio_server) = zx::Socket::create(zx::SocketOpts::STREAM).unwrap();

    let launcher = connect_to_protocol::<fdash::LauncherMarker>().unwrap();
    launcher
        .launch_with_socket(
            ".",
            stdio_server,
            None,
            Some("printenv"),
            fdash::DashNamespaceLayout::NestAllInstanceDirs,
        )
        .await
        .unwrap()
        .unwrap();

    let mut stdio = fasync::Socket::from_socket(stdio).unwrap();
    let mut buf = [0u8; 26];
    stdio.read_exact(&mut buf).await.unwrap();

    assert!(std::str::from_utf8(&buf).unwrap().contains("printenv: not found"));
}

#[fuchsia::test]
pub async fn spawn_dash_namespace_self() {
    let (stdio, stdio_server) = zx::Socket::create(zx::SocketOpts::STREAM).unwrap();

    let launcher = connect_to_protocol::<fdash::LauncherMarker>().unwrap();
    launcher
        .launch_with_socket(
            ".",
            stdio_server,
            None,
            None,
            fdash::DashNamespaceLayout::InstanceNamespaceIsRoot,
        )
        .await
        .unwrap()
        .unwrap();

    let mut stdio = fasync::Socket::from_socket(stdio).unwrap();

    // The dash process prints the interactive prompt "$ ".
    let mut buf = [0u8; 2];
    stdio.read_exact(&mut buf).await.unwrap();
    assert_eq!(buf, "$ ".as_bytes());

    // Type a command.
    stdio.write_all("ls".as_bytes()).await.unwrap();

    // The dash process prints back the typed characters.
    let mut buf = [0u8; 2];
    stdio.read_exact(&mut buf).await.unwrap();
    assert_eq!(buf, "ls".as_bytes());

    // Press enter.
    stdio.write_all("\r".as_bytes()).await.unwrap();

    // The dash process puts a newline.
    let mut buf = [0u8; 2];
    stdio.read_exact(&mut buf).await.unwrap();
    assert_eq!(buf, "\r\n".as_bytes());

    // Note that there is no `ls` binary available to dash.
    // We are relying on the `ls` functionality built into the dash process.

    // The dash process prints details about the current dir.
    let mut buf = [0u8; 15];
    stdio.read_exact(&mut buf).await.unwrap();
    assert_eq!(buf, "d  1        0 .".as_bytes());

    // The dash process puts a newline.
    let mut buf = [0u8; 2];
    stdio.read_exact(&mut buf).await.unwrap();
    assert_eq!(buf, "\r\n".as_bytes());

    // The dash process prints details about `.dash` (the hidden directory used by dash).
    let mut buf = [0u8; 19];
    stdio.read_exact(&mut buf).await.unwrap();
    assert_eq!(buf, "d  1        0 .dash".as_bytes());

    // The dash process puts a newline.
    let mut buf = [0u8; 2];
    stdio.read_exact(&mut buf).await.unwrap();
    assert_eq!(buf, "\r\n".as_bytes());

    // The dash process prints details about `pkg` (the explored component's package).
    let mut buf = [0u8; 17];
    stdio.read_exact(&mut buf).await.unwrap();
    assert_eq!(buf, "d  1        0 pkg".as_bytes());

    // The dash process puts a newline.
    let mut buf = [0u8; 2];
    stdio.read_exact(&mut buf).await.unwrap();
    assert_eq!(buf, "\r\n".as_bytes());

    // The dash process prints details about `svc` (the explored component's used services).
    let mut buf = [0u8; 17];
    stdio.read_exact(&mut buf).await.unwrap();
    assert_eq!(buf, "d  1        0 svc".as_bytes());

    // The dash process puts a newline and the next prompt.
    let mut buf = [0u8; 4];
    stdio.read_exact(&mut buf).await.unwrap();
    assert_eq!(buf, "\r\n$ ".as_bytes());
}

#[fuchsia::test]
pub async fn spawn_dash_with_tools_package() {
    let (stdio, stdio_server) = zx::Socket::create(zx::SocketOpts::STREAM).unwrap();

    let launcher = connect_to_protocol::<fdash::LauncherMarker>().unwrap();
    launcher
        .launch_with_socket(
            ".",
            stdio_server,
            Some("fuchsia-pkg://fuchsia.com/foo"),
            None,
            fdash::DashNamespaceLayout::NestAllInstanceDirs,
        )
        .await
        .unwrap()
        .unwrap();

    let mut stdio = fasync::Socket::from_socket(stdio).unwrap();

    // The dash process prints the interactive prompt "$ ".
    let mut buf = [0u8; 2];
    stdio.read_exact(&mut buf).await.unwrap();
    assert_eq!(buf, "$ ".as_bytes());

    // Type a command.
    stdio.write_all("ls /.dash/tools".as_bytes()).await.unwrap();

    // The dash process prints back the typed characters.
    let mut buf = [0u8; 15];
    stdio.read_exact(&mut buf).await.unwrap();
    assert_eq!(buf, "ls /.dash/tools".as_bytes());

    // Press enter
    stdio.write_all("\r".as_bytes()).await.unwrap();

    // The dash process puts a newline.
    let mut buf = [0u8; 2];
    stdio.read_exact(&mut buf).await.unwrap();
    assert_eq!(buf, "\r\n".as_bytes());

    // Note that there is no `ls` binary available to dash.
    // We are relying on the `ls` functionality built into the dash process.

    // The dash process prints details about the current dir.
    let mut buf = [0u8; 15];
    stdio.read_exact(&mut buf).await.unwrap();
    assert_eq!(buf, "d  2        0 .".as_bytes());

    // The dash process puts a newline.
    let mut buf = [0u8; 2];
    stdio.read_exact(&mut buf).await.unwrap();
    assert_eq!(buf, "\r\n".as_bytes());

    // The dash process prints details about `foo` (a file provided by mock-resolver).
    let mut buf = [0u8; 17];
    stdio.read_exact(&mut buf).await.unwrap();
    assert_eq!(buf, "-  1        6 foo".as_bytes());

    // The dash process puts a newline and the next prompt.
    let mut buf = [0u8; 4];
    stdio.read_exact(&mut buf).await.unwrap();
    assert_eq!(buf, "\r\n$ ".as_bytes());
}

#[fuchsia::test]
pub async fn spawn_dash_nested_self() {
    let (stdio, stdio_server) = zx::Socket::create(zx::SocketOpts::STREAM).unwrap();

    let launcher = connect_to_protocol::<fdash::LauncherMarker>().unwrap();
    launcher
        .launch_with_socket(
            ".",
            stdio_server,
            None,
            None,
            fdash::DashNamespaceLayout::NestAllInstanceDirs,
        )
        .await
        .unwrap()
        .unwrap();

    let mut stdio = fasync::Socket::from_socket(stdio).unwrap();
    let mut buf = [0u8, 0u8];
    stdio.read_exact(&mut buf).await.unwrap();

    // The dash process prints the interactive prompt "$ ".
    assert_eq!(buf, "$ ".as_bytes());

    stdio.write_all("ls".as_bytes()).await.unwrap();
    stdio.read_exact(&mut buf).await.unwrap();

    // The dash process prints back the typed characters "ls".
    assert_eq!(buf, "ls".as_bytes());
}

#[fuchsia::test]
pub async fn unknown_tools_package() {
    let (_stdio, stdio_server) = zx::Socket::create(zx::SocketOpts::STREAM).unwrap();

    let launcher = connect_to_protocol::<fdash::LauncherMarker>().unwrap();
    let err = launcher
        .launch_with_socket(
            ".",
            stdio_server,
            Some("fuchsia-pkg://fuchsia.com/bar"),
            None,
            fdash::DashNamespaceLayout::NestAllInstanceDirs,
        )
        .await
        .unwrap()
        .unwrap_err();

    assert_eq!(err, fdash::LauncherError::ToolsCannotResolve);
}

#[fuchsia::test]
pub async fn bad_moniker() {
    let (_stdio, stdio_server) = zx::Socket::create(zx::SocketOpts::STREAM).unwrap();

    let launcher = connect_to_protocol::<fdash::LauncherMarker>().unwrap();

    // Give a string that won't parse correctly as a moniker.
    let err = launcher
        .launch_with_socket(
            "!@#$%^&*(",
            stdio_server,
            None,
            None,
            fdash::DashNamespaceLayout::NestAllInstanceDirs,
        )
        .await
        .unwrap()
        .unwrap_err();
    assert_eq!(err, fdash::LauncherError::BadMoniker);
}

#[fuchsia::test]
pub async fn instance_not_found() {
    let (_stdio, stdio_server) = zx::Socket::create(zx::SocketOpts::STREAM).unwrap();

    let launcher = connect_to_protocol::<fdash::LauncherMarker>().unwrap();

    // Give a moniker to an instance that does not exist.
    let err = launcher
        .launch_with_socket(
            "./does_not_exist",
            stdio_server,
            None,
            None,
            fdash::DashNamespaceLayout::NestAllInstanceDirs,
        )
        .await
        .unwrap()
        .unwrap_err();
    assert_eq!(err, fdash::LauncherError::InstanceNotFound);
}
