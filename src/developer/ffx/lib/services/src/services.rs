// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::Context,
    anyhow::Result,
    async_trait::async_trait,
    core::marker::PhantomData,
    fidl::endpoints::{DiscoverableService, Request, RequestStream, ServiceMarker},
    fidl::server::ServeInner,
    futures::future::LocalBoxFuture,
    futures::prelude::*,
    std::sync::Arc,
};

#[async_trait(?Send)]
pub trait FidlService: Unpin + Default {
    type Service: DiscoverableService;

    async fn serve<'a>(
        &'a self,
        cx: &'a Context,
        mut stream: <Self::Service as ServiceMarker>::RequestStream,
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

pub struct FidlStreamHandler<F: FidlService> {
    _service: PhantomData<F>,
}

impl<F> Default for FidlStreamHandler<F>
where
    F: FidlService,
{
    fn default() -> Self {
        FidlStreamHandler { _service: PhantomData }
    }
}

#[async_trait(?Send)]
pub trait StreamHandler {
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
impl<F> StreamHandler for FidlStreamHandler<F>
where
    F: FidlService + 'static,
{
    /// Default implementation for the stream handler. The future returned by this
    /// lives for the lifetime of the stream represented by `server`
    async fn open(
        &self,
        cx: Context,
        server: Arc<ServeInner>,
    ) -> Result<LocalBoxFuture<'static, Result<()>>> {
        let stream = <F::Service as ServiceMarker>::RequestStream::from_inner(server, false);
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

// TODO(awdavies): Provide a default implementation for a singleton stream
// handler.

#[cfg(test)]
mod tests {
    use super::*;
    use crate::FakeDaemonBuilder;
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
            .register_service_closure::<ffx_test::NoopMarker, _>(noop_service_closure())
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
            .register_service_closure::<ffx_test::NoopMarker, _>(noop_service_closure())
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
}
