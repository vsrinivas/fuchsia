// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Error};
use fidl::endpoints::{DiscoverableService, Proxy, ServiceMarker};
use futures::future::BoxFuture;

use fuchsia_async as fasync;
use fuchsia_component::client::{connect_to_service, connect_to_service_at_path};
use glob::glob;

use fuchsia_zircon as zx;
use futures::lock::Mutex;
use std::sync::Arc;

pub type GenerateService =
    Box<dyn Fn(&str, zx::Channel) -> BoxFuture<'static, Result<(), Error>> + Send + Sync>;

pub type ServiceContextHandle = Arc<Mutex<ServiceContext>>;

/// A wrapper around service operations, allowing redirection to a nested
/// environment.
pub struct ServiceContext {
    generate_service: Option<GenerateService>,
}

impl ServiceContext {
    pub fn create(generate_service: Option<GenerateService>) -> ServiceContextHandle {
        return Arc::new(Mutex::new(ServiceContext::new(generate_service)));
    }

    pub fn new(generate_service: Option<GenerateService>) -> Self {
        Self { generate_service: generate_service }
    }

    /// Connect to a service with the given ServiceMarker.
    ///
    /// If a GenerateService was specified at creation, the name of the service marker will be used
    /// to generate a service.
    pub async fn connect<S: DiscoverableService>(&self) -> Result<S::Proxy, Error> {
        if let Some(generate_service) = &self.generate_service {
            let (client, server) = zx::Channel::create()?;
            ((generate_service)(S::SERVICE_NAME, server)).await?;
            return Ok(S::Proxy::from_channel(fasync::Channel::from_channel(client)?));
        } else {
            return connect_to_service::<S>();
        }
    }

    /// Connect to a service with the given name and ServiceMarker.
    ///
    /// If a GenerateService was specified at creation, the given name will be used to generate a
    /// service.
    pub async fn connect_named<S: ServiceMarker>(
        &self,
        service_name: &str,
    ) -> Result<S::Proxy, Error> {
        if let Some(generate_service) = &self.generate_service {
            let (client, server) = zx::Channel::create()?;
            if (generate_service)(service_name, server).await.is_err() {
                return Err(format_err!("Could not handl service {:?}", service_name));
            }

            Ok(S::Proxy::from_channel(fasync::Channel::from_channel(client)?))
        } else {
            Err(format_err!("No service generator"))
        }
    }

    /// Connect to a service at the given path and ServiceMarker.
    ///
    /// If a GenerateService was specified at creation, the name of the service marker will be used
    /// to generate a service and the path will be ignored.
    pub async fn connect_path<S: DiscoverableService>(
        &self,
        path: &str,
    ) -> Result<S::Proxy, Error> {
        if let Some(generate_service) = &self.generate_service {
            let (client, server) = zx::Channel::create()?;
            ((generate_service)(S::SERVICE_NAME, server)).await?;
            Ok(S::Proxy::from_channel(fasync::Channel::from_channel(client)?))
        } else {
            connect_to_service_at_path::<S>(path)
        }
    }

    /// Connect to a service by discovering a hardware device at the given glob-style pattern.
    ///
    /// The first discovered path will be used to connected.
    ///
    /// If a GenerateService was specified at creation, the name of the service marker will be used
    /// to generate a service and the path will be ignored.
    pub async fn connect_device_path<S: DiscoverableService>(
        &self,
        glob_pattern: &str,
    ) -> Result<S::Proxy, Error> {
        if self.generate_service.is_some() {
            // If a generate_service is already specified, just connect through there
            self.connect::<S>().await
        } else {
            let found_path = glob(glob_pattern)?
                .filter_map(|entry| entry.ok())
                .next()
                .ok_or_else(|| format_err!("failed to enumerate devices"))?;

            let path_str =
                found_path.to_str().ok_or_else(|| format_err!("failed to convert path to str"))?;

            connect_to_service_at_path::<S>(path_str)
        }
    }
}
