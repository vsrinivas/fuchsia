// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    anyhow::Result, async_trait::async_trait, fidl_fuchsia_developer_bridge as bridge,
    protocols::prelude::*,
};

#[ffx_protocol]
#[derive(Default)]
pub struct Testing;

#[async_trait(?Send)]
impl FidlProtocol for Testing {
    type Protocol = bridge::TestingMarker;
    type StreamHandler = FidlStreamHandler<Self>;

    async fn handle(&self, _cx: &Context, req: bridge::TestingRequest) -> Result<()> {
        match req {
            // Hang intends to block the reactor indefinitely, however
            // that's a little tricky to do exactly. This approximation
            // is strong enough for right now, though it may be awoken
            // again periodically on timers, depending on implementation
            // details of the underlying reactor.
            bridge::TestingRequest::Hang { .. } => {
                log::info!("instructed to hang by client invocation");
                std::thread::park();
                Ok(())
            }
            bridge::TestingRequest::Crash { .. } => panic!("instructed to crash by the client"),
        }
    }
}
