// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[macro_use]
mod common;
mod crypto_provider;
mod key_manager;
mod kms_asymmetric_key;
mod kms_sealing_key;
mod tee;

use crate::key_manager::KeyManager;

use failure::{Error, ResultExt};
use fidl_fuchsia_kms::KeyManagerRequestStream;
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use fuchsia_syslog as syslog;
use futures::prelude::*;
use log::{error, info};
use serde_derive::{Deserialize, Serialize};
use serde_json;
use std::fs;
use std::sync::Arc;

const CONFIG_PATH: &str = "/config/data/crypto_provider_config.json";

fn main() -> Result<(), Error> {
    syslog::init_with_tags(&["kms"]).expect("syslog init should not fail");
    let mut executor = fasync::Executor::new().context("Error creating executor")?;
    let key_manager_ref = Arc::new({
        let mut key_manager = KeyManager::new();
        let config_result = get_provider_from_config();
        if let Ok(provider) = config_result {
            info!("Config found, using provider: {} for KMS", &provider);
            key_manager.set_provider(&provider)?;
        } else {
            info!("No config found, use default crypto provider for KMS");
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

fn get_provider_from_config() -> Result<String, Error> {
    let json = fs::read_to_string(CONFIG_PATH)?;
    let config: Config = serde_json::from_str(&json)?;
    Ok(config.crypto_provider.to_string())
}

fn spawn(mut stream: KeyManagerRequestStream, key_manager: Arc<KeyManager>) {
    fasync::spawn(
        async move {
            while let Some(r) = stream.try_next().await? {
                key_manager.handle_request(r)?;
            }
            Ok(())
        }
            .unwrap_or_else(|e: fidl::Error| error!("Error handling KMS request: {:?}", e)),
    );
}
