// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{Error, ResultExt},
    fidl::endpoints::create_proxy,
    fidl_fuchsia_test_breakpoints::*,
    fuchsia_component::client::*,
};

/// A wrapper over the BreakpointSystem FIDL proxy.
/// Provides all of the FIDL methods with a cleaner, simpler interface.
/// Refer to breakpoints.fidl for a detailed description of this protocol.
pub struct BreakpointSystemClient {
    proxy: BreakpointSystemProxy,
}

impl BreakpointSystemClient {
    /// Connects to the BreakpointSystem service at its default location
    /// The default location is presumably "/svc/fuchsia.test.breakpoints.BreakpointSystem"
    pub fn new() -> Result<Self, Error> {
        let proxy = connect_to_service::<BreakpointSystemMarker>()
            .context("could not connect to BreakpointSystem service")?;
        Ok(BreakpointSystemClient::from_proxy(proxy))
    }

    /// Wraps a provided BreakpointSystem proxy
    pub fn from_proxy(proxy: BreakpointSystemProxy) -> Self {
        Self { proxy }
    }

    pub async fn register(
        &self,
        event_types: Vec<EventType>,
    ) -> Result<InvocationReceiverClient, Error> {
        let (proxy, server_end) = create_proxy::<InvocationReceiverMarker>()?;
        self.proxy
            .register(&mut event_types.into_iter(), server_end)
            .await
            .context("could not register breakpoints")?;
        Ok(InvocationReceiverClient::new(proxy))
    }
}

/// A wrapper over the InvocationReceiver FIDL proxy.
/// Provides all of the FIDL methods with a cleaner, simpler interface.
/// Refer to breakpoints.fidl for a detailed description of this protocol.
pub struct InvocationReceiverClient {
    proxy: InvocationReceiverProxy,
}

impl InvocationReceiverClient {
    fn new(proxy: InvocationReceiverProxy) -> Self {
        Self { proxy }
    }

    pub async fn expect(
        &self,
        event_type: EventType,
        component: Vec<&str>,
    ) -> Result<InvocationClient, Error> {
        let (proxy, server_end) = create_proxy::<InvocationMarker>()?;
        self.proxy
            .expect(event_type, &mut component.into_iter(), server_end)
            .await
            .context("could not expect breakpoint")?;
        Ok(InvocationClient::new(proxy))
    }

    pub async fn expect_type(&self, event_type: EventType) -> Result<InvocationClient, Error> {
        let (proxy, server_end) = create_proxy::<InvocationMarker>()?;
        self.proxy
            .expect_type(event_type, server_end)
            .await
            .context("could not expect type of breakpoint")?;
        Ok(InvocationClient::new(proxy))
    }

    pub async fn wait_until_use_capability(
        &self,
        component: Vec<&str>,
        requested_capability_path: &str,
    ) -> Result<InvocationClient, Error> {
        let (proxy, server_end) = create_proxy::<InvocationMarker>()?;
        self.proxy
            .wait_until_use_capability(
                &mut component.into_iter(),
                requested_capability_path,
                server_end,
            )
            .await
            .context("could not wait until use capability")?;
        Ok(InvocationClient::new(proxy))
    }
}

/// A wrapper over the Invocation FIDL proxy.
/// Provides all of the FIDL methods with a cleaner, simpler interface.
/// Refer to breakpoints.fidl for a detailed description of this protocol.
#[must_use = "invoke resume() otherwise component manager will be halted indefinitely!"]
pub struct InvocationClient {
    proxy: InvocationProxy,
}

impl InvocationClient {
    fn new(proxy: InvocationProxy) -> Self {
        Self { proxy }
    }

    pub async fn resume(&self) -> Result<(), Error> {
        self.proxy.resume().await.context("could not resume invocation")?;
        Ok(())
    }
}
