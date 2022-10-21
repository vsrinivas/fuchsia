// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use anyhow::Result;
use async_trait::async_trait;
use ffx_core::Injector;
use ffx_writer::Writer;
use fidl_fuchsia_developer_ffx::{DaemonProxy, FastbootProxy, TargetProxy, VersionInfo};
use fidl_fuchsia_developer_remotecontrol::RemoteControlProxy;
use std::future::Future;
use std::pin::Pin;

#[derive(Default)]
pub struct FakeInjectorBuilder {
    inner: FakeInjector,
}

macro_rules! factory_func {
    ($func:ident, $output:ty $(,)?) => {
        pub fn $func<F, Fut>(mut self, closure: F) -> Self
        where
            F: Fn() -> Fut + 'static,
            Fut: Future<Output = Result<$output>> + 'static,
        {
            self.inner.$func = Box::new(move || Box::pin(closure()));
            self
        }
    };
}

impl FakeInjectorBuilder {
    pub fn new() -> Self {
        Self::default()
    }

    factory_func!(daemon_factory_closure, DaemonProxy);
    factory_func!(remote_factory_closure, RemoteControlProxy);
    factory_func!(fastboot_factory_closure, FastbootProxy);
    factory_func!(target_factory_closure, TargetProxy);
    factory_func!(build_info_closure, VersionInfo);
    factory_func!(writer_closure, Writer);

    pub fn is_experiment_closure<F, Fut>(mut self, closure: F) -> Self
    where
        F: Fn() -> Fut + 'static,
        Fut: Future<Output = bool> + 'static,
    {
        self.inner.is_experiment_closure = Box::new(move |_| Box::pin(closure()));
        self
    }

    pub fn build(self) -> FakeInjector {
        self.inner
    }
}

pub struct FakeInjector {
    daemon_factory_closure: Box<dyn Fn() -> Pin<Box<dyn Future<Output = Result<DaemonProxy>>>>>,
    remote_factory_closure:
        Box<dyn Fn() -> Pin<Box<dyn Future<Output = Result<RemoteControlProxy>>>>>,
    fastboot_factory_closure: Box<dyn Fn() -> Pin<Box<dyn Future<Output = Result<FastbootProxy>>>>>,
    target_factory_closure: Box<dyn Fn() -> Pin<Box<dyn Future<Output = Result<TargetProxy>>>>>,
    is_experiment_closure: Box<dyn Fn(&str) -> Pin<Box<dyn Future<Output = bool>>>>,
    build_info_closure: Box<dyn Fn() -> Pin<Box<dyn Future<Output = Result<VersionInfo>>>>>,
    writer_closure: Box<dyn Fn() -> Pin<Box<dyn Future<Output = Result<Writer>>>>>,
}

impl Default for FakeInjector {
    fn default() -> Self {
        Self {
            daemon_factory_closure: Box::new(|| Box::pin(async { unimplemented!() })),
            remote_factory_closure: Box::new(|| Box::pin(async { unimplemented!() })),
            fastboot_factory_closure: Box::new(|| Box::pin(async { unimplemented!() })),
            target_factory_closure: Box::new(|| Box::pin(async { unimplemented!() })),
            is_experiment_closure: Box::new(|_| Box::pin(async { unimplemented!() })),
            build_info_closure: Box::new(|| Box::pin(async { unimplemented!() })),
            writer_closure: Box::new(|| Box::pin(async { unimplemented!() })),
        }
    }
}

#[async_trait(?Send)]
impl Injector for FakeInjector {
    async fn daemon_factory(&self) -> Result<DaemonProxy> {
        (self.daemon_factory_closure)().await
    }

    async fn remote_factory(&self) -> Result<RemoteControlProxy> {
        (self.remote_factory_closure)().await
    }

    async fn fastboot_factory(&self) -> Result<FastbootProxy> {
        (self.fastboot_factory_closure)().await
    }

    async fn target_factory(&self) -> Result<TargetProxy> {
        (self.target_factory_closure)().await
    }

    async fn is_experiment(&self, key: &str) -> bool {
        (self.is_experiment_closure)(key).await
    }

    async fn build_info(&self) -> Result<VersionInfo> {
        (self.build_info_closure)().await
    }

    async fn writer(&self) -> Result<Writer> {
        (self.writer_closure)().await
    }
}
