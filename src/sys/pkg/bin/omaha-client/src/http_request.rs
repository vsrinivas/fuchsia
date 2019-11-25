// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(dead_code)]

use futures::{compat::Future01CompatExt, future::BoxFuture, prelude::*};
use hyper::{Body, Client, Request, Response};
use omaha_client::http_request::HttpRequest;

pub struct FuchsiaHyperHttpRequest {
    client: Client<hyper_rustls::HttpsConnector<fuchsia_hyper::HyperConnector>, Body>,
}

impl HttpRequest for FuchsiaHyperHttpRequest {
    fn request(&mut self, req: Request<Body>) -> BoxFuture<'_, Result<Response<Body>, hyper::Error>> {
        self.client.request(req).compat().boxed()
    }
}

impl FuchsiaHyperHttpRequest {
    pub fn new() -> Self {
        FuchsiaHyperHttpRequest { client: fuchsia_hyper::new_https_client() }
    }
}
