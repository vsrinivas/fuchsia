// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capability::*,
        model::{
            error::ModelError,
            hooks::{EventType, Hook, HooksRegistration},
        },
    },
    anyhow::Error,
    async_trait::async_trait,
    fidl::endpoints::{ServerEnd, ServiceMarker},
    fuchsia_async as fasync, fuchsia_zircon as zx,
    log::warn,
    std::{
        path::PathBuf,
        sync::{Arc, Weak},
    },
};

#[async_trait]
pub trait BuiltinCapability {
    /// Name of the capability. Used for hook registration and logging.
    const NAME: &'static str;

    /// Service marker for the capability.
    type Marker: ServiceMarker;

    /// Serves an instance of the capability given an appropriate RequestStream.
    /// Returns when the channel backing the RequestStream is closed or an
    /// unrecoverable error occurs.
    async fn serve(
        self: Arc<Self>,
        mut stream: <Self::Marker as ServiceMarker>::RequestStream,
    ) -> Result<(), Error>;

    /// Returns the registration hooks for the capability.
    fn hooks(self: &Arc<Self>) -> Vec<HooksRegistration>
    where
        Self: 'static + Hook + Sized,
    {
        vec![HooksRegistration::new(
            Self::NAME,
            vec![EventType::RouteCapability],
            Arc::downgrade(self) as Weak<dyn Hook>,
        )]
    }

    /// Returns a `CapabilityProvider` for a given framework capability.
    async fn on_route_framework_capability<'a>(
        self: &'a Arc<Self>,
        capability: &'a FrameworkCapability,
        capability_provider: Option<Box<dyn CapabilityProvider>>,
    ) -> Result<Option<Box<dyn CapabilityProvider>>, ModelError>;
}

pub struct BuiltinCapabilityProvider<B: BuiltinCapability> {
    capability: Weak<B>,
}

impl<B: BuiltinCapability> BuiltinCapabilityProvider<B> {
    pub fn new(capability: Weak<B>) -> Self {
        Self { capability }
    }
}

#[async_trait]
impl<B: 'static + BuiltinCapability + Sync + Send> CapabilityProvider
    for BuiltinCapabilityProvider<B>
{
    async fn open(
        self: Box<Self>,
        _flags: u32,
        _open_mode: u32,
        _relative_path: PathBuf,
        server_end: zx::Channel,
    ) -> Result<(), ModelError> {
        let server_end = ServerEnd::<B::Marker>::new(server_end);
        let stream = server_end.into_stream().unwrap();
        fasync::spawn(async move {
            if let Some(capability) = self.capability.upgrade() {
                if let Err(err) = capability.serve(stream).await {
                    warn!("{}::open failed: {}", B::NAME, err);
                }
            } else {
                warn!("{} has been dropped", B::NAME);
            }
        });
        Ok(())
    }
}
