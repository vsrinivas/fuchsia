// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl::endpoints::{Proxy, RequestStream, ServiceMarker},
    fuchsia_async as fasync, fuchsia_zircon as zx,
};

mod control;
mod host_device;

fn create_fidl_endpoints<S: ServiceMarker>() -> Result<(S::Proxy, S::RequestStream), Error> {
    let (client, server) = zx::Channel::create()?;
    let client = fasync::Channel::from_channel(client)?;
    let client = S::Proxy::from_channel(client);
    let server = fasync::Channel::from_channel(server)?;
    let server = S::RequestStream::from_channel(server);
    Ok((client, server))
}
