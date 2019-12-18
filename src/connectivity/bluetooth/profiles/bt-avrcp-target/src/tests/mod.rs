// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::Error,
    fidl::endpoints::{Proxy, RequestStream, ServiceMarker},
    fidl_fuchsia_bluetooth_avrcp::*,
    fuchsia_async as fasync, fuchsia_zircon as zx,
};

pub fn create_fidl_endpoints<S: ServiceMarker>() -> Result<(S::Proxy, S::RequestStream), Error> {
    let (client, server) = zx::Channel::create()?;
    let client = fasync::Channel::from_channel(client)?;
    let client = S::Proxy::from_channel(client);
    let server = fasync::Channel::from_channel(server)?;
    let server = S::RequestStream::from_channel(server);
    Ok((client, server))
}

#[fuchsia_async::run_singlethreaded(test)]
// TODO(42623): Add integration tests.
/// Create a channel to serve AVRCP TargetHandler requests.
/// Serve requests from all the possible TG branches, and test the
/// correct insertion, deletion, querying, and setting of MediaState.
/// Ensure end-to-end communication behaves as expected.
async fn test_media_and_avrcp_listener() -> Result<(), Error> {
    let (_c_client, _c_server): (PeerManagerProxy, PeerManagerRequestStream) =
        create_fidl_endpoints::<PeerManagerMarker>()?;
    Ok(())
}
