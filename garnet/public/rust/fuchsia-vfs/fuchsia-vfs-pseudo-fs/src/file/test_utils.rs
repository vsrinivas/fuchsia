// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! common utilities for pseudo-file-related tests.

#![cfg(test)]

use {
    crate::directory::entry::DirectoryEntry,
    fidl::endpoints::{create_proxy, ServerEnd},
    fidl_fuchsia_io::{FileMarker, FileProxy},
    fuchsia_async as fasync,
    futures::{channel::mpsc, future::LocalFutureObj, select, Future, FutureExt, Poll, StreamExt},
    pin_utils::pin_mut,
    std::iter,
};

pub fn run_server_client<GetClientRes>(
    flags: u32,
    server: impl DirectoryEntry,
    get_client: impl FnOnce(FileProxy) -> GetClientRes,
) where
    GetClientRes: Future<Output = ()>,
{
    run_server_client_with_mode(flags, 0, server, get_client)
}

pub fn run_server_client_with_mode<GetClientRes>(
    flags: u32,
    mode: u32,
    mut server: impl DirectoryEntry,
    get_client: impl FnOnce(FileProxy) -> GetClientRes,
) where
    GetClientRes: Future<Output = ()>,
{
    let mut exec = fasync::Executor::new().expect("Executor creation failed");

    let (client_proxy, server_end) =
        create_proxy::<FileMarker>().expect("Failed to create connection endpoints");

    server.open(flags, mode, &mut iter::empty(), ServerEnd::new(server_end.into_channel()));

    let client = get_client(client_proxy);

    let future = server.join(client);
    // TODO: How to limit the execution time?  run_until_stalled() does not trigger timers, so
    // I can not do this:
    //
    //   let timeout = 300.millis();
    //   let future = future.on_timeout(
    //       timeout.after_now(),
    //       || panic!("Test did not finish in {}ms", timeout.millis()));

    // As our clients are async generators, we need to pin this future explicitly.
    // All async generators are !Unpin by default.
    pin_mut!(future);
    let _ = exec.run_until_stalled(&mut future);
}

pub fn run_server_client_with_open_requests_channel_and_executor<GetClientRes>(
    mut exec: fasync::Executor,
    mut server: impl DirectoryEntry,
    get_client: impl FnOnce(mpsc::Sender<(u32, u32, ServerEnd<FileMarker>)>) -> GetClientRes,
    executor: impl FnOnce(&mut FnMut() -> Poll<((), ())>),
) where
    GetClientRes: Future<Output = ()>,
{
    let (open_requests_tx, open_requests_rx) =
        mpsc::channel::<(u32, u32, ServerEnd<FileMarker>)>(0);

    let server_wrapper = async move {
        let mut open_requests_rx = open_requests_rx.fuse();
        loop {
            select! {
                _ = server => panic!("file should never complete"),
                open_req = open_requests_rx.next() => {
                    if let Some((flags, mode, server_end)) = open_req {
                        server
                            .open(flags, mode, &mut iter::empty(),
                                    ServerEnd::new(server_end.into_channel()));
                    }
                },
                complete => return,
            }
        }
    };

    let client = get_client(open_requests_tx);

    let future = server_wrapper.join(client);

    // As our clients are async generators, we need to pin this future explicitly.
    // All async generators are !Unpin by default.
    pin_mut!(future);
    let mut obj = LocalFutureObj::new(future);
    executor(&mut || exec.run_until_stalled(&mut obj));
}

pub fn run_server_client_with_open_requests_channel<GetClientRes>(
    server: impl DirectoryEntry,
    get_client: impl FnOnce(mpsc::Sender<(u32, u32, ServerEnd<FileMarker>)>) -> GetClientRes,
) where
    GetClientRes: Future<Output = ()>,
{
    let exec = fasync::Executor::new().expect("Executor creation failed");

    run_server_client_with_open_requests_channel_and_executor(
        exec,
        server,
        get_client,
        |run_until_stalled| {
            let _ = run_until_stalled();
        },
    );
}
