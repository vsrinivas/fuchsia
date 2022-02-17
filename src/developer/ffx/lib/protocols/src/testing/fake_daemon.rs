// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        Context, DaemonProtocolProvider, FidlProtocol, NameToStreamHandlerMap, ProtocolRegister,
        StreamHandler,
    },
    anyhow::{anyhow, bail, Context as _, Result},
    async_trait::async_trait,
    ffx_daemon_events::TargetInfo,
    ffx_daemon_target::target::Target,
    ffx_daemon_target::target_collection::TargetCollection,
    fidl::endpoints::{DiscoverableProtocolMarker, ProtocolMarker, Proxy, Request, RequestStream},
    fidl::server::ServeInner,
    fidl_fuchsia_developer_bridge as bridge, fidl_fuchsia_developer_remotecontrol as rcs,
    fidl_fuchsia_diagnostics as diagnostics,
    futures::future::LocalBoxFuture,
    futures::prelude::*,
    std::cell::{Cell, RefCell},
    std::rc::Rc,
    std::sync::Arc,
};

#[derive(Default)]
struct InjectedStreamHandler<F: FidlProtocol> {
    started: Cell<bool>,
    stopped: Cell<bool>,
    inner: Rc<RefCell<F>>,
}

impl<F: FidlProtocol> InjectedStreamHandler<F> {
    fn new(inner: Rc<RefCell<F>>) -> Self {
        Self { inner, started: Cell::new(false), stopped: Cell::new(false) }
    }
}

