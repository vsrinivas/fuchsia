// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! LoWPAN Service for Fuchsia

pub mod service;

use anyhow::{format_err, Context as _, Error};
use fidl_fuchsia_lowpan_device::{LookupRequestStream, RegisterRequestStream};
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use fuchsia_syslog::macros::*;
use futures::prelude::*;
use futures::task::{FutureObj, Spawn, SpawnError};
use lowpan_driver_common::ServeTo;
use service::*;
use std::default::Default;

enum IncomingService {
    Lookup(LookupRequestStream),
    Registry(RegisterRequestStream),
}

const MAX_CONCURRENT: usize = 100;

/// Type that implements futures::task::Spawn and uses
/// Fuchsia's port-based global executor.
pub struct FuchsiaGlobalExecutor;
impl Spawn for FuchsiaGlobalExecutor {
    fn spawn_obj(&self, future: FutureObj<'static, ()>) -> Result<(), SpawnError> {
        fasync::spawn(future);
        Ok(())
    }
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["lowpan"]).context("initialize logging")?;
    fuchsia_syslog::set_severity(fuchsia_syslog::levels::INFO);

    fx_log_info!("LoWPAN Service starting up");

    let service = LowpanService::with_spawner(FuchsiaGlobalExecutor);

    let mut fs = ServiceFs::new_local();

    fs.dir("svc")
        .add_fidl_service(IncomingService::Lookup)
        .add_fidl_service(IncomingService::Registry);

    fs.take_and_serve_directory_handle()?;

    let fut = fs.for_each_concurrent(MAX_CONCURRENT, |request| async {
        if let Err(err) = match request {
            IncomingService::Lookup(stream) => service.serve_to(stream).await,
            IncomingService::Registry(stream) => service.serve_to(stream).await,
        } {
            fx_log_err!("{:?}", err);
        }
    });

    fut.await;

    fx_log_info!("LoWPAN Service shut down");

    Ok(())
}
