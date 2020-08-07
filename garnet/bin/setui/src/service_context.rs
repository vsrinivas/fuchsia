// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::internal::event::message::Factory as EventMessengerFactory;
use crate::internal::event::{Event, Publisher};
use crate::message::base::MessengerType;

use anyhow::{format_err, Error};
use fidl::endpoints::{DiscoverableService, Proxy, ServiceMarker};
use futures::future::{BoxFuture, OptionFuture};

use fuchsia_async as fasync;
use fuchsia_component::client::{connect_to_service, connect_to_service_at_path};
use glob::glob;

use fuchsia_zircon as zx;
use futures::lock::Mutex;
use std::future::Future;
use std::sync::Arc;

pub type GenerateService =
    Box<dyn Fn(&str, zx::Channel) -> BoxFuture<'static, Result<(), Error>> + Send + Sync>;

pub type ServiceContextHandle = Arc<Mutex<ServiceContext>>;

/// A wrapper around service operations, allowing redirection to a nested
/// environment.
pub struct ServiceContext {
    generate_service: Option<GenerateService>,
    event_messenger_factory: Option<EventMessengerFactory>,
}

impl ServiceContext {
    pub fn create(
        generate_service: Option<GenerateService>,
        event_messenger_factory: Option<EventMessengerFactory>,
    ) -> ServiceContextHandle {
        return Arc::new(Mutex::new(ServiceContext::new(
            generate_service,
            event_messenger_factory,
        )));
    }

    pub fn new(
        generate_service: Option<GenerateService>,
        event_messenger_factory: Option<EventMessengerFactory>,
    ) -> Self {
        Self { generate_service, event_messenger_factory }
    }

    async fn make_publisher(&self) -> Option<Publisher> {
        let maybe: OptionFuture<_> = self
            .event_messenger_factory
            .as_ref()
            .map(|factory| Publisher::create(factory, MessengerType::Unbound))
            .into();
        maybe.await
    }

    /// Connect to a service with the given ServiceMarker.
    ///
    /// If a GenerateService was specified at creation, the name of the service marker will be used
    /// to generate a service.
    pub async fn connect<S: DiscoverableService>(
        &self,
    ) -> Result<ExternalServiceProxy<S::Proxy>, Error> {
        let proxy = if let Some(generate_service) = &self.generate_service {
            let (client, server) = zx::Channel::create()?;
            ((generate_service)(S::SERVICE_NAME, server)).await?;
            S::Proxy::from_channel(fasync::Channel::from_channel(client)?)
        } else {
            connect_to_service::<S>()?
        };

        Ok(ExternalServiceProxy { proxy, publisher: self.make_publisher().await })
    }

    pub async fn connect_with_publisher<S: DiscoverableService>(
        &self,
        publisher: Publisher,
    ) -> Result<ExternalServiceProxy<S::Proxy>, Error> {
        let proxy = if let Some(generate_service) = &self.generate_service {
            let (client, server) = zx::Channel::create()?;
            ((generate_service)(S::SERVICE_NAME, server)).await?;
            S::Proxy::from_channel(fasync::Channel::from_channel(client)?)
        } else {
            connect_to_service::<S>()?
        };

        Ok(ExternalServiceProxy { proxy, publisher: Some(publisher) })
    }

    /// Connect to a service with the given name and ServiceMarker.
    ///
    /// If a GenerateService was specified at creation, the given name will be used to generate a
    /// service.
    pub async fn connect_named<S: ServiceMarker>(
        &self,
        service_name: &str,
    ) -> Result<ExternalServiceProxy<S::Proxy>, Error> {
        if let Some(generate_service) = &self.generate_service {
            let (client, server) = zx::Channel::create()?;
            if (generate_service)(service_name, server).await.is_err() {
                return Err(format_err!("Could not handl service {:?}", service_name));
            }

            Ok(ExternalServiceProxy {
                proxy: S::Proxy::from_channel(fasync::Channel::from_channel(client)?),
                publisher: self.make_publisher().await,
            })
        } else {
            Err(format_err!("No service generator"))
        }
    }

