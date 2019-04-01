// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro, futures_api)]

use failure::{Error, ResultExt};
use hyper::{Body, Request};
use log::info;
use omaha_client::http_request::{fuchsia_hyper::FuchsiaHyperHttpRequest, HttpRequest};

fn main() -> Result<(), Error> {
    fuchsia_syslog::init().expect("Can't init logger");
    info!("Starting omaha client...");

    let mut executor = fuchsia_async::Executor::new().context("Error creating executor")?;

    executor.run_singlethreaded(
        async {
            let mut http_request = FuchsiaHyperHttpRequest::new();
            let req = Request::get("https://www.google.com/").body(Body::empty())?;
            let response = await!(http_request.request(req))?;

            info!("status: {}", response.status());
            Ok(())
        },
    )
}