#[async_trait(?Send)]
impl<F: 'static + FidlProtocol> StreamHandler for InjectedStreamHandler<F> {
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
        let mut stream =
            <<F as FidlProtocol>::Protocol as ProtocolMarker>::RequestStream::from_inner(
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

#[derive(Clone)]
pub struct FakeDaemon {
    register: Option<ProtocolRegister>,
    target_collection: Rc<TargetCollection>,
    rcs_handler: Option<Arc<dyn Fn(rcs::RemoteControlRequest, Option<String>)>>,
}

impl FakeDaemon {
    pub async fn open_proxy<P: DiscoverableProtocolMarker>(&self) -> P::Proxy {
        let client = fidl::AsyncChannel::from_channel(
            self.open_protocol(P::PROTOCOL_NAME.to_string()).await.unwrap(),
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

impl Default for FakeDaemon {
    fn default() -> Self {
        FakeDaemon {
            register: Default::default(),
            target_collection: Default::default(),
            rcs_handler: Default::default(),
        }
    }
}

#[async_trait(?Send)]
impl DaemonProtocolProvider for FakeDaemon {
    async fn open_protocol(&self, protocol_name: String) -> Result<fidl::Channel> {
        let (server, client) = fidl::Channel::create().context("creating channel")?;
        self.register
            .as_ref()
            .unwrap()
            .open(
                protocol_name,
                Context::new(self.clone()),
                fidl::AsyncChannel::from_channel(server)?,
            )
            .await?;
        Ok(client)
    }

    async fn open_remote_control(
        &self,
        target_identifier: Option<String>,
    ) -> Result<rcs::RemoteControlProxy> {
        if let Some(rcs_handler) = self.rcs_handler.clone() {
            let (client, server) = fidl::endpoints::create_endpoints::<rcs::RemoteControlMarker>()
                .context("creating endpoints")?;
            fuchsia_async::Task::local(async move {
                let mut server = server.into_stream().unwrap();

                while let Some(Ok(e)) = server.next().await {
                    rcs_handler(e, target_identifier.clone());
                }
            })
            .detach();
            Ok(client.into_proxy()?)
        } else {
            Err(anyhow!("FakeDaemon was not provided with an RCS implementation"))
        }
    }

    async fn open_target_proxy(
        &self,
        target_identifier: Option<String>,
        protocol_selector: diagnostics::Selector,
    ) -> Result<fidl::Channel> {
        let (_, res) =
            self.open_target_proxy_with_info(target_identifier, protocol_selector).await?;
        Ok(res)
    }

    async fn open_target_proxy_with_info(
        &self,
        target_identifier: Option<String>,
        protocol_selector: diagnostics::Selector,
    ) -> Result<(bridge::TargetInfo, fidl::Channel)> {
        // TODO(awdavies): This is likely very fragile. Explore more edge cases
        // to make sure tests don't panic unnecessarily.
        let protocol_name: String =
            match protocol_selector.tree_selector.ok_or(anyhow!("expected tree selector"))? {
                diagnostics::TreeSelector::PropertySelector(p) => match p.target_properties {
                    diagnostics::StringSelector::StringPattern(s) => s,
                    diagnostics::StringSelector::ExactMatch(s) => s,
                    _ => bail!("unknown target properties"),
                },
                _ => bail!("invalid selector"),
            };
        // This could cause some issues if we're building tests that expect targets that
        // are or aren't supposed to be connected. That would require using the
        // `get_connected` function here. For the time being there seems to be the
        // assumption that any target being added is going to be looked up later for
        // a test.
        Ok((
            bridge::TargetInfo::from(
                &*self
                    .target_collection
                    .get(target_identifier.clone())
                    .ok_or(anyhow!("couldn't find target for query: {:?}", target_identifier))?,
            ),
            self.open_protocol(protocol_name).await?,
        ))
    }

    async fn get_target_collection(&self) -> Result<Rc<TargetCollection>> {
        Ok(self.target_collection.clone())
    }
}

pub struct FakeDaemonBuilder {
    map: NameToStreamHandlerMap,
    target_collection: Rc<TargetCollection>,
    rcs_handler: Option<Arc<dyn Fn(rcs::RemoteControlRequest, Option<String>)>>,
}

impl FakeDaemonBuilder {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn target(self, target: bridge::TargetInfo) -> Self {
        let t = TargetInfo {
            nodename: target.nodename,
            addresses: target
                .addresses
                .map(|a| a.into_iter().map(Into::into).collect())
                .unwrap_or(Vec::new()),
            ssh_host_address: target.ssh_host_address.map(|a| a.address),
            ..Default::default()
        };
        let _ = self.target_collection.merge_insert(Target::from_target_info(t.into()));
        self
    }

    pub fn rcs_handler<F: Fn(rcs::RemoteControlRequest, Option<String>) + 'static>(
        mut self,
        f: F,
    ) -> Self {
        if self.rcs_handler.replace(Arc::new(f)).is_some() {
            panic!("Multiple RCS handlers registered");
        }
        self
    }

    pub fn register_instanced_protocol_closure<P, F>(mut self, f: F) -> Self
    where
        P: DiscoverableProtocolMarker,
        F: Fn(&Context, Request<P>) -> Result<()> + 'static,
    {
        if let Some(_) = self.map.insert(
            P::PROTOCOL_NAME.to_owned(),
            Box::new(ClosureStreamHandler::<P> { func: Rc::new(f) }),
        ) {
            panic!("duplicate protocol registered: {:#?}", P::PROTOCOL_NAME);
        }
        self
    }

    pub fn register_fidl_protocol<F: FidlProtocol>(mut self) -> Self
    where
        F: 'static,
    {
        if let Some(_) = self
            .map
            .insert(F::Protocol::PROTOCOL_NAME.to_owned(), Box::new(F::StreamHandler::default()))
        {
            panic!("duplicate protocol registered under: {}", F::Protocol::PROTOCOL_NAME);
        }
        self
    }

    /// This is similar to [register_fidl_protocol], but instead of managing the
    /// instantiation of the protocol using the defaults, the client instantiates
    /// this protocol instance. This is useful if the client wants to have access
    /// to some inner state for testing like a channel.
    pub fn inject_fidl_protocol<F: FidlProtocol>(mut self, svc: Rc<RefCell<F>>) -> Self
    where
        F: 'static,
    {
        if let Some(_) = self.map.insert(
            F::Protocol::PROTOCOL_NAME.to_owned(),
            Box::new(InjectedStreamHandler::new(svc)),
        ) {
            panic!("duplicate protocol registered under: {}", F::Protocol::PROTOCOL_NAME);
        }
        self
    }

    pub fn build(self) -> FakeDaemon {
        FakeDaemon {
            register: Some(ProtocolRegister::new(self.map)),
            rcs_handler: self.rcs_handler,
            target_collection: self.target_collection,
            ..Default::default()
        }
    }
}

impl Default for FakeDaemonBuilder {
    fn default() -> Self {
        Self {
            map: Default::default(),
            rcs_handler: Default::default(),
            target_collection: Default::default(),
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
            .register_instanced_protocol_closure::<ffx_test::NoopMarker, _>(|_, req| match req {
                ffx_test::NoopRequest::DoNoop { responder } => responder.send().map_err(Into::into),
            })
            .build();
        f.shutdown().await.unwrap();
        assert!(f.shutdown().await.is_err());
    }
}
