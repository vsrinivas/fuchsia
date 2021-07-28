// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::Context,
    anyhow::{anyhow, Result},
    async_lock::RwLock,
    async_trait::async_trait,
    async_utils::async_once::Once,
    core::marker::PhantomData,
    fidl::endpoints::{DiscoverableProtocolMarker, ProtocolMarker, Request, RequestStream},
    fidl::server::ServeInner,
    futures::future::{LocalBoxFuture, Shared},
    futures::prelude::*,
    std::rc::Rc,
    std::sync::Arc,
};

#[async_trait(?Send)]
pub trait FidlService: Unpin + Default {
    type Service: DiscoverableProtocolMarker;
    type StreamHandler: StreamHandler + Default;

    async fn serve<'a>(
        &'a self,
        cx: &'a Context,
        mut stream: <Self::Service as ProtocolMarker>::RequestStream,
    ) -> Result<()> {
        while let Ok(Some(req)) = stream.try_next().await {
            self.handle(cx, req).await?
        }
        Ok(())
    }

    async fn handle(&self, cx: &Context, req: Request<Self::Service>) -> Result<()>;

    async fn start(&mut self, _cx: &Context) -> Result<()> {
        Ok(())
    }

    async fn stop(&mut self, _cx: &Context) -> Result<()> {
        Ok(())
    }
}

pub struct FidlInstancedStreamHandler<F: FidlService> {
    _service: PhantomData<F>,
}

impl<F> Default for FidlInstancedStreamHandler<F>
where
    F: FidlService,
{
    fn default() -> Self {
        FidlInstancedStreamHandler { _service: PhantomData }
    }
}

#[async_trait(?Send)]
pub trait StreamHandler {
    /// Starts the service, if possible. For instanced services this would be
    /// a no-op as the service would no longer later be usable by any caller.
    async fn start(&self, cx: Context) -> Result<()>;

    /// Called when opening a new stream. This stream may be shutdown outside
    /// of the control of this object by the service register.
    async fn open(
        &self,
        cx: Context,
        server: Arc<ServeInner>,
    ) -> Result<LocalBoxFuture<'static, Result<()>>>;

    /// Called after every other stream handled via the `open` function has
    /// been shut down.
    async fn shutdown(&self, cx: &Context) -> Result<()>;
}

#[async_trait(?Send)]
impl<F> StreamHandler for FidlInstancedStreamHandler<F>
where
    F: FidlService + 'static,
{
    /// This is a no-op, as the service cannot be interacted with by the caller
    /// afterwards.
    async fn start(&self, _cx: Context) -> Result<()> {
        Ok(())
    }

    /// Default implementation for the stream handler. The future returned by this
    /// lives for the lifetime of the stream represented by `server`
    async fn open(
        &self,
        cx: Context,
        server: Arc<ServeInner>,
    ) -> Result<LocalBoxFuture<'static, Result<()>>> {
        let stream = <F::Service as ProtocolMarker>::RequestStream::from_inner(server, false);
        let mut svc = F::default();
        svc.start(&cx).await?;
        let fut = Box::pin(async move {
            let serve_res = svc.serve(&cx, stream).await.map_err(|e| {
                log::warn!("service failure while handling stream. Stopping service: {:?}", e);
                e
            });
            svc.stop(&cx).await?;
            serve_res
        });
        Ok(fut)
    }

    async fn shutdown(&self, _cx: &Context) -> Result<()> {
        Ok(())
    }
}

type StartFut = Shared<LocalBoxFuture<'static, Rc<Result<()>>>>;

#[derive(Default)]
pub struct FidlStreamHandler<F: FidlService> {
    service: Rc<RwLock<Option<F>>>,
    start_fut: Once<StartFut>,
}

impl<F: FidlService> FidlStreamHandler<F>
where
    F: 'static,
{
    fn make_start_fut(&self, cx: Context) -> Shared<LocalBoxFuture<'static, Rc<Result<()>>>> {
        let inner = Rc::downgrade(&self.service);
        async move {
            // In order to have a clonable result here, this needs
            // to return an Rc of the error which is then later
            // transposed into another error (see error below).
            //
            // TODO(awdavies): Implement this using a singleflight
            // mechanism, allowing for the service to attempt to
            // start more than once.
            if let Some(inner) = inner.upgrade() {
                if let Some(ref mut inner) = *inner.write().await {
                    Rc::new(inner.start(&cx).await)
                } else {
                    Rc::new(Err(anyhow!("singleton has been shut down")))
                }
            } else {
                Rc::new(Err(anyhow!("lost Rc<_> to service singleton")))
            }
        }
        .boxed_local()
        .shared()
    }

    async fn start_service(&self, cx: &Context) -> Result<()> {
        let cx = cx.clone();
        let fut = self
            .start_fut
            .get_or_init(async move {
                self.service.write().await.replace(F::default());
                self.make_start_fut(cx)
            })
            .await
            .clone();

        let res = fut.await;
        match &*res {
            Ok(_) => Ok(()),
            Err(e) => Err(anyhow!("singleton fut err: {:#?}", e)),
        }
    }
}

