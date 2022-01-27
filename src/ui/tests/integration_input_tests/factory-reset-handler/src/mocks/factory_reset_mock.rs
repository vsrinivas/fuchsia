// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_recovery::{FactoryResetRequest, FactoryResetRequestStream},
    fuchsia_component_test::new::{ChildOptions, RealmBuilder},
    fuchsia_zircon as zx,
    futures::{channel::mpsc::UnboundedSender, TryStreamExt},
};

/// A mock implementation of `fuchsia.recovery.FactoryReset`, which
/// a) responds to every request with ZX_OK, and
/// b) informs an `UnboundedSender` when each requests comes in.
///
/// All `clone()`s of this mock will relay their requests to the
/// same `UnbounderSender`.
#[derive(Clone)]
pub(crate) struct FactoryResetMock {
    name: String,
    request_relay_write_end: UnboundedSender<()>,
}

impl FactoryResetMock {
    pub(crate) fn new<M: Into<String>>(
        name: M,
        request_relay_write_end: UnboundedSender<()>,
    ) -> Self {
        Self { name: name.into(), request_relay_write_end }
    }

    async fn serve_one_client(self, mut request_stream: FactoryResetRequestStream) {
        while let Some(request) =
            request_stream.try_next().await.expect("Failed to read FactoryResetRequest")
        {
            match request {
                FactoryResetRequest::Reset { responder, .. } => {
                    responder.send(zx::sys::ZX_OK).expect("Failed to send reset result");
                    self.request_relay_write_end
                        .unbounded_send(())
                        .expect("Failed to relay reset request to test");
                }
            }
        }
    }
}

impl_test_realm_component!(FactoryResetMock);