    /// Connect to a service at the given path and DiscoverableService.
    ///
    /// If a GenerateService was specified at creation, the name of the service marker will be used
    /// to generate a service and the path will be ignored.
    pub async fn connect_discoverable_path<S: DiscoverableService>(
        &self,
        path: &str,
    ) -> Result<ExternalServiceProxy<S::Proxy>, Error> {
        let proxy = if let Some(generate_service) = &self.generate_service {
            let (client, server) = zx::Channel::create()?;
            ((generate_service)(S::SERVICE_NAME, server)).await?;
            S::Proxy::from_channel(fasync::Channel::from_channel(client)?)
        } else {
            connect_to_service_at_path::<S>(path)?
        };

        Ok(ExternalServiceProxy { proxy, publisher: self.make_publisher().await })
    }

    /// Connect to a service at the given path and ServiceMarker.
    ///
    /// If a GenerateService was specified at creation, the name of the service marker will be used
    /// to generate a service and the path will be ignored.
    pub async fn connect_path<S: ServiceMarker>(
        &self,
        path: &str,
    ) -> Result<ExternalServiceProxy<S::Proxy>, Error> {
        let (proxy, server) = fidl::endpoints::create_proxy::<S>()?;
        fdio::service_connect(path, server.into_channel())?;

        Ok(ExternalServiceProxy { proxy, publisher: self.make_publisher().await })
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
    ) -> Result<ExternalServiceProxy<S::Proxy>, Error> {
        if self.generate_service.is_some() {
            // If a generate_service is already specified, just connect through there
            return self.connect::<S>().await;
        }

        let found_path = glob(glob_pattern)?
            .filter_map(|entry| entry.ok())
            .next()
            .ok_or_else(|| format_err!("failed to enumerate devices"))?;

        let path_str =
            found_path.to_str().ok_or_else(|| format_err!("failed to convert path to str"))?;

        Ok(ExternalServiceProxy {
            proxy: connect_to_service_at_path::<S>(path_str)?,
            publisher: self.make_publisher().await,
        })
    }

    pub async fn wrap_proxy<P: Proxy>(&self, proxy: P) -> ExternalServiceProxy<P> {
        ExternalServiceProxy { proxy, publisher: self.make_publisher().await }
    }
}

/// A wrapper around a proxy, used to track disconnections.
///
/// This wraps any type implementing `Proxy`. Whenever any call returns a
/// `ClientChannelClosed` error, this wrapper publishes a closed event for
/// the wrapped proxy.
#[derive(Clone, Debug)]
pub struct ExternalServiceProxy<P>
where
    P: Proxy,
{
    proxy: P,
    publisher: Option<Publisher>,
}

impl<P> ExternalServiceProxy<P>
where
    P: Proxy,
{
    #[cfg(test)]
    pub fn new(proxy: P, publisher: Option<Publisher>) -> Self {
        Self { proxy, publisher }
    }

    fn inspect_result<T>(&self, result: &Result<T, fidl::Error>) {
        if let Err(fidl::Error::ClientChannelClosed { .. }) = result {
            self.publisher.as_ref().map(|p| p.send_event(Event::Closed(P::Service::DEBUG_NAME)));
        }
    }

    /// Make a call to a synchronous API of the wrapped proxy.
    pub fn call<T, F>(&self, func: F) -> Result<T, fidl::Error>
    where
        F: FnOnce(&P) -> Result<T, fidl::Error>,
    {
        let result = func(&self.proxy);
        self.inspect_result(&result);
        result
    }

    /// Nake a call to an asynchronous API of the wrapped proxy.
    pub async fn call_async<T, F, Fut>(&self, func: F) -> Result<T, fidl::Error>
    where
        F: FnOnce(&P) -> Fut,
        Fut: Future<Output = Result<T, fidl::Error>>,
    {
        let result = func(&self.proxy).await;
        self.inspect_result(&result);
        result
    }
}

/// Helper macro to simplify calls to proxy objects
#[macro_export]
macro_rules! call {
    ($proxy:expr => $($call:tt)+) => {
        $proxy.call(|proxy| proxy.$($call)+)
    }
}

/// Helper macro to simplify async calls to proxy objects
#[macro_export]
macro_rules! call_async {
    ($proxy:expr => $($call:tt)+) => {
        $proxy.call_async(|proxy| proxy.$($call)+)
    }
}
