// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::{Context, StreamHandler},
    anyhow::Result,
    fidl::server::ServeInner,
    fuchsia_async::Task,
    std::cell::RefCell,
    std::collections::hash_map::Entry,
    std::collections::HashMap,
    std::rc::Rc,
    std::sync::atomic::{AtomicBool, AtomicUsize, Ordering},
    std::sync::{Arc, Weak},
    thiserror::Error,
};

pub type NameToStreamHandlerMap = HashMap<String, Box<dyn StreamHandler>>;
type ProtocolHandleMap = HashMap<usize, ProtocolHandle>;
type NameToProtocolHandleMap = HashMap<String, ProtocolHandleMap>;

#[derive(Default)]
struct ProtocolRegisterInner {
    protocol_map: NameToStreamHandlerMap,
    handles: RefCell<NameToProtocolHandleMap>,
    next_task_id: AtomicUsize,
    stopping: AtomicBool,
}

impl ProtocolRegisterInner {
    fn drain_handles(&self) -> Vec<LabeledProtocolHandle> {
        let mut res = Vec::new();
        for (name, mut map) in self.handles.borrow_mut().drain() {
            for (id, t) in map.drain() {
                let name = name.clone();
                res.push(LabeledProtocolHandle { name, id, inner: t });
            }
        }
        res
    }

    fn remove_handle(&self, s: &String, id: &usize) -> Option<ProtocolHandle> {
        self.handles.borrow_mut().get_mut(s).and_then(|e| e.remove(id))
    }
}

struct ProtocolHandle {
    // This is just the handle to the stream that is being handled inside
    // `self.task`. Ideally the task will complete in a reasonable amount of
    // time after this is called.
    handle: Weak<ServeInner>,
    task: Task<Result<()>>,
}

/// Protocol handle with more debug information.
struct LabeledProtocolHandle {
    name: String,
    id: usize,
    inner: ProtocolHandle,
}

impl LabeledProtocolHandle {
    pub(crate) async fn shutdown(self) -> Result<()> {
        self.inner.shutdown().await
    }
}

impl ProtocolHandle {
    pub(crate) async fn shutdown(self) -> Result<()> {
        if let Some(handle) = self.handle.upgrade() {
            handle.shutdown();
            self.task.await
        } else {
            Ok(())
        }
    }
}

#[derive(Error, Debug)]
pub enum ProtocolError {
    #[error("protocol error: {0:?}")]
    StreamOpenError(#[from] anyhow::Error),
    #[error("bad protocol register state: {0:?}")]
    BadRegisterState(String),
    #[error("could not find protocol under the name: {0}")]
    NoProtocolFound(String),
    #[error("duplicate task id found under protocol {0}: {1}")]
    DuplicateTaskId(String, usize),
}

#[derive(Default, Clone)]
pub struct ProtocolRegister {
    inner: Rc<ProtocolRegisterInner>,
}

impl ProtocolRegister {
    pub fn new(map: NameToStreamHandlerMap) -> Self {
        // TODO(awdavies): Start the static protocols. Probably need the daemon
        // to just do this on its own.
        Self { inner: Rc::new(ProtocolRegisterInner { protocol_map: map, ..Default::default() }) }
    }

    /// Returns an error if `self.stopping` has been set to true, otherwise
    /// returns `Ok(())`.
    fn invariant_check(&self) -> Result<(), ProtocolError> {
        if self.inner.stopping.load(Ordering::SeqCst) {
            return Err(ProtocolError::BadRegisterState(
                "Cannot start any protocols. Shutting down".to_string(),
            ));
        }
        Ok(())
    }

    pub async fn start(&self, name: String, cx: Context) -> Result<(), ProtocolError> {
        self.invariant_check()?;
        let svc = self
            .inner
            .protocol_map
            .get(&name)
            .ok_or(ProtocolError::NoProtocolFound(name.clone()))?;
        svc.start(cx).await.map_err(Into::into)
    }

    pub async fn open(
        &self,
        name: String,
        cx: Context,
        server_channel: fidl::AsyncChannel,
    ) -> Result<(), ProtocolError> {
        self.invariant_check()?;
        let task_id = self.inner.next_task_id.fetch_add(1, Ordering::SeqCst);
        let svc = self
            .inner
            .protocol_map
            .get(&name)
            .ok_or(ProtocolError::NoProtocolFound(name.clone()))?;
        let weak_inner = Rc::downgrade(&self.inner);
        let server = Arc::new(ServeInner::new(server_channel));
        let weak_server = Arc::downgrade(&server);
        let name_copy = name.clone();
        let fut = svc.open(cx, server).await?;
        let new_task = async move {
            fut.await.unwrap_or_else(|e| log::warn!("running protocol stream handler: {:#?}", e));
            if let Some(inner) = weak_inner.upgrade() {
                if let Some(handle) = inner.remove_handle(&name_copy, &task_id) {
                    // Closes the stream's handle to make sure the task
                    // completes cleanly.
                    let r = handle.shutdown().await;
                    log::info!(
                        "protocol stream for {}-{} finished with result: {:?}",
                        name_copy,
                        task_id,
                        r
                    );
                }
            }
            Ok(())
        };
        match self.inner.handles.borrow_mut().entry(name.clone()) {
            Entry::Occupied(mut e) => {
                if let Some(_s) = e.get_mut().insert(
                    task_id,
                    ProtocolHandle { task: Task::local(new_task), handle: weak_server },
                ) {
                    return Err(ProtocolError::DuplicateTaskId(name, task_id));
                }
            }
            Entry::Vacant(e) => {
                let mut new_map = HashMap::new();
                new_map.insert(
                    task_id,
                    ProtocolHandle { task: Task::local(new_task), handle: weak_server },
                );
                e.insert(new_map);
            }
        }
        Ok(())
    }

