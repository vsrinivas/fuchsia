// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(futures_api)]

extern crate failure;
extern crate fidl;
extern crate fidl_fuchsia_auth;
extern crate fuchsia_app as component;
extern crate fuchsia_async as async;
extern crate fuchsia_syslog as syslog;
extern crate futures;
#[macro_use]
extern crate log;
extern crate rand;

mod dev_auth_provider;
mod dev_auth_provider_factory;

use component::server::ServicesServer;
use dev_auth_provider_factory::AuthProviderFactory;
use failure::{Error, ResultExt};
use fidl::endpoints2::ServiceMarker;
use fidl_fuchsia_auth::AuthProviderFactoryMarker;

fn main() -> Result<(), Error> {
    syslog::init_with_tags(&["auth"]).expect("Can't init logger");
    info!("Starting dev auth provider");

    let mut executor = async::Executor::new().context("Error creating executor")?;

    let fut = ServicesServer::new()
        .add_service((AuthProviderFactoryMarker::NAME, |chan| {
            AuthProviderFactory::spawn(chan)
        }))
        .start()
        .context("Error starting dev auth provider server")?;

    executor
        .run_singlethreaded(fut)
        .context("Failed to execute dev auth provider future")?;
    Ok(())
}
