// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Copied from //src/sys/pkg/bin/omaha-client.

#![allow(dead_code)]

use futures::{future::BoxFuture, prelude::*};
use hyper::{Body, Client, Request, Response};
use omaha_client::http_request::{Error, HttpRequest};

pub struct FuchsiaHyperHttpRequest {
    client: Client<hyper_rustls::HttpsConnector<fuchsia_hyper::HyperConnector>, Body>,
}

impl HttpRequest for FuchsiaHyperHttpRequest {
    fn request(&mut self, req: Request<Body>) -> BoxFuture<'_, Result<Response<Body>, Error>> {
        self.client.request(req).map_err(|e| e.into()).boxed()
    }
}

impl FuchsiaHyperHttpRequest {
    pub fn new() -> Self {
        FuchsiaHyperHttpRequest { client: fuchsia_hyper::new_https_client() }
    }
}
