// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Result},
    async_trait::async_trait,
    fidl::endpoints::Proxy,
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

    pub async fn open_target_proxy<S>(
        &self,
        target_identifier: Option<String>,
        selector: &'static str,
    ) -> Result<S::Proxy>
    where
        S: fidl::endpoints::DiscoverableService,
    {
        let channel = self
            .inner
            .open_target_proxy(target_identifier, selectors::parse_selector(selector)?)
            .await?;
        let proxy = S::Proxy::from_channel(
            fidl::AsyncChannel::from_channel(channel).context("making async channel")?,
        );
        Ok(proxy)
    }
}
