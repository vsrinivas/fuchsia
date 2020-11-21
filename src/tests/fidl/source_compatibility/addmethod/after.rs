// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(dead_code)]

use fidl::endpoints::RequestStream;
use fuchsia_async as fasync;
use futures::prelude::*;

use fidl_fidl_test_addmethod::*;

struct ExampleFakeProxy;

impl ExampleProxyInterface for ExampleFakeProxy {
    fn existing_method(&self) -> Result<(), fidl::Error> {
        Ok(())
    }
    fn new_method(&self) -> Result<(), fidl::Error> {
        Ok(())
    }
}

async fn example_service(chan: fasync::Channel) -> Result<(), fidl::Error> {
    let mut stream = ExampleRequestStream::from_channel(chan);
    while let Some(req) = stream.try_next().await? {
        match req {
            ExampleRequest::ExistingMethod { .. } => {}
            ExampleRequest::NewMethod { .. } => {}
        }
    }
    Ok(())
}

fn main() {}
