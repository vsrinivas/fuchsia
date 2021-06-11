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
type ServiceHandleMap = HashMap<usize, ServiceHandle>;
type NameToServiceHandleMap = HashMap<String, ServiceHandleMap>;

#[derive(Default)]
struct ServiceRegisterInner {
    service_map: NameToStreamHandlerMap,
    handles: RefCell<NameToServiceHandleMap>,
    next_task_id: AtomicUsize,
    stopping: AtomicBool,
}

impl ServiceRegisterInner {
    fn drain_handles(&self) -> Vec<LabeledServiceHandle> {
        let mut res = Vec::new();
        for (name, mut map) in self.handles.borrow_mut().drain() {
            for (id, t) in map.drain() {
                let name = name.clone();
                res.push(LabeledServiceHandle { name, id, inner: t });
            }
        }
        res
    }

    fn remove_handle(&self, s: &String, id: &usize) -> Option<ServiceHandle> {
        self.handles.borrow_mut().get_mut(s).and_then(|e| e.remove(id))
    }
}

struct ServiceHandle {
    // This is just the handle to the stream that is being handled inside
    // `self.task`. Ideally the task will complete in a reasonable amount of
    // time after this is called.
    handle: Weak<ServeInner>,
    task: Task<Result<()>>,
}

/// Service handle with more debug information.
struct LabeledServiceHandle {
    name: String,
    id: usize,
    inner: ServiceHandle,
}

impl LabeledServiceHandle {
    pub(crate) async fn shutdown(self) -> Result<()> {
        self.inner.shutdown().await
    }
}

impl ServiceHandle {
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
pub enum ServiceError {
    #[error("service error: {0:?}")]
    StreamOpenError(#[from] anyhow::Error),
    #[error("bad service register state: {0:?}")]
    BadRegisterState(String),
    #[error("could not find service under the name: {0}")]
    NoServiceFound(String),
    #[error("duplicate task id found under service {0}: {1}")]
    DuplicateTaskId(String, usize),
}

#[derive(Default, Clone)]
pub struct ServiceRegister {
    inner: Rc<ServiceRegisterInner>,
}

impl ServiceRegister {
    pub fn new(map: NameToStreamHandlerMap) -> Self {
        // TODO(awdavies): Start the static services. Probably need the daemon
        // to just do this on its own.
        Self { inner: Rc::new(ServiceRegisterInner { service_map: map, ..Default::default() }) }
    }

    /// Returns an error if `self.stopping` has been set to true, otherwise
    /// returns `Ok(())`.
    fn invariant_check(&self) -> Result<(), ServiceError> {
        if self.inner.stopping.load(Ordering::SeqCst) {
            return Err(ServiceError::BadRegisterState(
                "Cannot start any services. Shutting down".to_string(),
            ));
        }
        Ok(())
    }

    pub async fn start(&self, name: String, cx: Context) -> Result<(), ServiceError> {
        self.invariant_check()?;
        let svc =
            self.inner.service_map.get(&name).ok_or(ServiceError::NoServiceFound(name.clone()))?;
        svc.start(cx).await.map_err(Into::into)
    }

    pub async fn open(
        &self,
        name: String,
        cx: Context,
        server_channel: fidl::AsyncChannel,
    ) -> Result<(), ServiceError> {
        self.invariant_check()?;
        let task_id = self.inner.next_task_id.fetch_add(1, Ordering::SeqCst);
        let svc =
            self.inner.service_map.get(&name).ok_or(ServiceError::NoServiceFound(name.clone()))?;
        let weak_inner = Rc::downgrade(&self.inner);
        let server = Arc::new(ServeInner::new(server_channel));
        let weak_server = Arc::downgrade(&server);
        let name_copy = name.clone();
        let fut = svc.open(cx, server).await?;
        let new_task = async move {
            fut.await.unwrap_or_else(|e| log::warn!("running service stream handler: {:#?}", e));
            if let Some(inner) = weak_inner.upgrade() {
                if let Some(handle) = inner.remove_handle(&name_copy, &task_id) {
                    // Closes the stream's handle to make sure the task
                    // completes cleanly.
                    let r = handle.shutdown().await;
                    log::info!(
                        "service stream for {}-{} finished with result: {:?}",
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
                    ServiceHandle { task: Task::local(new_task), handle: weak_server },
                ) {
                    return Err(ServiceError::DuplicateTaskId(name, task_id));
                }
            }
            Entry::Vacant(e) => {
                let mut new_map = HashMap::new();
                new_map.insert(
                    task_id,
                    ServiceHandle { task: Task::local(new_task), handle: weak_server },
                );
                e.insert(new_map);
            }
        }
        Ok(())
    }

    pub async fn shutdown(&self, cx: Context) -> Result<(), ServiceError> {
        if self
            .inner
            .stopping
            .compare_exchange(false, true, Ordering::SeqCst, Ordering::Acquire)
            .is_err()
        {
            return Err(ServiceError::BadRegisterState(
                "already shutting down ServiceRegister".to_string(),
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
        let mut service_futs = Vec::new();
        for (name, svc) in self.inner.service_map.iter() {
            let name = name.clone();
            let cx = &cx;
            let fut = async move {
                log::debug!("shutting down stream handler for {}", name);
                svc.shutdown(cx)
                    .await
                    .unwrap_or_else(|e| log::warn!("closing stream handler for {}: {:?}", name, e));
            };
            service_futs.push(fut);
        }
        futures::future::join_all(service_futs).await;
        Ok(())
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::{DaemonServiceProvider, FidlService, FidlStreamHandler};
    use async_trait::async_trait;
    use fidl::endpoints::DiscoverableService;
    use fidl_fuchsia_diagnostics as diagnostics;
    use fidl_fuchsia_ffx_test as ffx_test;

    #[derive(Default, Clone)]
    struct TestDaemon;

    #[async_trait(?Send)]
    impl DaemonServiceProvider for TestDaemon {
        async fn open_service_proxy(&self, _name: String) -> Result<fidl::Channel> {
            unimplemented!()
        }

        async fn open_target_proxy(
            &self,
            _target_identifier: Option<String>,
            _service_selector: diagnostics::Selector,
        ) -> Result<fidl::Channel> {
            unimplemented!()
        }
    }

    #[derive(Default, Clone)]
    struct NoopService;

    #[async_trait(?Send)]
    impl FidlService for NoopService {
        type Service = ffx_test::NoopMarker;
        type StreamHandler = FidlStreamHandler<Self>;

        async fn handle(&self, _cx: &Context, req: ffx_test::NoopRequest) -> Result<()> {
            match req {
                ffx_test::NoopRequest::DoNoop { responder } => responder.send().map_err(Into::into),
            }
        }
    }

    fn create_noop_register() -> ServiceRegister {
        let service_string =
            <<NoopService as FidlService>::Service as DiscoverableService>::SERVICE_NAME.to_owned();
        let mut map = NameToStreamHandlerMap::new();
        map.insert(service_string.clone(), Box::new(FidlStreamHandler::<NoopService>::default()));
        ServiceRegister::new(map)
    }

    async fn create_noop_proxy() -> Result<(ffx_test::NoopProxy, ServiceRegister)> {
        let register = create_noop_register();
        let (noop_proxy, server) = fidl::endpoints::create_endpoints::<ffx_test::NoopMarker>()?;
        register
            .open(
                ffx_test::NoopMarker::SERVICE_NAME.to_owned(),
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
                ffx_test::NoopMarker::SERVICE_NAME.to_owned(),
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
