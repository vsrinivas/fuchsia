// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Result},
    async_fs::File,
    async_lock::{Mutex, MutexGuard},
    async_trait::async_trait,
    fidl_fuchsia_developer_ffx as ffx, fidl_fuchsia_tracing_controller as trace,
    fuchsia_async::Task,
    futures::prelude::*,
    futures::task::{Context as FutContext, Poll},
    protocols::prelude::*,
    std::collections::hash_map::Entry,
    std::collections::BTreeSet,
    std::collections::HashMap,
    std::pin::Pin,
    std::rc::{Rc, Weak},
    std::time::{Duration, Instant},
    tasks::TaskManager,
    thiserror::Error,
};

#[derive(Debug)]
struct TraceTask {
    target_info: ffx::TargetInfo,
    output_file: String,
    config: trace::TraceConfig,
    proxy: trace::ControllerProxy,
    options: ffx::TraceOptions,
    start_time: Instant,
    task: Task<()>,
    trace_shutdown_complete: Rc<Mutex<bool>>,
}

// This is just implemented for convenience so the wrapper is await-able.
impl Future for TraceTask {
    type Output = ();

    fn poll(mut self: Pin<&mut Self>, cx: &mut FutContext<'_>) -> Poll<Self::Output> {
        Pin::new(&mut self.task).poll(cx)
    }
}

// Just a wrapper type for ffx::Trigger that does some unwrapping on allocation.
#[derive(Debug, PartialEq, Eq)]
struct TriggerSetItem {
    alert: String,
    action: ffx::Action,
}

impl TriggerSetItem {
    fn new(t: ffx::Trigger) -> Option<Self> {
        let alert = t.alert?;
        let action = t.action?;
        Some(Self { alert, action })
    }

    /// Convenience constructor for doing a lookup.
    fn lookup(alert: String) -> Self {
        Self { alert: alert.to_owned(), action: ffx::Action::Terminate }
    }
}

impl std::cmp::PartialOrd for TriggerSetItem {
    fn partial_cmp(&self, other: &Self) -> Option<std::cmp::Ordering> {
        self.alert.partial_cmp(&other.alert)
    }
}

impl std::cmp::Ord for TriggerSetItem {
    fn cmp(&self, other: &Self) -> std::cmp::Ordering {
        self.alert.cmp(&other.alert)
    }
}

type TriggersFut<'a> = Pin<Box<dyn Future<Output = Option<ffx::Action>> + 'a>>;

struct TriggersWatcher<'a> {
    inner: TriggersFut<'a>,
}

impl<'a> TriggersWatcher<'a> {
    fn new(controller: &'a trace::ControllerProxy, triggers: Option<Vec<ffx::Trigger>>) -> Self {
        Self {
            inner: Box::pin(async move {
                let set: BTreeSet<TriggerSetItem> = triggers
                    .map(|t| t.into_iter().filter_map(|i| TriggerSetItem::new(i)).collect())
                    .unwrap_or(BTreeSet::new());
                if set.is_empty() {
                    return std::future::pending().await;
                }
                while let Ok(alert) = controller.watch_alert().await {
                    tracing::trace!("alert received: {}", alert);
                    let lookup_item = TriggerSetItem::lookup(alert);
                    if set.contains(&lookup_item) {
                        return set.get(&lookup_item).map(|s| s.action.clone());
                    }
                }
                None
            }),
        }
    }
}

