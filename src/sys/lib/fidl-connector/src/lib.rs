// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::{
        client::Client,
        endpoints::{DiscoverableService, Proxy, ServiceMarker},
    },
    fuchsia_component::client::connect_to_service_at_path,
    parking_lot::RwLock,
    std::{ops::Deref, sync::Arc},
};

const SVC_DIR: &str = "/svc";

/// A trait that manages connecting to service.
pub trait Connect {
    /// Connect to this FIDL service.
    type Proxy: Proxy;

    /// Connect to the proxy, or return an error.
    fn connect(&self) -> Result<Self::Proxy, anyhow::Error>;
}

/// A `Connect` implementation that will try to reconnect to a FIDL service if the channel has
/// received a peer closed signal. This means it is possible `ServiceReconnector` to return a
/// closed channel, but it should eventually reconnect once the FIDL service is restarted.
#[derive(Clone)]
pub struct ServiceReconnector<S>
where
    S: DiscoverableService,
    <S as ServiceMarker>::Proxy: Clone + Deref<Target = Client>,
{
    inner: Arc<ServiceReconnectorInner<S>>,
}

impl<S> ServiceReconnector<S>
where
    S: DiscoverableService,
    <S as ServiceMarker>::Proxy: Clone + Deref<Target = Client>,
{
    /// Return a FIDL service connector at the default service directory in the
    /// application's root namespace.
    pub fn new() -> Self {
        Self::with_service_at(SVC_DIR)
    }

    /// Return a FIDL service connector at the specified service directory in
    /// the application's root namespace.
    ///
    /// The service directory path must be an absolute path.
    pub fn with_service_at(service_directory_path: &str) -> Self {
        let service_path = format!("{}/{}", service_directory_path, S::SERVICE_NAME);
        Self::with_service_at_path(service_path)
    }

    /// Return a FIDL service connector at the specified service path.
    pub fn with_service_at_path<P: Into<String>>(service_path: P) -> Self {
        let service_path = service_path.into();
        Self { inner: Arc::new(ServiceReconnectorInner { proxy: RwLock::new(None), service_path }) }
    }
}

impl<S> Connect for ServiceReconnector<S>
where
    S: DiscoverableService,
    <S as ServiceMarker>::Proxy: Clone + Deref<Target = Client>,
{
    type Proxy = S::Proxy;

    fn connect(&self) -> Result<Self::Proxy, anyhow::Error> {
        self.inner.connect()
    }
}

struct ServiceReconnectorInner<S>
where
    S: ServiceMarker,
    <S as ServiceMarker>::Proxy: Clone + Deref<Target = Client>,
{
    proxy: RwLock<Option<<S as ServiceMarker>::Proxy>>,
    service_path: String,
}

impl<S> Connect for ServiceReconnectorInner<S>
where
    S: DiscoverableService,
    <S as ServiceMarker>::Proxy: Clone + Deref<Target = Client>,
{
    type Proxy = S::Proxy;

    fn connect(&self) -> Result<Self::Proxy, anyhow::Error> {
        if let Some(ref proxy) = *self.proxy.read() {
            // Note: `.is_closed()` only returns true if we've observed a peer
            // closed on the channel. So if the caller hasn't tried to interact
            // with the proxy, we won't actually know if this proxy is closed.
            if !proxy.is_closed() {
                return Ok(proxy.clone());
            }
        }

        // We didn't connect, so grab the write mutex. Note it's possible we've
        // lost a race with another connection, so we need to re-check if the
        // proxy was closed.
        let mut proxy = self.proxy.write();
        if let Some(ref proxy) = *proxy {
            if !proxy.is_closed() {
                return Ok(proxy.clone());
            }
        }

        let p = connect_to_service_at_path::<S>(&self.service_path)?;
        *proxy = Some(p.clone());
        Ok(p)
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl_test_fidl_connector::{TestMarker, TestRequest, TestRequestStream},
        fuchsia_async as fasync,
        fuchsia_component::server::ServiceFs,
        fuchsia_zircon as zx,
        futures::prelude::*,
        std::cell::Cell,
    };

    #[fasync::run_singlethreaded(test)]
    async fn test_service_reconnector() {
        let ns = fdio::Namespace::installed().expect("installed namespace");
        let service_device_path = "/test/service_connector/svc";
        let c = ServiceReconnector::<TestMarker>::with_service_at(service_device_path);
        let (service_channel, server_end) = zx::Channel::create().expect("create channel");
        ns.bind(&service_device_path, service_channel).expect("bind test svc");

        // In order to test that we reconnect, we create a mock service that
        // closes the connection if the `disconnect` method is called in order
        // to test if we created a new connection.
        let gen = Cell::new(1);

        let mut fs = ServiceFs::new_local();
        fs.add_fidl_service(move |mut stream: TestRequestStream| {
            let current_gen = gen.get();
            gen.set(current_gen + 1);
            fasync::Task::local(async move {
                while let Some(req) = stream.try_next().await.unwrap_or(None) {
                    match req {
                        TestRequest::Ping { responder } => {
                            responder.send(current_gen).expect("patient client");
                        }
                        TestRequest::Disconnect { responder } => {
                            // Close the response.
                            drop(responder);
                        }
                    }
                }
            })
            .detach()
        })
        .serve_connection(server_end)
        .expect("serve_connection");

        fasync::Task::local(fs.collect()).detach();

        let proxy = c.connect().expect("can connect");
        assert_eq!(proxy.ping().await.expect("ping"), 1);

        let proxy = c.connect().expect("can connect");
        assert_eq!(proxy.ping().await.expect("ping"), 1);

        proxy.disconnect().await.expect_err("oops");

        let proxy = c.connect().expect("can connect");
        assert_eq!(proxy.ping().await.expect("ping"), 2);
    }
}
