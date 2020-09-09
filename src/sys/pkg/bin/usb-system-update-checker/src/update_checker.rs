// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Error},
    fidl_fuchsia_io::DirectoryMarker,
    fidl_fuchsia_update_usb::{
        CheckError, CheckSuccess, CheckerRequest, CheckerRequestStream, MonitorMarker,
    },
    fuchsia_syslog::fx_log_warn,
    fuchsia_url::pkg_url::PkgUrl,
    futures::prelude::*,
};

pub struct UsbUpdateChecker {}

impl UsbUpdateChecker {
    pub fn new() -> Self {
        UsbUpdateChecker {}
    }

    pub async fn handle_request_stream(
        &self,
        stream: &mut CheckerRequestStream,
    ) -> Result<(), Error> {
        while let Some(request) = stream.try_next().await.context("Getting request from stream")? {
            match request {
                CheckerRequest::Check { update_url, logs_dir, monitor, responder } => {
                    let mut result = self.do_check(&update_url.url, logs_dir, monitor);
                    responder.send(&mut result).context("Sending check result")?;
                }
            }
        }

        Ok(())
    }

    fn do_check(
        &self,
        update_url: &str,
        _logs_dir: Option<fidl::endpoints::ClientEnd<DirectoryMarker>>,
        _monitor: Option<fidl::endpoints::ClientEnd<MonitorMarker>>,
    ) -> Result<CheckSuccess, CheckError> {
        let _update_url = PkgUrl::parse(update_url)
            .map_err(|e| {
                fx_log_warn!("Failed to parse update_url '{}': {:#}", update_url, anyhow!(e));
                CheckError::InvalidUpdateUrl
            })
            .and_then(|url| self.validate_url(url))?;

        todo!();
    }

    fn validate_url(&self, url: PkgUrl) -> Result<PkgUrl, CheckError> {
        if url.package_hash().is_some() {
            return Err(CheckError::InvalidUpdateUrl);
        }

        Ok(url)
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl_fuchsia_pkg::PackageUrl,
        fidl_fuchsia_update_usb::{CheckerMarker, CheckerProxy},
        fuchsia_async as fasync,
    };

    struct TestEnv {
        checker: UsbUpdateChecker,
        stream: CheckerRequestStream,
    }

    impl TestEnv {
        fn new() -> (Self, CheckerProxy) {
            let checker = UsbUpdateChecker::new();
            let (proxy, stream) = fidl::endpoints::create_proxy_and_stream::<CheckerMarker>()
                .expect("create proxy and stream succeeds");
            (Self { checker, stream }, proxy)
        }

        async fn handle_requests<T, O>(&mut self, future: T) -> O
        where
            T: Future<Output = O>,
        {
            let handle_future = self.checker.handle_request_stream(&mut self.stream);

            futures::select! {
                result = future.fuse() => result,
                // This shouldn't happen - we expect the request stream to remain at least until
                // the other future is done.
                _ = handle_future.fuse() => panic!("Wrong future finished first!"),
            }
        }
    }

    // TODO(fxb/59376): move FIDL tests out of here.
    #[fasync::run_singlethreaded(test)]
    async fn test_invalid_url() {
        let (mut env, proxy) = TestEnv::new();

        let evil_url = "wow:// this isn't even a URL!!".to_owned();
        let future = proxy.check(&mut PackageUrl { url: evil_url }, None, None);
        let result = env.handle_requests(future).await.expect("FIDL request succeeds");
        assert_eq!(result, Err(CheckError::InvalidUpdateUrl));
    }

    // TODO(fxb/59376): move FIDL tests out of here.
    #[fasync::run_singlethreaded(test)]
    async fn test_pinned_url() {
        let (mut env, proxy) = TestEnv::new();

        // A valid hash is 64 bytes.
        let hash = "d00dfeed".repeat(8);
        let evil_url = format!("fuchsia-pkg://fuchsia.com/update?hash={}", hash);
        let future = proxy.check(&mut PackageUrl { url: evil_url }, None, None);
        let result = env.handle_requests(future).await.expect("FIDL request succeeds");

        assert_eq!(result, Err(CheckError::InvalidUpdateUrl));
    }
}
