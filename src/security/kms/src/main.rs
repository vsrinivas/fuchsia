// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[macro_use]
mod common;
mod crypto_provider;
mod key_manager;
mod kms_asymmetric_key;
mod kms_sealing_key;

use crate::key_manager::KeyManager;

use anyhow::{format_err, Context as _, Error};
use fidl_fuchsia_kms::{KeyManagerRequestStream, KeyProvider};
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use futures::prelude::*;
use serde::{Deserialize, Serialize};
use serde_json;
use std::fs;
use std::sync::Arc;
use tracing::{error, info};

const CONFIG_PATH: &str = "/config/data/crypto_provider_config.json";

#[fuchsia::main(logging_tags = ["kms"])]
fn main() -> Result<(), Error> {
    let mut executor = fasync::LocalExecutor::new().context("Error creating executor")?;
    let key_manager_ref = Arc::new({
        let mut key_manager = KeyManager::new();
        match get_provider_from_config() {
            Ok(provider) => {
                info!("Config found, using provider: {:?} for KMS", &provider);
                key_manager.set_provider(provider)?;
            }
            Err(err) => {
                info!("Failed to read config, err: {:?} use default crypto provider for KMS", err);
            }
        }
        key_manager
    });
    let mut fs = ServiceFs::new();
    fs.dir("svc").add_fidl_service(|stream| spawn(stream, Arc::clone(&key_manager_ref)));
    fs.take_and_serve_directory_handle()?;
    let () = executor.run_singlethreaded(fs.collect());
    Ok(())
}

#[derive(Debug, Deserialize, PartialEq, Serialize)]
pub struct Config<'a> {
    pub crypto_provider: &'a str,
}

fn get_provider_from_config() -> Result<KeyProvider, Error> {
    let json = fs::read_to_string(CONFIG_PATH)?;
    let config: Config<'_> = serde_json::from_str(&json)?;
    match config.crypto_provider {
        "OpteeProvider" => Ok(KeyProvider::OpteeProvider),
        "SoftwareProvider" => Ok(KeyProvider::SoftwareProvider),
        "SoftwareAsymmetricOnlyProvider" => Ok(KeyProvider::SoftwareAsymmetricOnlyProvider),
        _ => Err(format_err!("Unsupported provider {:?}", config.crypto_provider)),
    }
}

fn spawn(mut stream: KeyManagerRequestStream, key_manager: Arc<KeyManager>) {
    fasync::Task::spawn(
        async move {
            while let Some(r) = stream.try_next().await? {
                key_manager.handle_request(r)?;
            }
            Ok(())
        }
        .unwrap_or_else(|e: fidl::Error| error!("Error handling KMS request: {:?}", e)),
    )
    .detach();
}