    pub async fn shutdown(&self, cx: Context) -> Result<(), ProtocolError> {
        if self
            .inner
            .stopping
            .compare_exchange(false, true, Ordering::SeqCst, Ordering::Acquire)
            .is_err()
        {
            return Err(ProtocolError::BadRegisterState(
                "already shutting down ProtocolRegister".to_string(),
            ));
        }

        let handler_futs = self
            .inner
            .drain_handles()
            .drain(..)
            .map(|h| async move {
                let name = h.name.clone();
                let id = h.id;
                log::debug!("shutting down handle {}-{}", name, id);
                h.shutdown()
                    .await
                    .unwrap_or_else(|e| log::warn!("shutdown for handle {}-{}: {:?}", name, id, e));
            })
            .collect::<Vec<_>>();
        futures::future::join_all(handler_futs).await;
        let mut protocol_futs = Vec::new();
        for (name, svc) in self.inner.protocol_map.iter() {
            let name = name.clone();
            let cx = &cx;
            let fut = async move {
                log::debug!("shutting down stream handler for {}", name);
                svc.shutdown(cx)
                    .await
                    .unwrap_or_else(|e| log::warn!("closing stream handler for {}: {:?}", name, e));
            };
            protocol_futs.push(fut);
        }
        futures::future::join_all(protocol_futs).await;
        Ok(())
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::{DaemonProtocolProvider, FidlProtocol, FidlStreamHandler};
    use async_trait::async_trait;
    use fidl::endpoints::DiscoverableProtocolMarker;
    use fidl_fuchsia_developer_bridge as bridge;
    use fidl_fuchsia_diagnostics as diagnostics;
    use fidl_fuchsia_ffx_test as ffx_test;

    #[derive(Default, Clone)]
    struct TestDaemon;

    #[async_trait(?Send)]
    impl DaemonProtocolProvider for TestDaemon {
        async fn open_protocol(&self, _name: String) -> Result<fidl::Channel> {
            unimplemented!()
        }

        async fn open_target_proxy(
            &self,
            _target_identifier: Option<String>,
            _protocol_selector: diagnostics::Selector,
        ) -> Result<fidl::Channel> {
            unimplemented!()
        }

        async fn open_target_proxy_with_info(
            &self,
            _target_identifier: Option<String>,
            _protocol_selector: diagnostics::Selector,
        ) -> Result<(bridge::TargetInfo, fidl::Channel)> {
            unimplemented!()
        }
    }

    #[derive(Default, Clone)]
    struct NoopProtocol;

    #[async_trait(?Send)]
    impl FidlProtocol for NoopProtocol {
        type Protocol = ffx_test::NoopMarker;
        type StreamHandler = FidlStreamHandler<Self>;

        async fn handle(&self, _cx: &Context, req: ffx_test::NoopRequest) -> Result<()> {
            match req {
                ffx_test::NoopRequest::DoNoop { responder } => responder.send().map_err(Into::into),
            }
        }
    }

    fn create_noop_register() -> ProtocolRegister {
        let protocol_string =
            <<NoopProtocol as FidlProtocol>::Protocol as DiscoverableProtocolMarker>::PROTOCOL_NAME
                .to_owned();
        let mut map = NameToStreamHandlerMap::new();
        map.insert(protocol_string.clone(), Box::new(FidlStreamHandler::<NoopProtocol>::default()));
        ProtocolRegister::new(map)
    }

    async fn create_noop_proxy() -> Result<(ffx_test::NoopProxy, ProtocolRegister)> {
        let register = create_noop_register();
        let (noop_proxy, server) = fidl::endpoints::create_endpoints::<ffx_test::NoopMarker>()?;
        register
            .open(
                ffx_test::NoopMarker::PROTOCOL_NAME.to_owned(),
                Context::new(TestDaemon::default()),
                fidl::AsyncChannel::from_channel(server.into_channel())?,
            )
            .await?;
        Ok((noop_proxy.into_proxy()?, register))
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_start_stop() -> Result<()> {
        let (noop_proxy, register) = create_noop_proxy().await?;
        noop_proxy.do_noop().await?;
        register.shutdown(Context::new(TestDaemon::default())).await?;
        assert!(noop_proxy.do_noop().await.is_err());
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_err_on_open_after_shutdown() -> Result<()> {
        let register = create_noop_register();
        let (noop_proxy, server) = fidl::endpoints::create_endpoints::<ffx_test::NoopMarker>()?;
        register.shutdown(Context::new(TestDaemon::default())).await?;
        let res = register
            .open(
                ffx_test::NoopMarker::PROTOCOL_NAME.to_owned(),
                Context::new(TestDaemon::default()),
                fidl::AsyncChannel::from_channel(server.into_channel())?,
            )
            .await;
        let noop_proxy = noop_proxy.into_proxy()?;
        assert!(res.is_err());
        assert!(noop_proxy.do_noop().await.is_err());
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_err_double_shutdown() -> Result<()> {
        let register = create_noop_register();
        register.shutdown(Context::new(TestDaemon::default())).await?;
        assert!(register.shutdown(Context::new(TestDaemon::default())).await.is_err());
        Ok(())
    }
}
