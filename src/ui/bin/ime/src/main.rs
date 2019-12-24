// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod fidl_helpers;
pub mod ime_service;
pub mod index_convert;
pub mod legacy_ime;
pub mod multiplex;

#[cfg(test)]
pub mod integration_tests;

mod keyboard;
use anyhow::{Context as _, Error};
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use fuchsia_syslog;
use futures::StreamExt;

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["ime_service"]).expect("ime syslog init should not fail");

    let ime_service = ime_service::ImeService::new();
    let keyboard_service = keyboard::Service::new(ime_service.clone())
        .await
        .context("error initializing keyboard service")?;
    let mut fs = ServiceFs::new();
    fs.dir("svc")
        .add_fidl_service(|stream| keyboard_service.spawn_keyboard_service(stream))
        .add_fidl_service(|stream| keyboard_service.spawn_ime_service(stream))
        .add_fidl_service(|stream| ime_service.bind_ime_visibility_service(stream))
        .add_fidl_service(|stream| ime_service.bind_text_input_context(stream));

    fs.take_and_serve_directory_handle()?;
    let () = fs.collect().await;
    Ok(())
}
