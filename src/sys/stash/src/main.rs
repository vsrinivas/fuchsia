// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(missing_docs)]

//! `stash` provides key/value storage to components.

use anyhow::{Context as _, Error};
use fidl::endpoints::ServiceMarker;
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use fuchsia_syslog::{fx_log_err, fx_log_info};
use futures::lock::Mutex;
use futures::{StreamExt, TryFutureExt, TryStreamExt};
use std::convert::{TryFrom, TryInto};
use std::env;
use std::path::PathBuf;
use std::process;
use std::sync::Arc;

mod accessor;
mod instance;
mod store;

use fidl_fuchsia_stash::{SecureStoreMarker, StoreMarker, StoreRequest, StoreRequestStream};

struct StashSettings {
    backing_file: String,
    secure_mode: bool,
}

impl Default for StashSettings {
    fn default() -> StashSettings {
        StashSettings { backing_file: "/data/stash.store".to_string(), secure_mode: false }
    }
}

impl TryFrom<env::Args> for StashSettings {
    type Error = ();
    fn try_from(mut args: env::Args) -> Result<StashSettings, ()> {
        // ignore arg[0]
        let _ = args.next();
        let mut res = StashSettings::default();
        while let Some(flag) = args.next() {
            match flag.as_str() {
                "--backing_file" => {
                    if let Some(f) = args.next() {
                        res.backing_file = f;
                    } else {
                        return Err(());
                    }
                }
                "--secure" => res.secure_mode = true,
                _ => return Err(()),
            }
        }
        return Ok(res);
    }
}

fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["stash"])?;

    let r_opts: Result<StashSettings, ()> = env::args().try_into();

    match r_opts {
        Err(_) => {
            print_help();
            process::exit(1);
        }
        Ok(opts) => {
            let mut executor = fasync::Executor::new().context("Error creating executor")?;
            let store_manager =
                Arc::new(Mutex::new(store::StoreManager::new(PathBuf::from(&opts.backing_file))?));

            let name = if opts.secure_mode { SecureStoreMarker::NAME } else { StoreMarker::NAME };

            let mut fs = ServiceFs::new();
            fs.dir("svc").add_fidl_service_at(name, |stream| {
                stash_server(store_manager.clone(), !opts.secure_mode, stream)
            });
            fs.take_and_serve_directory_handle()?;
            executor.run_singlethreaded(fs.collect::<()>());
        }
    }
    Ok(())
}

fn print_help() {
    println!(
        r"stash
garnet service for storing key/value pairs

USAGE:
    stash [FLAGS]

FLAGS:
        --secure                Disables support for handling raw bytes. This flag Should be used
                                when running in critical path of verified boot.
        --backing_file <FILE>   location of backing file for the store"
    )
}

fn stash_server(
    store_manager: Arc<Mutex<store::StoreManager>>,
    enable_bytes: bool,
    mut stream: StoreRequestStream,
) {
    fasync::spawn(
        async move {
            fx_log_info!("new connection");
            let mut state = instance::Instance {
                client_name: None,
                enable_bytes: enable_bytes,
                store_manager: store_manager,
            };

            while let Some(req) = stream.try_next().await.context("error running stash server")? {
                match req {
                    StoreRequest::Identify { name, control_handle } => {
                        if let Err(e) = state.identify(name.clone()) {
                            control_handle.shutdown();
                            return Err(e);
                        }
                        fx_log_info!("identified new client: {}", name);
                    }
                    StoreRequest::CreateAccessor {
                        read_only,
                        control_handle,
                        accessor_request,
                    } => {
                        if let Err(e) = state.create_accessor(read_only, accessor_request) {
                            control_handle.shutdown();
                            return Err(e);
                        }
                        fx_log_info!("created new accessor");
                    }
                }
            }
            Ok(())
        }
        .unwrap_or_else(|e: anyhow::Error| fx_log_err!("couldn't run stash service: {:?}", e)),
    );
}
