// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(clippy::all)]

use {
    fidl_fuchsia_update_verify as fidl,
    fuchsia_async::Task,
    futures::{future, FutureExt as _, StreamExt as _},
    std::sync::Arc,
};

/// A call hook that can be used to inject responses into the Verifier service.
pub trait Hook: Send + Sync {
    /// Describes what the verify call will return.
    fn verify(
        &self,
        options: fidl::VerifyOptions,
    ) -> future::BoxFuture<'static, fidl::VerifierVerifyResult>;
}

impl<F> Hook for F
where
    F: Fn(fidl::VerifyOptions) -> fidl::VerifierVerifyResult + Send + Sync,
{
    fn verify(
        &self,
        options: fidl::VerifyOptions,
    ) -> future::BoxFuture<'static, fidl::VerifierVerifyResult> {
        future::ready(self(options)).boxed()
    }
}

pub struct MockVerifierService {
    call_hook: Box<dyn Hook>,
}

impl MockVerifierService {
    /// Creates a new MockVerifierService with a given callback to run per call to the service.
    pub fn new(hook: impl Hook + 'static) -> Self {
        Self { call_hook: Box::new(hook) }
    }

    /// Spawns an `fasync::Task` which serves fuchsia.update.verify/BlobfsVerifier.
    pub fn spawn_blobfs_verifier_service(self: Arc<Self>) -> (fidl::BlobfsVerifierProxy, Task<()>) {
        let (proxy, stream) =
            ::fidl::endpoints::create_proxy_and_stream::<fidl::BlobfsVerifierMarker>().unwrap();

        let task = Task::spawn(self.run_blobfs_verifier_service(stream));

        (proxy, task)
    }

    /// Serves fuchsia.update.verify/BlobfsVerifier.Verify requests on the given request stream.
    pub async fn run_blobfs_verifier_service(
        self: Arc<Self>,
        stream: fidl::BlobfsVerifierRequestStream,
    ) {
        let Self { call_hook } = &*self;
        stream
            .for_each(|request| match request.expect("received verifier request") {
                fidl::BlobfsVerifierRequest::Verify { options, responder } => call_hook
                    .verify(options)
                    .map(|mut res| responder.send(&mut res).expect("sent verifier response")),
            })
            .await
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl_fuchsia_update_verify::VerifyError,
        fuchsia_async as fasync,
        std::sync::atomic::{AtomicU32, Ordering},
    };

    #[fasync::run_singlethreaded(test)]
    async fn test_mock_verifier() {
        let mock = Arc::new(MockVerifierService::new(|_| Ok(())));
        let (proxy, _server) = mock.spawn_blobfs_verifier_service();

        let verify_result = proxy.verify(fidl::VerifyOptions::EMPTY).await.expect("made fidl call");

        assert_eq!(verify_result, Ok(()));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_mock_verifier_fails() {
        let mock = Arc::new(MockVerifierService::new(|_| Err(VerifyError::Internal)));
        let (proxy, _server) = mock.spawn_blobfs_verifier_service();

        let verify_result = proxy.verify(fidl::VerifyOptions::EMPTY).await.expect("made fidl call");

        assert_eq!(verify_result, Err(VerifyError::Internal));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_mock_verifier_with_external_state() {
        let called = Arc::new(AtomicU32::new(0));
        let called_clone = Arc::clone(&called);
        let mock = Arc::new(MockVerifierService::new(move |_| {
            called_clone.fetch_add(1, Ordering::SeqCst);
            Ok(())
        }));
        let (proxy, _server) = mock.spawn_blobfs_verifier_service();

        let verify_result = proxy.verify(fidl::VerifyOptions::EMPTY).await.expect("made fidl call");

        assert_eq!(verify_result, Ok(()));
        assert_eq!(called.load(Ordering::SeqCst), 1);
    }
}
