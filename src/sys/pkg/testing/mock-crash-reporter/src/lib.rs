// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(clippy::all)]

use {
    fidl_fuchsia_feedback::{CrashReporterMarker, CrashReporterProxy, CrashReporterRequestStream},
    fuchsia_async::Task,
    fuchsia_zircon::Status,
    futures::{
        channel::mpsc,
        future::{self, BoxFuture},
        lock::Mutex,
        prelude::*,
        TryStreamExt,
    },
    std::sync::Arc,
};

pub use fidl_fuchsia_feedback::CrashReport;

/// A call hook that can be used to inject responses into the CrashReporter service.
pub trait Hook: Send + Sync {
    /// Describes what the file call will return.
    fn file(&self, report: CrashReport) -> BoxFuture<'static, Result<(), Status>>;
}

impl<F> Hook for F
where
    F: Fn(CrashReport) -> Result<(), Status> + Send + Sync,
{
    fn file(&self, report: CrashReport) -> BoxFuture<'static, Result<(), Status>> {
        future::ready(self(report)).boxed()
    }
}

pub struct MockCrashReporterService {
    call_hook: Box<dyn Hook>,
}

impl MockCrashReporterService {
    /// Creates a new MockCrashReporterService with a given callback to run per call to the service.
    pub fn new(hook: impl Hook + 'static) -> Self {
        Self { call_hook: Box::new(hook) }
    }

    /// Spawns an `fasync::Task` which serves fuchsia.feedback/CrashReporter.
    pub fn spawn_crash_reporter_service(self: Arc<Self>) -> (CrashReporterProxy, Task<()>) {
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<CrashReporterMarker>().unwrap();

        let task = Task::spawn(self.run_crash_reporter_service(stream));

        (proxy, task)
    }

    /// Serves fuchsia.feedback/CrashReporter.File requests on the given request stream.
    pub async fn run_crash_reporter_service(
        self: Arc<Self>,
        mut stream: CrashReporterRequestStream,
    ) {
        while let Some(event) = stream.try_next().await.expect("received CrashReporter request") {
            match event {
                fidl_fuchsia_feedback::CrashReporterRequest::File { report, responder } => {
                    let mut res = self.call_hook.file(report).await.map_err(|s| s.into_raw());
                    responder.send(&mut res).unwrap();
                }
            }
        }
    }
}

/// Hook that can be used to yield control of the `file` call to the caller. The caller can
/// control when `file` calls complete by pulling from the mpsc::Receiver.
pub struct ThrottleHook {
    file_response: Result<(), Status>,
    sender: Arc<Mutex<mpsc::Sender<CrashReport>>>,
}

impl ThrottleHook {
    pub fn new(file_response: Result<(), Status>) -> (Self, mpsc::Receiver<CrashReport>) {
        // We deliberately give a capacity of 1 so that the caller must pull from the
        // receiver in order to unblock the `file` call.
        let (sender, recv) = mpsc::channel(0);
        (Self { file_response, sender: Arc::new(Mutex::new(sender)) }, recv)
    }
}
impl Hook for ThrottleHook {
    fn file(&self, report: CrashReport) -> BoxFuture<'static, Result<(), Status>> {
        let sender = Arc::clone(&self.sender);
        let file_response = self.file_response;

        async move {
            sender.lock().await.send(report).await.unwrap();
            file_response
        }
        .boxed()
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fuchsia_async as fasync,
        std::sync::atomic::{AtomicU32, Ordering},
    };

    #[fasync::run_singlethreaded(test)]
    async fn test_mock_crash_reporter() {
        let mock = Arc::new(MockCrashReporterService::new(|_| Ok(())));
        let (proxy, _server) = mock.spawn_crash_reporter_service();

        let file_result = proxy.file(CrashReport::EMPTY).await.expect("made fidl call");

        assert_eq!(file_result, Ok(()));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_mock_crash_reporter_fails() {
        let mock = Arc::new(MockCrashReporterService::new(|_| Err(Status::NOT_FOUND)));
        let (proxy, _server) = mock.spawn_crash_reporter_service();

        let file_result =
            proxy.file(CrashReport::EMPTY).await.expect("made fidl call").map_err(Status::from_raw);

        assert_eq!(file_result, Err(Status::NOT_FOUND));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_mock_crash_reporter_with_external_state() {
        let called = Arc::new(AtomicU32::new(0));
        let called_clone = Arc::clone(&called);
        let mock = Arc::new(MockCrashReporterService::new(move |_| {
            called_clone.fetch_add(1, Ordering::SeqCst);
            Ok(())
        }));
        let (proxy, _server) = mock.spawn_crash_reporter_service();

        let file_result = proxy.file(CrashReport::EMPTY).await.expect("made fidl call");

        assert_eq!(file_result, Ok(()));
        assert_eq!(called.load(Ordering::SeqCst), 1);
    }
}
