// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro)]

pub mod fidl_helpers;
pub mod ime_service;
pub mod index_convert;
pub mod legacy_ime;
pub mod multiplex;

#[cfg(test)]
pub mod integration_tests;

use failure::{Error, ResultExt};
use fuchsia_component::server::ServiceFs;
use fuchsia_syslog;
use futures::StreamExt;

fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["ime_service"]).expect("ime syslog init should not fail");
    let mut executor = fuchsia_async::Executor::new()
        .context("Creating fuchsia_async executor for IME service failed")?;
    let ime_service = ime_service::ImeService::new();
    let mut fs = ServiceFs::new();
    fs.dir("public")
        .add_fidl_service(|stream| ime_service.bind_ime_service(stream))
        .add_fidl_service(|stream| ime_service.bind_ime_visibility_service(stream))
        .add_fidl_service(|stream| ime_service.bind_text_input_context(stream));
    fs.take_and_serve_directory_handle()?;
    let () = executor.run_singlethreaded(fs.collect());
    Ok(())
}
