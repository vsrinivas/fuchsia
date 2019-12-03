// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod common;
mod dev_auth_provider;
mod oauth_open_id_connect;

use crate::dev_auth_provider::AuthProvider;
use crate::oauth_open_id_connect::OauthOpenIdConnect;
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
    fs.dir("svc")
        .add_fidl_service(|stream| fasync::spawn(AuthProvider::handle_requests_for_stream(stream)));
    fs.dir("svc").add_fidl_service(|stream| {
        fasync::spawn(OauthOpenIdConnect::handle_requests_for_stream(stream))
    });
    fs.take_and_serve_directory_handle()?;

    executor.run_singlethreaded(fs.collect::<()>());
    Ok(())
}
