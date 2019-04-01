// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::HttpRequest;
use futures::compat::Future01CompatExt;
use futures::future::FutureObj;
use hyper::{Body, Client, Request, Response};

pub struct FuchsiaHyperHttpRequest {
    client: Client<hyper_rustls::HttpsConnector<fuchsia_hyper::HyperConnector>, Body>,
}

impl HttpRequest for FuchsiaHyperHttpRequest {
    fn request(&mut self, req: Request<Body>) -> FutureObj<Result<Response<Body>, hyper::Error>> {
        FutureObj::new(Box::pin(self.client.request(req).compat()))
    }
}

impl FuchsiaHyperHttpRequest {
    pub fn new() -> Self {
        FuchsiaHyperHttpRequest { client: fuchsia_hyper::new_https_client() }
    }
}