#[async_trait(?Send)]
impl<F> StreamHandler for FidlStreamHandler<F>
where
    F: FidlService + 'static,
{
    /// Starts the service at most once. Invoking this more than once will
    /// return the same result each time (including errors).
    async fn start(&self, cx: Context) -> Result<()> {
        self.start_service(&cx).await
    }

    async fn open(
        &self,
        cx: Context,
        server: Arc<ServeInner>,
    ) -> Result<LocalBoxFuture<'static, Result<()>>> {
        self.start_service(&cx).await?;
        let stream = <F::Service as ProtocolMarker>::RequestStream::from_inner(server, false);
        let service = Rc::downgrade(&self.service);
        let fut = async move {
            if let Some(service) = service.upgrade() {
                service
                    .read()
                    .await
                    .as_ref()
                    .ok_or(anyhow!("service has been shutdown"))?
                    .serve(&cx, stream)
                    .await
            } else {
                log::debug!("dropped singleton service Rc<_>");
                Ok(())
            }
        };
        Ok(Box::pin(fut))
    }

    async fn shutdown(&self, cx: &Context) -> Result<()> {
        self.service.write().await.take().ok_or(anyhow!("service has been stopped"))?.stop(cx).await
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::testing::FakeDaemonBuilder;
    use anyhow::anyhow;
    use fidl_fuchsia_ffx_test as ffx_test;
    use std::sync::atomic::{AtomicU64, Ordering};

    fn noop_service_closure() -> impl Fn(&Context, ffx_test::NoopRequest) -> Result<()> {
        |_, req| match req {
            ffx_test::NoopRequest::DoNoop { responder } => responder.send().map_err(Into::into),
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn noop_test() {
        let daemon = FakeDaemonBuilder::new()
            .register_instanced_service_closure::<ffx_test::NoopMarker, _>(noop_service_closure())
            .build();
        let proxy = daemon.open_proxy::<ffx_test::NoopMarker>().await;
        assert!(proxy.do_noop().await.is_ok());
        daemon.shutdown().await.unwrap();
    }

    #[derive(Default)]
    struct CounterService {
        count: AtomicU64,
    }

    #[async_trait(?Send)]
    impl FidlService for CounterService {
        type Service = ffx_test::CounterMarker;
        type StreamHandler = FidlInstancedStreamHandler<Self>;

        async fn handle(&self, cx: &Context, req: ffx_test::CounterRequest) -> Result<()> {
            // This is just here for some additional stress.
            let noop_proxy = cx
                .open_target_proxy::<ffx_test::NoopMarker>(
                    None,
                    "core/appmgr:out:fuchsia.ffx.test.Noop",
                )
                .await?;
            noop_proxy.do_noop().await?;
            match req {
                ffx_test::CounterRequest::AddOne { responder } => {
                    self.count.fetch_add(1, Ordering::SeqCst);
                    responder.send().map_err(Into::into)
                }
                ffx_test::CounterRequest::GetCount { responder } => {
                    responder.send(self.count.load(Ordering::SeqCst)).map_err(Into::into)
                }
            }
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn counter_test() {
        let daemon = FakeDaemonBuilder::new()
            .register_instanced_service_closure::<ffx_test::NoopMarker, _>(noop_service_closure())
            .register_fidl_service::<CounterService>()
            .build();
        let counter_proxy = daemon.open_proxy::<ffx_test::CounterMarker>().await;
        counter_proxy.add_one().await.unwrap();
        counter_proxy.add_one().await.unwrap();
        counter_proxy.add_one().await.unwrap();
        assert_eq!(3, counter_proxy.get_count().await.unwrap());
        drop(counter_proxy);
        // Ensure no state is kept between instances.
        let counter_proxy = daemon.open_proxy::<ffx_test::CounterMarker>().await;
        assert_eq!(0, counter_proxy.get_count().await.unwrap());
    }

    /// This is a struct that panics if stop hasn't been called. When attempting
    /// to run `serve` it will always err as well.
    #[derive(Default)]
    struct NoopServicePanicker {
        stopped: bool,
    }

    #[async_trait(?Send)]
    impl FidlService for NoopServicePanicker {
        type Service = ffx_test::NoopMarker;
        type StreamHandler = FidlInstancedStreamHandler<Self>;

        async fn handle(&self, _cx: &Context, _req: ffx_test::NoopRequest) -> Result<()> {
            Err(anyhow!("this is intended to fail every time"))
        }

        async fn stop(&mut self, _cx: &Context) -> Result<()> {
            self.stopped = true;
            Ok(())
        }
    }

    impl Drop for NoopServicePanicker {
        fn drop(&mut self) {
            assert!(self.stopped)
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn service_error_ensures_drop_test() -> Result<()> {
        let daemon =
            FakeDaemonBuilder::new().register_fidl_service::<NoopServicePanicker>().build();
        let proxy = daemon.open_proxy::<ffx_test::NoopMarker>().await;
        assert!(proxy.do_noop().await.is_err());
        daemon.shutdown().await
    }

    #[derive(Default)]
    struct SingletonCounterService {
        count: AtomicU64,
        started: bool,
    }

    #[async_trait(?Send)]
    impl FidlService for SingletonCounterService {
        type Service = ffx_test::CounterMarker;
        type StreamHandler = FidlStreamHandler<Self>;

        async fn handle(&self, cx: &Context, req: ffx_test::CounterRequest) -> Result<()> {
            // This is just here for some additional stress.
            let noop_proxy = cx
                .open_target_proxy::<ffx_test::NoopMarker>(
                    None,
                    "core/appmgr:out:fuchsia.ffx.test.Noop",
                )
                .await?;
            noop_proxy.do_noop().await?;
            match req {
                ffx_test::CounterRequest::AddOne { responder } => {
                    self.count.fetch_add(1, Ordering::SeqCst);
                    responder.send().map_err(Into::into)
                }
                ffx_test::CounterRequest::GetCount { responder } => {
                    responder.send(self.count.load(Ordering::SeqCst)).map_err(Into::into)
                }
            }
        }

        async fn start(&mut self, cx: &Context) -> Result<()> {
            let noop_proxy = cx
                .open_target_proxy::<ffx_test::NoopMarker>(
                    None,
                    "core/appmgr:out:fuchsia.ffx.test.Noop",
                )
                .await
                .unwrap();
            noop_proxy.do_noop().await.unwrap();
            if self.started {
                panic!("this can only be run once");
            }
            self.started = true;
            Ok(())
        }

        async fn stop(&mut self, _cx: &Context) -> Result<()> {
            if !self.started {
                panic!("this must be started before being shut down");
            }
            Ok(())
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn counter_test_singleton() {
        let daemon = FakeDaemonBuilder::new()
            .register_instanced_service_closure::<ffx_test::NoopMarker, _>(noop_service_closure())
            .register_fidl_service::<SingletonCounterService>()
            .build();
        let (counter_proxy1, counter_proxy2, counter_proxy3) = futures::join!(
            daemon.open_proxy::<ffx_test::CounterMarker>(),
            daemon.open_proxy::<ffx_test::CounterMarker>(),
            daemon.open_proxy::<ffx_test::CounterMarker>()
        );
        assert_eq!(0, counter_proxy1.get_count().await.unwrap());
        assert_eq!(0, counter_proxy2.get_count().await.unwrap());
        assert_eq!(0, counter_proxy3.get_count().await.unwrap());
        counter_proxy3.add_one().await.unwrap();
        assert_eq!(1, counter_proxy1.get_count().await.unwrap());
        assert_eq!(1, counter_proxy2.get_count().await.unwrap());
        assert_eq!(1, counter_proxy3.get_count().await.unwrap());
        counter_proxy1.add_one().await.unwrap();
        assert_eq!(2, counter_proxy1.get_count().await.unwrap());
        assert_eq!(2, counter_proxy2.get_count().await.unwrap());
        assert_eq!(2, counter_proxy3.get_count().await.unwrap());
        drop(counter_proxy1);
        drop(counter_proxy2);
        drop(counter_proxy3);
        let counter_proxy = daemon.open_proxy::<ffx_test::CounterMarker>().await;
        assert_eq!(2, counter_proxy.get_count().await.unwrap());
        daemon.shutdown().await.unwrap();
    }
}
