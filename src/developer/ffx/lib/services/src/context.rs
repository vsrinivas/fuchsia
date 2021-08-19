// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Result},
    async_trait::async_trait,
    ffx_daemon_core::events::Queue,
    ffx_daemon_events::{DaemonEvent, TargetEvent},
    ffx_daemon_target::target_collection::TargetCollection,
    fidl::endpoints::Proxy,
    fidl_fuchsia_developer_bridge as bridge,
    fidl_fuchsia_developer_remotecontrol::RemoteControlProxy,
    fidl_fuchsia_diagnostics as diagnostics, selectors,
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

    async fn open_remote_control(
        &self,
        _target_identifier: Option<String>,
    ) -> Result<RemoteControlProxy> {
        unimplemented!()
    }

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

    /// Returns a copy of the daemon target collection.
    async fn get_target_collection(&self) -> Result<Rc<TargetCollection>> {
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

    pub async fn get_target_event_queue(
        &self,
        target_identifier: Option<String>,
    ) -> Result<(bridge::Target, Queue<TargetEvent>)> {
        self.inner.get_target_event_queue(target_identifier).await
    }

    pub async fn daemon_event_queue(&self) -> Queue<DaemonEvent> {
        self.inner.daemon_event_queue().await
    }

    pub async fn open_remote_control(
        &self,
        target_identifier: Option<String>,
    ) -> Result<RemoteControlProxy> {
        self.inner.open_remote_control(target_identifier).await
    }

    pub async fn open_service_proxy<S>(&self) -> Result<S::Proxy>
    where
        S: fidl::endpoints::DiscoverableProtocolMarker,
    {
        let channel = self
            .inner
            .open_service_proxy(
                <S as fidl::endpoints::DiscoverableProtocolMarker>::PROTOCOL_NAME.to_owned(),
            )
            .await?;
        let proxy = S::Proxy::from_channel(
            fidl::AsyncChannel::from_channel(channel).context("making service async channel")?,
        );
        Ok(proxy)
    }

    pub async fn get_target_collection(&self) -> Result<Rc<TargetCollection>> {
        self.inner.get_target_collection().await
    }
}
