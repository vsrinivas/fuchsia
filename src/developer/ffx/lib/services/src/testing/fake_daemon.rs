// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        Context, DaemonServiceProvider, FidlService, NameToStreamHandlerMap, ServiceRegister,
        StreamHandler,
    },
    anyhow::{anyhow, bail, Context as _, Result},
    async_trait::async_trait,
    ffx_daemon_target::target_collection::TargetCollection,
    fidl::endpoints::{DiscoverableProtocolMarker, ProtocolMarker, Proxy, Request, RequestStream},
    fidl::server::ServeInner,
    fidl_fuchsia_developer_bridge as bridge, fidl_fuchsia_diagnostics as diagnostics,
    futures::future::LocalBoxFuture,
    futures::prelude::*,
    std::cell::{Cell, RefCell},
    std::rc::Rc,
    std::sync::Arc,
};

#[derive(Default)]
struct InjectedStreamHandler<F: FidlService> {
    started: Cell<bool>,
    stopped: Cell<bool>,
    inner: Rc<RefCell<F>>,
}

impl<F: FidlService> InjectedStreamHandler<F> {
    fn new(inner: Rc<RefCell<F>>) -> Self {
        Self { inner, started: Cell::new(false), stopped: Cell::new(false) }
    }
}

#[async_trait(?Send)]
impl<F: 'static + FidlService> StreamHandler for InjectedStreamHandler<F> {
    async fn start(&self, _cx: Context) -> Result<()> {
        Ok(())
    }

    async fn open(
        &self,
        cx: Context,
        server: Arc<ServeInner>,
    ) -> Result<LocalBoxFuture<'static, Result<()>>> {
        if !self.started.get() {
            self.inner.borrow_mut().start(&cx).await?;
            self.started.set(true);
        }
        let mut stream = <<F as FidlService>::Service as ProtocolMarker>::RequestStream::from_inner(
            server, false,
        );
        let inner = self.inner.clone();
        let fut = Box::pin(async move {
            while let Ok(Some(req)) = stream.try_next().await {
                inner.borrow().handle(&cx, req).await?
            }
            Ok(())
        });
        Ok(fut)
    }

    async fn shutdown(&self, cx: &Context) -> Result<()> {
        if !self.stopped.get() {
            self.inner.borrow_mut().stop(cx).await?;
        } else {
            panic!("can only be stopped once");
        }
        Ok(())
    }
}

pub struct ClosureStreamHandler<P: DiscoverableProtocolMarker> {
    func: Rc<dyn Fn(&Context, Request<P>) -> Result<()>>,
}

#[async_trait(?Send)]
impl<P> StreamHandler for ClosureStreamHandler<P>
where
    P: DiscoverableProtocolMarker,
{
    async fn start(&self, _cx: Context) -> Result<()> {
        Ok(())
    }

    async fn open(
        &self,
        cx: Context,
        server: Arc<ServeInner>,
    ) -> Result<LocalBoxFuture<'static, Result<()>>> {
        let mut stream = <P as ProtocolMarker>::RequestStream::from_inner(server, false);
        let weak_func = Rc::downgrade(&self.func);
        let fut = Box::pin(async move {
            while let Ok(Some(req)) = stream.try_next().await {
                if let Some(func) = weak_func.upgrade() {
                    (func)(&cx, req)?
                }
            }
            Ok(())
        });
        Ok(fut)
    }

    async fn shutdown(&self, _: &Context) -> Result<()> {
        Ok(())
    }
}

#[derive(Default, Clone)]
pub struct FakeDaemon {
    nodename: Option<String>,
    register: Option<ServiceRegister>,
    target_collection: Rc<TargetCollection>,
}

impl FakeDaemon {
    pub async fn open_proxy<P: DiscoverableProtocolMarker>(&self) -> P::Proxy {
        let client = fidl::AsyncChannel::from_channel(
            self.open_service_proxy(P::PROTOCOL_NAME.to_string()).await.unwrap(),
        )
        .unwrap();
        P::Proxy::from_channel(client)
    }

    pub async fn shutdown(&self) -> Result<()> {
        self.register
            .as_ref()
            .expect("must have register set")
            .shutdown(Context::new(self.clone()))
            .await
            .map_err(Into::into)
    }
}

