// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Result},
    async_trait::async_trait,
    ffx_daemon_core::events::Queue,
    ffx_daemon_events::{DaemonEvent, TargetEvent},
    fidl::endpoints::Proxy,
    fidl_fuchsia_developer_bridge as bridge, fidl_fuchsia_diagnostics as diagnostics, selectors,
    std::rc::Rc,
};

#[async_trait(?Send)]
pub trait DaemonServiceProvider {
    async fn open_service_proxy(&self, service_name: String) -> Result<fidl::Channel>;

    async fn open_target_proxy(
        &self,
        target_identifier: Option<String>,
        service_selector: diagnostics::Selector,
    ) -> Result<fidl::Channel>;

    /// Identical to open_target_proxy, but also returns a target info struct.
    async fn open_target_proxy_with_info(
        &self,
        target_identifier: Option<String>,
        service_selector: diagnostics::Selector,
    ) -> Result<(bridge::Target, fidl::Channel)>;

    async fn get_target_event_queue(
        &self,
        _target_identifier: Option<String>,
    ) -> Result<(bridge::Target, Queue<TargetEvent>)> {
        unimplemented!()
    }

    /// Returns a clone of the daemon event queue.
    async fn daemon_event_queue(&self) -> Queue<DaemonEvent> {
        unimplemented!()
    }
}

/// A struct containing the current service's active context when invoking the
/// handle function. This is intended to interface with the Daemon.
#[derive(Clone)]
pub struct Context {
    inner: Rc<dyn DaemonServiceProvider>,
}

impl Context {
    pub fn new(t: impl DaemonServiceProvider + 'static) -> Self {
        Self { inner: Rc::new(t) }
    }

    pub async fn open_target_proxy<P>(
        &self,
        target_identifier: Option<String>,
        selector: &'static str,
    ) -> Result<P::Proxy>
    where
        P: fidl::endpoints::DiscoverableProtocolMarker,
    {
        let (_, proxy) = self.open_target_proxy_with_info::<P>(target_identifier, selector).await?;
        Ok(proxy)
    }

    pub async fn open_target_proxy_with_info<P>(
        &self,
        target_identifier: Option<String>,
        selector: &'static str,
    ) -> Result<(bridge::Target, P::Proxy)>
    where
        P: fidl::endpoints::DiscoverableProtocolMarker,
    {
        let (info, channel) = self
            .inner
            .open_target_proxy_with_info(target_identifier, selectors::parse_selector(selector)?)
            .await?;
        let proxy = P::Proxy::from_channel(
            fidl::AsyncChannel::from_channel(channel).context("making async channel")?,
        );
        Ok((info, proxy))
    }
}
