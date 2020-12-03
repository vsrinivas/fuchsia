// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(dead_code)]

use fidl::endpoints::RequestStream;
use fidl_fidl_test_addmethod as fidl_lib;
use fuchsia_async as fasync;
use futures::prelude::*;

async fn server(chan: fasync::Channel) -> Result<(), fidl::Error> {
    let mut stream = fidl_lib::ExampleProtocolRequestStream::from_channel(chan);
    while let Some(req) = stream.try_next().await? {
        match req {
            fidl_lib::ExampleProtocolRequest::ExistingMethod { .. } => {}
            fidl_lib::ExampleProtocolRequest::NewMethod { .. } => {}
        }
    }
    Ok(())
}

fn main() {}
