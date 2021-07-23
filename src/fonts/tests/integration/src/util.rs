// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    fidl::endpoints::{DiscoverableProtocolMarker, ProtocolMarker},
    fuchsia_component_test::ScopedInstance,
    futures::lock::Mutex,
    std::collections::HashMap,
};

pub struct ProviderFactory {
    providers: Mutex<HashMap<&'static str, ScopedInstance>>,
}

impl ProviderFactory {
    pub fn new() -> Self {
        Self { providers: Mutex::new(HashMap::new()) }
    }

    /// Opens a connection to the protocol `T` exposed by `fonts_cm`, starting `fonts_cm` if necessary.
    /// If this function is called multiple times with the same `fonts_cm`, the component instance
    /// is shared (58150).
    // TODO: Instead of configuring fonts through a different manifest and command-line arguments,
    // offer a service or directory with the right fonts to the new component instance. This will
    // require support to dynamically offer a capability to a component.
    pub async fn get_provider<T>(&self, fonts_cm: &'static str) -> Result<T::Proxy, Error>
    where
        T: ProtocolMarker + DiscoverableProtocolMarker,
    {
        let mut providers = self.providers.lock().await;
        if !providers.contains_key(fonts_cm) {
            let app = ScopedInstance::new("coll".to_string(), fonts_cm.to_string())
                .await
                .context("Failed to create dynamic component")?;
            providers.insert(fonts_cm, app);
        }
        let font_provider = providers[&fonts_cm]
            .connect_to_protocol_at_exposed_dir::<T>()
            .context(format!("Failed to connect to exposed protocol {}", T::DEBUG_NAME))?;
        Ok(font_provider)
    }
}

pub struct TypedProviderFactory<T> {
    inner: ProviderFactory,
    _marker: std::marker::PhantomData<T>,
}

impl<T> TypedProviderFactory<T>
where
    T: ProtocolMarker + DiscoverableProtocolMarker,
{
    pub fn new() -> Self {
        Self { inner: ProviderFactory::new(), _marker: std::marker::PhantomData }
    }

    pub async fn get_provider(&self, fonts_cm: &'static str) -> Result<T::Proxy, Error> {
        self.inner.get_provider::<T>(fonts_cm).await
    }
}