impl Future for TriggersWatcher<'_> {
    type Output = Option<ffx::Action>;

    fn poll(mut self: Pin<&mut Self>, cx: &mut FutContext<'_>) -> Poll<Self::Output> {
        Pin::new(&mut self.inner).poll(cx)
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

async fn trace_shutdown(proxy: &trace::ControllerProxy) -> Result<(), ffx::RecordingError> {
    proxy
        .stop_tracing(trace::StopOptions { write_results: Some(true), ..trace::StopOptions::EMPTY })
        .await
        .map_err(|e| {
            tracing::warn!("stopping tracing: {:?}", e);
            ffx::RecordingError::RecordingStop
        })?;
    proxy
        .terminate_tracing(trace::TerminateOptions {
            write_results: Some(true),
            ..trace::TerminateOptions::EMPTY
        })
        .await
        .map_err(|e| {
            tracing::warn!("terminating tracing: {:?}", e);
            ffx::RecordingError::RecordingStop
        })?;
    Ok(())
}

impl TraceTask {
    async fn new(
        map: Weak<Mutex<TraceMap>>,
        target_info: ffx::TargetInfo,
        output_file: String,
        options: ffx::TraceOptions,
        config: trace::TraceConfig,
        proxy: trace::ControllerProxy,
    ) -> Result<Self, TraceTaskStartError> {
        let duration = options.duration;
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
        let target_info_clone = target_info.clone();
        let pipe_fut = async move {
            tracing::debug!("{:?} -> {} starting trace.", target_info_clone, output_file_clone);
            let mut out_file = f;
            let res = futures::io::copy(client, &mut out_file)
                .await
                .map_err(|e| tracing::warn!("file error: {:#?}", e));
            tracing::debug!(
                "{:?} -> {} trace complete, result: {:#?}",
                target_info_clone,
                output_file_clone,
                res
            );
            // async_fs files don't guarantee that the file is flushed on drop, so we need to
            // explicitly flush the file after writing.
            if let Err(err) = out_file.flush().await {
                tracing::warn!("file error: {:#?}", err);
            }
        };
        let controller = proxy.clone();
        let triggers = options.triggers.clone();
        let trace_shutdown_complete = Rc::new(Mutex::new(false));
        let trace_shutdown_complete_clone = trace_shutdown_complete.clone();
        Ok(Self {
            target_info: target_info.clone(),
            config,
            proxy,
            options,
            start_time: Instant::now(),
            output_file: output_file.clone(),
            trace_shutdown_complete,
            task: Task::local(async move {
                let mut timeout_fut = Box::pin(async move {
                    if let Some(duration) = duration {
                        fuchsia_async::Timer::new(Duration::from_secs_f64(duration)).await;
                    } else {
                        std::future::pending::<()>().await;
                    }
                })
                .fuse();
                let mut pipe_fut = Box::pin(pipe_fut).fuse();
                let shutdown_proxy = controller.clone();
                let shutdown_fut = async move {
                    let mut done = trace_shutdown_complete_clone.lock().await;
                    if !*done {
                        if let Err(e) = trace_shutdown(&shutdown_proxy).await {
                            tracing::warn!("error shutting down trace: {:?}", e);
                        }
                        *done = true
                    }
                };
                let mut trigger_fut = TriggersWatcher::new(&controller, triggers).fuse();
                futures::select! {
                    _ = pipe_fut => {},
                    _ = timeout_fut => {
                        shutdown_fut.await;
                        pipe_fut.await;
                    }
                    action = trigger_fut => {
                        if let Some(action) = action {
                            match action {
                                ffx::Action::Terminate => {
                                    shutdown_fut.await;
                                    pipe_fut.await;
                                }
                            }
                        } else {
                            // If we're here, it means the channel was dropped unexpectedly.
                            // the warnings will be logged in these futures.
                            shutdown_fut.await;
                            pipe_fut.await;
                        }
                    }
                };

                if let Some(map) = map.upgrade() {
                    let mut map = map.lock().await;
                    let _ = map.output_file_to_nodename.remove(&output_file);
                    let _ =
                        map.nodename_to_task.remove(&target_info.nodename.unwrap_or_else(|| {
                            tracing::warn!(
                                "trace writing to '{}' has no target nodename",
                                output_file
                            );
                            String::new()
                        }));
                }
            }),
        })
    }

    async fn shutdown(self) -> Result<(), ffx::RecordingError> {
        {
            let mut trace_shutdown_done = self.trace_shutdown_complete.lock().await;
            if !*trace_shutdown_done {
                if let Err(e) = trace_shutdown(&self.proxy).await {
                    tracing::warn!("error shutting down trace: {:?}", e);
                }
                *trace_shutdown_done = true;
            }
        }
        tracing::trace!(
            "trace task {:?} -> {} shutdown await start",
            self.target_info,
            self.output_file
        );
        let target_info_clone = self.target_info.clone();
        let output_file = self.output_file.clone();
        self.await;
        tracing::trace!(
            "trace task {:?} -> {} shutdown await completed",
            target_info_clone,
            output_file
        );
        Ok(())
    }
}

#[derive(Default)]
struct TraceMap {
    nodename_to_task: HashMap<String, TraceTask>,
    output_file_to_nodename: HashMap<String, String>,
}

#[ffx_protocol]
#[derive(Default)]
pub struct TracingProtocol {
    tasks: Rc<Mutex<TraceMap>>,
    iter_tasks: TaskManager,
}

async fn get_controller_proxy(
    target_query: Option<&String>,
    cx: &Context,
) -> Result<(ffx::TargetInfo, trace::ControllerProxy)> {
    let (target, proxy) = cx
        .open_target_proxy_with_info::<trace::ControllerMarker>(
            target_query.cloned(),
            "core/trace_manager:expose:fuchsia.tracing.controller.Controller",
        )
        .await?;
    Ok((target, proxy))
}

impl TracingProtocol {
    async fn remove_output_file_or_find_target_nodename(
        &self,
        cx: &Context,
        tasks: &mut MutexGuard<'_, TraceMap>,
        output_file: &String,
    ) -> Result<String, ffx::RecordingError> {
        match tasks.output_file_to_nodename.remove(output_file) {
            Some(n) => Ok(n),
            None => cx
                .get_target_collection()
                .await
                .map_err(|e| {
                    tracing::warn!("unable to get target collection: {:?}", e);
                    ffx::RecordingError::RecordingStop
                })?
                .get(output_file.as_str())
                .ok_or_else(|| {
                    tracing::warn!("target query '{}' matches no targets", output_file);
                    ffx::RecordingError::NoSuchTarget
                })?
                .nodename()
                .ok_or_else(|| {
                    tracing::warn!(
                        "target query '{}' matches target with no nodename",
                        output_file
                    );
                    ffx::RecordingError::DisconnectedTarget
                }),
        }
    }
}

#[async_trait(?Send)]
impl FidlProtocol for TracingProtocol {
    type Protocol = ffx::TracingMarker;
    type StreamHandler = FidlStreamHandler<Self>;

    async fn handle(&self, cx: &Context, req: ffx::TracingRequest) -> Result<()> {
        match req {
            ffx::TracingRequest::StartRecording {
                target_query,
                output_file,
                options,
                target_config,
                responder,
            } => {
                let mut tasks = self.tasks.lock().await;
                let target_query = target_query.string_matcher;
                let (target_info, proxy) =
                    match get_controller_proxy(target_query.as_ref(), cx).await {
                        Ok(p) => p,
                        Err(e) => {
                            tracing::warn!("getting target controller proxy: {:?}", e);
                            return responder
                                .send(&mut Err(ffx::RecordingError::TargetProxyOpen))
                                .map_err(Into::into);
                        }
                    };
                // This should functionally never happen (a target whose nodename isn't
                // known after having been identified for service discovery would be a
                // critical error).
                let nodename = match target_info.nodename {
                    Some(ref n) => n.clone(),
                    None => {
                        tracing::warn!(
                            "query does not match a valid target with nodename: {:?}",
                            target_query
                        );
                        return responder
                            .send(&mut Err(ffx::RecordingError::TargetProxyOpen))
                            .map_err(Into::into);
                    }
                };
                match tasks.output_file_to_nodename.entry(output_file.clone()) {
                    Entry::Occupied(_) => {
                        return responder
                            .send(&mut Err(ffx::RecordingError::DuplicateTraceFile))
                            .map_err(Into::into);
                    }
                    Entry::Vacant(e) => {
                        let task = match TraceTask::new(
                            Rc::downgrade(&self.tasks),
                            target_info.clone(),
                            output_file.clone(),
                            options,
                            target_config,
                            proxy,
                        )
                        .await
                        {
                            Ok(t) => t,
                            Err(e) => {
                                tracing::warn!("unable to start trace: {:?}", e);
                                let mut res = match e {
                                    TraceTaskStartError::TracingStartError(t) => match t {
                                        trace::StartErrorCode::AlreadyStarted => {
                                            Err(ffx::RecordingError::RecordingAlreadyStarted)
                                        }
                                        e => {
                                            tracing::warn!("Start error: {:?}", e);
                                            Err(ffx::RecordingError::RecordingStart)
                                        }
                                    },
                                    e => {
                                        tracing::warn!("Start error: {:?}", e);
                                        Err(ffx::RecordingError::RecordingStart)
                                    }
                                };
                                return responder.send(&mut res).map_err(Into::into);
                            }
                        };
                        e.insert(nodename.clone());
                        tasks.nodename_to_task.insert(nodename, task);
                    }
                }
                responder.send(&mut Ok(target_info)).map_err(Into::into)
            }
            ffx::TracingRequest::StopRecording { name, responder } => {
                let task = {
                    let mut tasks = self.tasks.lock().await;
                    let nodename = match self
                        .remove_output_file_or_find_target_nodename(cx, &mut tasks, &name)
                        .await
                    {
                        Ok(n) => n,
                        Err(e) => return responder.send(&mut Err(e)).map_err(Into::into),
                    };
                    if let Some(task) = tasks.nodename_to_task.remove(&nodename) {
                        task
                    } else {
                        // TODO(fxbug.dev/86410)
                        tracing::warn!("no task associated with trace file '{}'", name);
                        return responder
                            .send(&mut Err(ffx::RecordingError::NoSuchTraceFile))
                            .map_err(Into::into);
                    }
                };
                let target_info = task.target_info.clone();
                let mut res = task.shutdown().await.map(|_| target_info);
                responder.send(&mut res).map_err(Into::into)
            }
            ffx::TracingRequest::Status { iterator, responder } => {
                let mut stream = iterator.into_stream()?;
                let res = self
                    .tasks
                    .lock()
                    .await
                    .nodename_to_task
                    .values()
                    .map(|t| ffx::TraceInfo {
                        target: Some(t.target_info.clone()),
                        output_file: Some(t.output_file.clone()),
                        duration: t.options.duration.clone(),
                        remaining_runtime: t.options.duration.clone().map(|d| {
                            Duration::from_secs_f64(d)
                                .checked_sub(t.start_time.elapsed())
                                .unwrap_or(Duration::from_secs(0))
                                .as_secs_f64()
                        }),
                        config: Some(t.config.clone()),
                        triggers: t.options.triggers.clone(),
                        ..ffx::TraceInfo::EMPTY
                    })
                    .collect::<Vec<_>>();
                self.iter_tasks.spawn(async move {
                    const CHUNK_SIZE: usize = 20;
                    let mut iter = res.into_iter();
                    while let Ok(Some(ffx::TracingStatusIteratorRequest::GetNext { responder })) =
                        stream.try_next().await
                    {
                        let _ = responder
                            .send(
                                &mut iter.by_ref().take(CHUNK_SIZE).collect::<Vec<_>>().into_iter(),
                            )
                            .map_err(|e| {
                                tracing::warn!("responding to tracing status iterator: {:?}", e);
                            });
                    }
                });
                responder.send().map_err(Into::into)
            }
        }
    }

    async fn stop(&mut self, _cx: &Context) -> Result<()> {
        let tasks = {
            let mut tasks = self.tasks.lock().await;
            tasks.output_file_to_nodename.clear();
            tasks.nodename_to_task.drain().map(|(_, v)| v.shutdown()).collect::<Vec<_>>()
        };
        futures::future::join_all(tasks).await;
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use async_lock::Mutex;
    use protocols::testing::FakeDaemonBuilder;
    use std::cell::RefCell;

    const FAKE_CONTROLLER_TRACE_OUTPUT: &'static str = "HOWDY HOWDY HOWDY";

    #[derive(Default)]
    struct FakeController {
        socket: Mutex<Option<fidl::Socket>>,
    }

    #[async_trait(?Send)]
    impl FidlProtocol for FakeController {
        type Protocol = trace::ControllerMarker;
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
            .register_fidl_protocol::<FakeController>()
            .register_fidl_protocol::<TracingProtocol>()
            .target(ffx::TargetInfo {
                nodename: Some("foobar".to_string()),
                ..ffx::TargetInfo::EMPTY
            })
            .build();
        let proxy = daemon.open_proxy::<ffx::TracingMarker>().await;
        let temp_dir = tempfile::TempDir::new().unwrap();
        let output = temp_dir.path().join("trace-test.fxt").into_os_string().into_string().unwrap();
        proxy
            .start_recording(
                ffx::TargetQuery {
                    string_matcher: Some("foobar".to_owned()),
                    ..ffx::TargetQuery::EMPTY
                },
                &output,
                ffx::TraceOptions::EMPTY,
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
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_trace_error_double_start() {
        let daemon = FakeDaemonBuilder::new()
            .register_fidl_protocol::<FakeController>()
            .register_fidl_protocol::<TracingProtocol>()
            .target(ffx::TargetInfo {
                nodename: Some("foobar".to_string()),
                ..ffx::TargetInfo::EMPTY
            })
            .build();
        let proxy = daemon.open_proxy::<ffx::TracingMarker>().await;
        let temp_dir = tempfile::TempDir::new().unwrap();
        let output = temp_dir.path().join("trace-test.fxt").into_os_string().into_string().unwrap();
        proxy
            .start_recording(
                ffx::TargetQuery {
                    string_matcher: Some("foobar".to_owned()),
                    ..ffx::TargetQuery::EMPTY
                },
                &output,
                ffx::TraceOptions::EMPTY,
                trace::TraceConfig::EMPTY,
            )
            .await
            .unwrap()
            .unwrap();
        // The target query needs to be empty here in order to fall back to checking
        // the trace file.
        assert_eq!(
            Err(ffx::RecordingError::DuplicateTraceFile),
            proxy
                .start_recording(
                    ffx::TargetQuery::EMPTY,
                    &output,
                    ffx::TraceOptions::EMPTY,
                    trace::TraceConfig::EMPTY
                )
                .await
                .unwrap()
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_trace_error_handling_already_started() {
        let daemon = FakeDaemonBuilder::new()
            .register_fidl_protocol::<TracingProtocol>()
            .register_instanced_protocol_closure::<trace::ControllerMarker, _>(|_, req| match req {
                trace::ControllerRequest::InitializeTracing { .. } => Ok(()),
                trace::ControllerRequest::StartTracing { responder, .. } => responder
                    .send(&mut Err(trace::StartErrorCode::AlreadyStarted))
                    .map_err(Into::into),
                r => panic!("unexpecte request: {:#?}", r),
            })
            .target(ffx::TargetInfo {
                nodename: Some("foobar".to_string()),
                ..ffx::TargetInfo::EMPTY
            })
            .build();
        let proxy = daemon.open_proxy::<ffx::TracingMarker>().await;
        let temp_dir = tempfile::TempDir::new().unwrap();
        let output = temp_dir.path().join("trace-test.fxt").into_os_string().into_string().unwrap();
        assert_eq!(
            Err(ffx::RecordingError::RecordingAlreadyStarted),
            proxy
                .start_recording(
                    ffx::TargetQuery {
                        string_matcher: Some("foobar".to_owned()),
                        ..ffx::TargetQuery::EMPTY
                    },
                    &output,
                    ffx::TraceOptions::EMPTY,
                    trace::TraceConfig::EMPTY
                )
                .await
                .unwrap()
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_trace_error_handling_generic_start_error() {
        let daemon = FakeDaemonBuilder::new()
            .register_fidl_protocol::<TracingProtocol>()
            .register_instanced_protocol_closure::<trace::ControllerMarker, _>(|_, req| match req {
                trace::ControllerRequest::InitializeTracing { .. } => Ok(()),
                trace::ControllerRequest::StartTracing { responder, .. } => responder
                    .send(&mut Err(trace::StartErrorCode::NotInitialized))
                    .map_err(Into::into),
                r => panic!("unexpecte request: {:#?}", r),
            })
            .target(ffx::TargetInfo {
                nodename: Some("foobar".to_string()),
                ..ffx::TargetInfo::EMPTY
            })
            .build();
        let proxy = daemon.open_proxy::<ffx::TracingMarker>().await;
        let temp_dir = tempfile::TempDir::new().unwrap();
        let output = temp_dir.path().join("trace-test.fxt").into_os_string().into_string().unwrap();
        assert_eq!(
            Err(ffx::RecordingError::RecordingStart),
            proxy
                .start_recording(
                    ffx::TargetQuery {
                        string_matcher: Some("foobar".to_owned()),
                        ..ffx::TargetQuery::EMPTY
                    },
                    &output,
                    ffx::TraceOptions::EMPTY,
                    trace::TraceConfig::EMPTY
                )
                .await
                .unwrap()
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_trace_shutdown_no_trace() {
        let daemon = FakeDaemonBuilder::new().register_fidl_protocol::<TracingProtocol>().build();
        let proxy = daemon.open_proxy::<ffx::TracingMarker>().await;
        let temp_dir = tempfile::TempDir::new().unwrap();
        let output = temp_dir.path().join("trace-test.fxt").into_os_string().into_string().unwrap();
        assert_eq!(
            ffx::RecordingError::NoSuchTarget,
            proxy.stop_recording(&output).await.unwrap().unwrap_err()
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_trace_duration_shutdown_via_output_file() {
        let daemon = FakeDaemonBuilder::new()
            .register_fidl_protocol::<FakeController>()
            .target(ffx::TargetInfo {
                nodename: Some("foobar".to_owned()),
                ..ffx::TargetInfo::EMPTY
            })
            .build();
        let protocol = Rc::new(RefCell::new(TracingProtocol::default()));
        let (proxy, _task) = protocols::testing::create_proxy(protocol.clone(), &daemon).await;
        let temp_dir = tempfile::TempDir::new().unwrap();
        let output = temp_dir.path().join("trace-test.fxt").into_os_string().into_string().unwrap();
        proxy
            .start_recording(
                ffx::TargetQuery {
                    string_matcher: Some("foobar".to_owned()),
                    ..ffx::TargetQuery::EMPTY
                },
                &output,
                ffx::TraceOptions { duration: Some(500000.0), ..ffx::TraceOptions::EMPTY },
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
        let tasks = protocol.borrow().tasks.clone();
        assert!(tasks.lock().await.nodename_to_task.is_empty());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_trace_duration_shutdown_via_nodename() {
        let daemon = FakeDaemonBuilder::new()
            .register_fidl_protocol::<FakeController>()
            .target(ffx::TargetInfo {
                nodename: Some("foobar".to_string()),
                ..ffx::TargetInfo::EMPTY
            })
            .build();
        let protocol = Rc::new(RefCell::new(TracingProtocol::default()));
        let (proxy, _task) = protocols::testing::create_proxy(protocol.clone(), &daemon).await;
        let temp_dir = tempfile::TempDir::new().unwrap();
        let output = temp_dir.path().join("trace-test.fxt").into_os_string().into_string().unwrap();
        proxy
            .start_recording(
                ffx::TargetQuery {
                    string_matcher: Some("foobar".to_owned()),
                    ..ffx::TargetQuery::EMPTY
                },
                &output,
                ffx::TraceOptions { duration: Some(500000.0), ..ffx::TraceOptions::EMPTY },
                trace::TraceConfig::EMPTY,
            )
            .await
            .unwrap()
            .unwrap();
        proxy.stop_recording("foobar").await.unwrap().unwrap();

        let mut f = File::open(std::path::PathBuf::from(output)).await.unwrap();
        let mut res = String::new();
        f.read_to_string(&mut res).await.unwrap();
        assert_eq!(res, FAKE_CONTROLLER_TRACE_OUTPUT.to_string());
        let tasks = protocol.borrow().tasks.clone();
        assert!(tasks.lock().await.nodename_to_task.is_empty());
    }

    fn spawn_fake_alert_watcher(alert: &'static str) -> trace::ControllerProxy {
        let (proxy, server) = fidl::endpoints::create_proxy::<trace::ControllerMarker>().unwrap();
        let mut stream = server.into_stream().unwrap();
        fuchsia_async::Task::local(async move {
            while let Ok(Some(req)) = stream.try_next().await {
                match req {
                    trace::ControllerRequest::WatchAlert { responder } => {
                        responder.send(alert).unwrap();
                    }
                    r => panic!("unexpected request in this test: {:?}", r),
                }
            }
        })
        .detach();
        proxy
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_triggers_valid() {
        let proxy = spawn_fake_alert_watcher("foober");
        let triggers = Some(vec![
            ffx::Trigger { alert: Some("foo".to_owned()), action: None, ..ffx::Trigger::EMPTY },
            ffx::Trigger {
                alert: Some("foober".to_owned()),
                action: Some(ffx::Action::Terminate),
                ..ffx::Trigger::EMPTY
            },
        ]);
        let res = TriggersWatcher::new(&proxy, triggers).await;
        assert_eq!(res, Some(ffx::Action::Terminate));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_triggers_server_dropped() {
        let (proxy, server) = fidl::endpoints::create_proxy::<trace::ControllerMarker>().unwrap();
        drop(server);
        let triggers = Some(vec![
            ffx::Trigger { alert: Some("foo".to_owned()), action: None, ..ffx::Trigger::EMPTY },
            ffx::Trigger {
                alert: Some("foober".to_owned()),
                action: Some(ffx::Action::Terminate),
                ..ffx::Trigger::EMPTY
            },
        ]);
        let res = TriggersWatcher::new(&proxy, triggers).await;
        assert_eq!(res, None);
    }
}