#[async_trait(?Send)]
impl DaemonServiceProvider for FakeDaemon {
    async fn open_service_proxy(&self, service_name: String) -> Result<fidl::Channel> {
        let (server, client) = fidl::Channel::create().context("creating channel")?;
        self.register
            .as_ref()
            .unwrap()
            .open(
                service_name,
                Context::new(self.clone()),
                fidl::AsyncChannel::from_channel(server)?,
            )
            .await?;
        Ok(client)
    }

    async fn open_target_proxy(
        &self,
        target_identifier: Option<String>,
        service_selector: diagnostics::Selector,
    ) -> Result<fidl::Channel> {
        let (_, res) =
            self.open_target_proxy_with_info(target_identifier, service_selector).await?;
        Ok(res)
    }

    async fn open_target_proxy_with_info(
        &self,
        _target_identifier: Option<String>,
        service_selector: diagnostics::Selector,
    ) -> Result<(bridge::Target, fidl::Channel)> {
        // TODO(awdavies): This is likely very fragile. Explore more edge cases
        // to make sure tests don't panic unnecessarily.
        let service_name: String =
            match service_selector.tree_selector.ok_or(anyhow!("expected tree selector"))? {
                diagnostics::TreeSelector::PropertySelector(p) => match p.target_properties {
                    diagnostics::StringSelector::StringPattern(s) => s,
                    diagnostics::StringSelector::ExactMatch(s) => s,
                    _ => bail!("unknown target properties"),
                },
                _ => bail!("invalid selector"),
            };
        Ok((
            bridge::Target { nodename: self.nodename.clone(), ..bridge::Target::EMPTY },
            self.open_service_proxy(service_name).await?,
        ))
    }

    async fn get_target_collection(&self) -> Result<Rc<TargetCollection>> {
        Ok(self.target_collection.clone())
    }
}

#[derive(Default)]
pub struct FakeDaemonBuilder {
    map: NameToStreamHandlerMap,
    nodename: Option<String>,
}

impl FakeDaemonBuilder {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn nodename(mut self, nodename: String) -> Self {
        self.nodename = Some(nodename);
        self
    }

    pub fn register_instanced_service_closure<P, F>(mut self, f: F) -> Self
    where
        P: DiscoverableProtocolMarker,
        F: Fn(&Context, Request<P>) -> Result<()> + 'static,
    {
        if let Some(_) = self.map.insert(
            P::PROTOCOL_NAME.to_owned(),
            Box::new(ClosureStreamHandler::<P> { func: Rc::new(f) }),
        ) {
            panic!("duplicate service registered: {:#?}", P::PROTOCOL_NAME);
        }
        self
    }

    pub fn register_fidl_service<F: FidlService>(mut self) -> Self
    where
        F: 'static,
    {
        if let Some(_) = self
            .map
            .insert(F::Service::PROTOCOL_NAME.to_owned(), Box::new(F::StreamHandler::default()))
        {
            panic!("duplicate service registered under: {}", F::Service::PROTOCOL_NAME);
        }
        self
    }

    /// This is similar to [register_fidl_service], but instead of managing the
    /// instantiation of the service using the defaults, the client instantiates
    /// this service instance. This is useful if the client wants to have access
    /// to some inner state for testing like a channel.
    pub fn inject_fidl_service<F: FidlService>(mut self, svc: Rc<RefCell<F>>) -> Self
    where
        F: 'static,
    {
        if let Some(_) = self
            .map
            .insert(F::Service::PROTOCOL_NAME.to_owned(), Box::new(InjectedStreamHandler::new(svc)))
        {
            panic!("duplicate service registered under: {}", F::Service::PROTOCOL_NAME);
        }
        self
    }

    pub fn build(self) -> FakeDaemon {
        FakeDaemon {
            register: Some(ServiceRegister::new(self.map)),
            nodename: self.nodename,
            ..Default::default()
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_ffx_test as ffx_test;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_err_double_shutdown() {
        let f = FakeDaemonBuilder::new()
            .register_instanced_service_closure::<ffx_test::NoopMarker, _>(|_, req| match req {
                ffx_test::NoopRequest::DoNoop { responder } => responder.send().map_err(Into::into),
            })
            .build();
        f.shutdown().await.unwrap();
        assert!(f.shutdown().await.is_err());
    }
}
