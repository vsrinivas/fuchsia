// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io as fio,
    fuchsia_async::{self as fasync, futures::TryStreamExt},
};

/// Same as below but provides a noop request handler to the provider `directory`.
/// This is useful if you have to provide an implementation of `fuchsia.io.Directory`
/// for a test, but don't need to handle any individual method.
pub fn spawn_noop_directory_server(directory: ServerEnd<fio::DirectoryMarker>) {
    let handler = move |_request| {};
    spawn_directory_server(directory, handler);
}

/// Spawn a request handler for the provided `directory` server end. This
/// function will spawn a detached Task to handle requests. Requests will be
/// handled using the provided `handler` function.
pub fn spawn_directory_server<F: 'static>(directory: ServerEnd<fio::DirectoryMarker>, handler: F)
where
    F: Fn(fio::DirectoryRequest) + Send,
{
    let mut stream = directory.into_stream().unwrap();
    fasync::Task::spawn(async move {
        while let Some(request) = stream.try_next().await.unwrap() {
            handler(request);
        }
    })
    .detach();
}
