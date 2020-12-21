// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(dead_code)]

use fidl::endpoints::RequestStream;
use fidl_fidl_test_protocolmethodremove as fidl_lib;
use fuchsia_async as fasync;
use futures::prelude::*;

// [START contents]
struct ExampleFakeProxy;

impl fidl_lib::ExampleProxyInterface for ExampleFakeProxy {
    fn existing_method(&self) -> Result<(), fidl::Error> {
        Ok(())
    }
}

async fn example_service(chan: fasync::Channel) -> Result<(), fidl::Error> {
    let mut stream = fidl_lib::ExampleRequestStream::from_channel(chan);
    while let Some(req) = stream.try_next().await? {
        match req {
            fidl_lib::ExampleRequest::ExistingMethod { .. } => {}
        }
    }
    Ok(())
}
// [END contents]

fn main() {}
