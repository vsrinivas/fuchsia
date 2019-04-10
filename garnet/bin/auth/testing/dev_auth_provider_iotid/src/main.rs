// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]
#![feature(async_await, await_macro, futures_api)]

mod dev_auth_provider_iotid;
mod dev_auth_provider_iotid_factory;

use crate::dev_auth_provider_iotid_factory::AuthProviderFactory;
use failure::{Error, ResultExt};
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use futures::StreamExt;
use log::info;

fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["auth"]).expect("Can't init logger");
    info!("Starting dev auth provider");

    let mut executor = fasync::Executor::new().context("Error creating executor")?;

    let mut fs = ServiceFs::new();
    fs.dir("public").add_fidl_service(AuthProviderFactory::spawn);
    fs.take_and_serve_directory_handle()?;

    executor.run_singlethreaded(fs.collect::<()>());
    Ok(())
}
