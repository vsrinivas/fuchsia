// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod common;
mod oauth;
mod oauth_open_id_connect;
mod open_id_connect;

use crate::oauth::Oauth;
use crate::oauth_open_id_connect::OauthOpenIdConnect;
use crate::open_id_connect::OpenIdConnect;
use anyhow::Error;
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use futures::StreamExt;
use log::info;

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["auth"]).expect("Can't init logger");
    info!("Starting dev auth provider");

    let mut fs = ServiceFs::new();
    fs.dir("svc").add_fidl_service(|stream| {
        fasync::spawn(OauthOpenIdConnect::handle_requests_for_stream(stream))
    });
    fs.dir("svc")
        .add_fidl_service(|stream| fasync::spawn(Oauth::handle_requests_for_stream(stream)));
    fs.dir("svc").add_fidl_service(|stream| {
        fasync::spawn(OpenIdConnect::handle_requests_for_stream(stream))
    });
    fs.take_and_serve_directory_handle()?;

    fs.collect::<()>().await;
    Ok(())
}
