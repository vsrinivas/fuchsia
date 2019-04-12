// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro, futures_api)]
#[macro_use]
mod common;
mod crypto_provider;
mod key_manager;
mod kms_asymmetric_key;
mod kms_sealing_key;

use crate::key_manager::KeyManager;

use failure::{Error, ResultExt};
use fidl_fuchsia_kms::KeyManagerRequestStream;
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use futures::prelude::*;
use log::error;
use std::sync::Arc;

fn main() -> Result<(), Error> {
    let mut executor = fasync::Executor::new().context("Error creating executor")?;
    let key_manager = Arc::new(KeyManager::new());
    let mut fs = ServiceFs::new();
    fs.dir("public").add_fidl_service(|stream| spawn(stream, Arc::clone(&key_manager)));
    fs.take_and_serve_directory_handle()?;
    let () = executor.run_singlethreaded(fs.collect());
    Ok(())
}

fn spawn(mut stream: KeyManagerRequestStream, key_manager: Arc<KeyManager>) {
    fasync::spawn(
        async move {
            while let Some(r) = await!(stream.try_next())? {
                key_manager.handle_request(r)?;
            }
            Ok(())
        }
            .unwrap_or_else(|e: fidl::Error| error!("Error handling KMS request: {:?}", e)),
    );
}
