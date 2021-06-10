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
    fidl::endpoints::{DiscoverableService, Proxy, Request, RequestStream, ServiceMarker},
    fidl::server::ServeInner,
    fidl_fuchsia_diagnostics as diagnostics,
    futures::future::LocalBoxFuture,
    futures::prelude::*,
    std::rc::Rc,
    std::sync::Arc,
};

pub struct ClosureStreamHandler<S: DiscoverableService> {
    func: Rc<dyn Fn(&Context, Request<S>) -> Result<()>>,
}

#[async_trait(?Send)]
impl<S> StreamHandler for ClosureStreamHandler<S>
where
    S: DiscoverableService,
{
    async fn open(
        &self,
        cx: Context,
        server: Arc<ServeInner>,
    ) -> Result<LocalBoxFuture<'static, Result<()>>> {
        let mut stream = <S as ServiceMarker>::RequestStream::from_inner(server, false);
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
    register: Option<ServiceRegister>,
}

impl FakeDaemon {
    pub async fn open_proxy<S: DiscoverableService>(&self) -> S::Proxy {
        let client = fidl::AsyncChannel::from_channel(
            self.open_service_proxy(S::SERVICE_NAME.to_string()).await.unwrap(),
        )
        .unwrap();
        S::Proxy::from_channel(client)
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
        _target_identifier: Option<String>,
        service_selector: diagnostics::Selector,
    ) -> Result<fidl::Channel> {
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
        self.open_service_proxy(service_name).await
    }
}

#[derive(Default)]
pub struct FakeDaemonBuilder {
    map: NameToStreamHandlerMap,
}

impl FakeDaemonBuilder {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn register_instanced_service_closure<S, F>(mut self, f: F) -> Self
    where
        S: DiscoverableService,
        F: Fn(&Context, Request<S>) -> Result<()> + 'static,
    {
        if let Some(_) = self.map.insert(
            S::SERVICE_NAME.to_owned(),
            Box::new(ClosureStreamHandler::<S> { func: Rc::new(f) }),
        ) {
            panic!("duplicate service registered: {:#?}", S::SERVICE_NAME);
        }
        self
    }

    pub fn register_fidl_service<F: FidlService>(mut self) -> Self
    where
        F: 'static,
    {
        if let Some(_) = self
            .map
            .insert(F::Service::SERVICE_NAME.to_owned(), Box::new(F::StreamHandler::default()))
        {
            panic!("duplicate service registered under: {}", F::Service::SERVICE_NAME);
        }
        self
    }

    pub fn build(self) -> FakeDaemon {
        FakeDaemon { register: Some(ServiceRegister::new(self.map)) }
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
