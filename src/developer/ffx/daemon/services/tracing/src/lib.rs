// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Result},
    async_fs::File,
    async_lock::Mutex,
    async_trait::async_trait,
    fidl_fuchsia_developer_bridge as bridge, fidl_fuchsia_tracing_controller as trace,
    fuchsia_async::Task,
    futures::prelude::*,
    futures::task::{Context as FutContext, Poll},
    services::prelude::*,
    std::collections::hash_map::Entry,
    std::collections::HashMap,
    std::pin::Pin,
    std::rc::{Rc, Weak},
    std::time::Duration,
    thiserror::Error,
};

#[derive(Debug)]
struct TraceTask {
    target_query: Option<String>,
    output_file: String,
    // TODO(fxbug.dev/84729)
    #[allow(unused)]
    config: trace::TraceConfig,
    proxy: trace::ControllerProxy,
    task: Task<()>,
}

// This is just implemented for convenience so the wrapper is await-able.
impl Future for TraceTask {
    type Output = ();

    fn poll(mut self: Pin<&mut Self>, cx: &mut FutContext<'_>) -> Poll<Self::Output> {
        Pin::new(&mut self.task).poll(cx)
    }
}

#[derive(Debug, Error)]
enum TraceTaskStartError {
    #[error("fidl error: {0:?}")]
    FidlError(#[from] fidl::Error),
    #[error("tracing start error: {0:?}")]
    TracingStartError(trace::StartErrorCode),
    #[error("general start error: {0:?}")]
    GeneralError(#[from] anyhow::Error),
}

async fn trace_shutdown(proxy: &trace::ControllerProxy) -> Result<(), bridge::RecordingError> {
    proxy
        .stop_tracing(trace::StopOptions { write_results: Some(true), ..trace::StopOptions::EMPTY })
        .await
        .map_err(|e| {
            log::warn!("stopping tracing: {:?}", e);
            bridge::RecordingError::RecordingStop
        })?;
    proxy
        .terminate_tracing(trace::TerminateOptions {
            write_results: Some(true),
            ..trace::TerminateOptions::EMPTY
        })
        .await
        .map_err(|e| {
            log::warn!("terminating tracing: {:?}", e);
            bridge::RecordingError::RecordingStop
        })?;
    Ok(())
}

impl TraceTask {
    async fn new(
        map: Weak<Mutex<TraceMap>>,
        target_query: Option<String>,
        output_file: String,
        duration: Option<f64>,
        config: trace::TraceConfig,
        proxy: trace::ControllerProxy,
    ) -> Result<Self, TraceTaskStartError> {
        let (client, server) =
            fidl::Socket::create(fidl::SocketOpts::STREAM).context("creating socket")?;
        let client = fidl::AsyncSocket::from_socket(client).context("making async socket")?;
        let f = File::create(&output_file).await.context("opening file")?;
        proxy.initialize_tracing(config.clone(), server)?;
        proxy
            .start_tracing(trace::StartOptions::EMPTY)
            .await?
            .map_err(TraceTaskStartError::TracingStartError)?;
        let output_file_clone = output_file.clone();
        let target_query_clone = target_query.clone();
        let pipe_fut = async move {
            log::debug!("{:?} -> {} starting trace.", target_query_clone, output_file_clone);
            let mut out_file = f;
            let res = futures::io::copy(client, &mut out_file)
                .await
                .map_err(|e| log::warn!("file error: {:#?}", e));
            log::debug!(
                "{:?} -> {} trace complete, result: {:#?}",
                target_query_clone,
                output_file_clone,
                res
            );
            // async_fs files don't guarantee that the file is flushed on drop, so we need to
            // explicitly flush the file after writing.
            if let Err(err) = out_file.flush().await {
                log::warn!("file error: {:#?}", err);
            }
        };
        let shutdown_proxy = proxy.clone();
        Ok(Self {
            target_query,
            config,
            proxy,
            output_file: output_file.clone(),
            task: Task::local(async move {
                if let Some(duration) = duration {
                    let mut pipe_fut = Box::pin(pipe_fut).fuse();
                    let mut timeout_fut = Box::pin(async move {
                        fuchsia_async::Timer::new(Duration::from_secs_f64(duration)).await;
                        if let Err(e) = trace_shutdown(&shutdown_proxy).await {
                            log::warn!(
                                "shutting down trace from timer for {} secs: {:?}",
                                duration,
                                e
                            );
                            return;
                        }
                    })
                    .fuse();
                    futures::select! {
                        _ = pipe_fut => (),
                        _ = timeout_fut => pipe_fut.await,
                    };
                } else {
                    pipe_fut.await;
                }

                if let Some(map) = map.upgrade() {
                    let _ = map.lock().await.remove(&output_file);
                }
            }),
        })
    }

    async fn shutdown(self) -> Result<(), bridge::RecordingError> {
        trace_shutdown(&self.proxy).await?;
        log::trace!(
            "trace task {:?} -> {} shutdown await start",
            self.target_query,
            self.output_file
        );
        let query = self.target_query.clone();
        let output_file = self.output_file.clone();
        self.await;
        log::trace!("trace task {:?} -> {} shutdown await completed", query, output_file);
        Ok(())
    }
}

/// This maps a target id to a trace task.
type TraceMap = HashMap<String, TraceTask>;

#[ffx_service]
#[derive(Default)]
pub struct TracingService {
    tasks: Rc<Mutex<TraceMap>>,
}

async fn get_controller_proxy(
    target_query: Option<&String>,
    cx: &Context,
) -> Result<trace::ControllerProxy> {
    cx.open_target_proxy::<trace::ControllerMarker>(
        target_query.cloned(),
        "core/appmgr:out:fuchsia.tracing.controller.Controller",
    )
    .await
}

#[async_trait(?Send)]
impl FidlService for TracingService {
    type Service = bridge::TracingMarker;
    type StreamHandler = FidlStreamHandler<Self>;

    async fn handle(&self, cx: &Context, req: bridge::TracingRequest) -> Result<()> {
        match req {
            bridge::TracingRequest::StartRecording {
                target_query,
                output_file,
                options,
                target_config,
                responder,
            } => {
                let mut tasks = self.tasks.lock().await;
                let proxy = match get_controller_proxy(target_query.as_ref(), cx).await {
                    Ok(p) => p,
                    Err(e) => {
                        log::warn!("getting target controller proxy: {:?}", e);
                        return responder
                            .send(&mut Err(bridge::RecordingError::TargetProxyOpen))
                            .map_err(Into::into);
                    }
                };
                match tasks.entry(output_file.clone()) {
                    Entry::Occupied(_) => {
                        return responder
                            .send(&mut Err(bridge::RecordingError::RecordingAlreadyStarted))
                            .map_err(Into::into);
                    }
                    Entry::Vacant(e) => {
                        let task = match TraceTask::new(
                            Rc::downgrade(&self.tasks),
                            target_query,
                            output_file,
                            options.duration,
                            target_config,
                            proxy,
                        )
                        .await
                        {
                            Ok(t) => t,
                            Err(e) => {
                                log::warn!("unable to start trace: {:?}", e);
                                let mut res = match e {
                                    TraceTaskStartError::TracingStartError(t) => match t {
                                        trace::StartErrorCode::AlreadyStarted => {
                                            Err(bridge::RecordingError::RecordingAlreadyStarted)
                                        }
                                        _ => Err(bridge::RecordingError::RecordingStart),
                                    },
                                    _ => Err(bridge::RecordingError::RecordingStart),
                                };
                                return responder.send(&mut res).map_err(Into::into);
                            }
                        };
                        e.insert(task);
                    }
                }
                responder.send(&mut Ok(())).map_err(Into::into)
            }
            bridge::TracingRequest::StopRecording { output_file, responder } => {
                let task = {
                    let mut tasks = self.tasks.lock().await;
                    if let Some(task) = tasks.remove(&output_file) {
                        task
                    } else {
                        log::warn!("no task associated with trace file '{}'", output_file);
                        return responder.send(&mut Ok(())).map_err(Into::into);
                    }
                };
                responder.send(&mut task.shutdown().await).map_err(Into::into)
            }
        }
    }

    async fn stop(&mut self, _cx: &Context) -> Result<()> {
        let tasks =
            { self.tasks.lock().await.drain().map(|(_, v)| v.shutdown()).collect::<Vec<_>>() };
        futures::future::join_all(tasks).await;
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use async_lock::Mutex;
    use services::testing::FakeDaemonBuilder;
    use std::cell::RefCell;

    const FAKE_CONTROLLER_TRACE_OUTPUT: &'static str = "HOWDY HOWDY HOWDY";

    #[derive(Default)]
    struct FakeController {
        socket: Mutex<Option<fidl::Socket>>,
    }

    #[async_trait(?Send)]
    impl FidlService for FakeController {
        type Service = trace::ControllerMarker;
        type StreamHandler = FidlStreamHandler<Self>;

        async fn handle(&self, _cx: &Context, req: trace::ControllerRequest) -> Result<()> {
            match req {
                trace::ControllerRequest::InitializeTracing { output, .. } => {
                    self.socket.lock().await.replace(output);
                    Ok(())
                }
                trace::ControllerRequest::StartTracing { responder, .. } => {
                    assert!(self.socket.lock().await.is_some());
                    responder.send(&mut Ok(())).map_err(Into::into)
                }
                trace::ControllerRequest::StopTracing { responder, options } => {
                    assert_eq!(options.write_results.unwrap(), true);
                    responder.send().map_err(Into::into)
                }
                trace::ControllerRequest::TerminateTracing { responder, options } => {
                    assert_eq!(options.write_results.unwrap(), true);
                    let socket = self.socket.lock().await.take().unwrap();
                    assert_eq!(
                        FAKE_CONTROLLER_TRACE_OUTPUT.len(),
                        socket.write(FAKE_CONTROLLER_TRACE_OUTPUT.as_bytes()).unwrap()
                    );
                    responder.send(trace::TerminateResult::EMPTY).map_err(Into::into)
                }
                r => panic!("unexpected request: {:#?}", r),
            }
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_trace_start_stop_write_check() {
        let daemon = FakeDaemonBuilder::new()
            .register_fidl_service::<FakeController>()
            .register_fidl_service::<TracingService>()
            .build();
        let proxy = daemon.open_proxy::<bridge::TracingMarker>().await;
        let temp_dir = tempfile::TempDir::new().unwrap();
        let output = temp_dir.path().join("trace-test.fxt").into_os_string().into_string().unwrap();
        proxy
            .start_recording(None, &output, bridge::TraceOptions::EMPTY, trace::TraceConfig::EMPTY)
            .await
            .unwrap()
            .unwrap();
        proxy.stop_recording(&output).await.unwrap().unwrap();

        let mut f = File::open(std::path::PathBuf::from(output)).await.unwrap();
        let mut res = String::new();
        f.read_to_string(&mut res).await.unwrap();
        assert_eq!(res, FAKE_CONTROLLER_TRACE_OUTPUT.to_string());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_trace_error_double_start() {
        let daemon = FakeDaemonBuilder::new()
            .register_fidl_service::<FakeController>()
            .register_fidl_service::<TracingService>()
            .build();
        let proxy = daemon.open_proxy::<bridge::TracingMarker>().await;
        let temp_dir = tempfile::TempDir::new().unwrap();
        let output = temp_dir.path().join("trace-test.fxt").into_os_string().into_string().unwrap();
        proxy
            .start_recording(None, &output, bridge::TraceOptions::EMPTY, trace::TraceConfig::EMPTY)
            .await
            .unwrap()
            .unwrap();
        assert_eq!(
            Err(bridge::RecordingError::RecordingAlreadyStarted),
            proxy
                .start_recording(
                    None,
                    &output,
                    bridge::TraceOptions::EMPTY,
                    trace::TraceConfig::EMPTY
                )
                .await
                .unwrap()
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_trace_error_handling_already_started() {
        let daemon = FakeDaemonBuilder::new()
            .register_fidl_service::<TracingService>()
            .register_instanced_service_closure::<trace::ControllerMarker, _>(|_, req| match req {
                trace::ControllerRequest::InitializeTracing { .. } => Ok(()),
                trace::ControllerRequest::StartTracing { responder, .. } => responder
                    .send(&mut Err(trace::StartErrorCode::AlreadyStarted))
                    .map_err(Into::into),
                r => panic!("unexpecte request: {:#?}", r),
            })
            .build();
        let proxy = daemon.open_proxy::<bridge::TracingMarker>().await;
        let temp_dir = tempfile::TempDir::new().unwrap();
        let output = temp_dir.path().join("trace-test.fxt").into_os_string().into_string().unwrap();
        assert_eq!(
            Err(bridge::RecordingError::RecordingAlreadyStarted),
            proxy
                .start_recording(
                    None,
                    &output,
                    bridge::TraceOptions::EMPTY,
                    trace::TraceConfig::EMPTY
                )
                .await
                .unwrap()
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_trace_error_handling_generic_start_error() {
        let daemon = FakeDaemonBuilder::new()
            .register_fidl_service::<TracingService>()
            .register_instanced_service_closure::<trace::ControllerMarker, _>(|_, req| match req {
                trace::ControllerRequest::InitializeTracing { .. } => Ok(()),
                trace::ControllerRequest::StartTracing { responder, .. } => responder
                    .send(&mut Err(trace::StartErrorCode::NotInitialized))
                    .map_err(Into::into),
                r => panic!("unexpecte request: {:#?}", r),
            })
            .build();
        let proxy = daemon.open_proxy::<bridge::TracingMarker>().await;
        let temp_dir = tempfile::TempDir::new().unwrap();
        let output = temp_dir.path().join("trace-test.fxt").into_os_string().into_string().unwrap();
        assert_eq!(
            Err(bridge::RecordingError::RecordingStart),
            proxy
                .start_recording(
                    None,
                    &output,
                    bridge::TraceOptions::EMPTY,
                    trace::TraceConfig::EMPTY
                )
                .await
                .unwrap()
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_trace_shutdown_no_trace() {
        let daemon = FakeDaemonBuilder::new().register_fidl_service::<TracingService>().build();
        let proxy = daemon.open_proxy::<bridge::TracingMarker>().await;
        let temp_dir = tempfile::TempDir::new().unwrap();
        let output = temp_dir.path().join("trace-test.fxt").into_os_string().into_string().unwrap();
        proxy.stop_recording(&output).await.unwrap().unwrap();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_trace_duration_override() {
        let daemon = FakeDaemonBuilder::new().register_fidl_service::<FakeController>().build();
        let service = Rc::new(RefCell::new(TracingService::default()));
        let (proxy, _task) = services::testing::create_proxy(service.clone(), &daemon).await;
        let temp_dir = tempfile::TempDir::new().unwrap();
        let output = temp_dir.path().join("trace-test.fxt").into_os_string().into_string().unwrap();
        proxy
            .start_recording(
                None,
                &output,
                bridge::TraceOptions { duration: Some(500000.0), ..bridge::TraceOptions::EMPTY },
                trace::TraceConfig::EMPTY,
            )
            .await
            .unwrap()
            .unwrap();
        proxy.stop_recording(&output).await.unwrap().unwrap();

        let mut f = File::open(std::path::PathBuf::from(output)).await.unwrap();
        let mut res = String::new();
        f.read_to_string(&mut res).await.unwrap();
        assert_eq!(res, FAKE_CONTROLLER_TRACE_OUTPUT.to_string());
        let tasks = service.borrow().tasks.clone();
        assert!(tasks.lock().await.is_empty());
    }
}
