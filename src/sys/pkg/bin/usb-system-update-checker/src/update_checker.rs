// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Error},
    fidl_fuchsia_io::DirectoryMarker,
    fidl_fuchsia_pkg::{PackageResolverMarker, PackageResolverProxy, UpdatePolicy},
    fidl_fuchsia_update_usb::{
        CheckError, CheckSuccess, CheckerRequest, CheckerRequestStream, MonitorMarker,
    },
    fuchsia_component::client::connect_to_service,
    fuchsia_syslog::fx_log_warn,
    fuchsia_url::pkg_url::PkgUrl,
    fuchsia_zircon::Status,
    futures::prelude::*,
    std::{cmp::Ordering, str::FromStr},
    update_package::{SystemVersion, UpdatePackage},
};

const BUILD_INFO_VERSION_PATH: &str = "/config/build-info/version";

pub struct UsbUpdateChecker<'a> {
    build_info_path: &'a str,
}

impl UsbUpdateChecker<'_> {
    pub fn new() -> Self {
        UsbUpdateChecker { build_info_path: BUILD_INFO_VERSION_PATH }
    }

    pub async fn handle_request_stream(
        &self,
        stream: &mut CheckerRequestStream,
    ) -> Result<(), Error> {
        while let Some(request) = stream.try_next().await.context("Getting request from stream")? {
            match request {
                CheckerRequest::Check { update_url, logs_dir, monitor, responder } => {
                    let mut result = self.do_check(&update_url.url, logs_dir, monitor).await;
                    responder.send(&mut result).context("Sending check result")?;
                }
            }
        }

        Ok(())
    }

    async fn do_check(
        &self,
        unpinned_update_url: &str,
        _logs_dir: Option<fidl::endpoints::ClientEnd<DirectoryMarker>>,
        _monitor: Option<fidl::endpoints::ClientEnd<MonitorMarker>>,
    ) -> Result<CheckSuccess, CheckError> {
        let unpinned_update_url = PkgUrl::parse(unpinned_update_url)
            .map_err(|e| {
                fx_log_warn!(
                    "Failed to parse update_url '{}': {:#}",
                    unpinned_update_url,
                    anyhow!(e)
                );
                CheckError::InvalidUpdateUrl
            })
            .and_then(|url| self.validate_url(url))?;

        let (package, _pinned_update_url) =
            self.open_update_package(unpinned_update_url).await.map_err(|e| {
                fx_log_warn!("Failed to open update package: {:#}", anyhow!(e));
                CheckError::UpdateFailed
            })?;

        let package_version = package.version().await.map_err(|e| {
            fx_log_warn!("Failed to read update package version: {:#}", anyhow!(e));
            CheckError::UpdateFailed
        })?;

        if !self.is_update_required(package_version).await? {
            return Ok(CheckSuccess::UpdateNotNeeded);
        }

        todo!();
    }

    async fn is_update_required(&self, new_version: SystemVersion) -> Result<bool, CheckError> {
        let system_version = self.get_system_version().await.map_err(|e| {
            fx_log_warn!("Failed to get system version: {:#}", anyhow!(e));
            CheckError::UpdateFailed
        })?;

        match new_version.partial_cmp(&system_version) {
            None => {
                fx_log_warn!(
                    "Could not compare system version {} with update package version {}",
                    system_version,
                    new_version
                );
                Err(CheckError::UpdateFailed)
            }
            Some(Ordering::Greater) => Ok(true),
            Some(_) => Ok(false),
        }
    }

    fn validate_url(&self, url: PkgUrl) -> Result<PkgUrl, CheckError> {
        if url.package_hash().is_some() {
            return Err(CheckError::InvalidUpdateUrl);
        }

        Ok(url)
    }

    async fn get_system_version(&self) -> Result<SystemVersion, Error> {
        let string = io_util::file::read_in_namespace_to_string(self.build_info_path)
            .await
            .context("Reading build info")?;

        Ok(SystemVersion::from_str(&string).context("Parsing system version")?)
    }

    /// Resolve the update package at "url" using the system resolver.
    /// Returns a wrapper around the update package and the input 'url' which is now pinned to the
    /// resolved UpdatePackage.
    async fn open_update_package(&self, url: PkgUrl) -> Result<(UpdatePackage, PkgUrl), Error> {
        let resolver = connect_to_service::<PackageResolverMarker>()
            .context("Connecting to package resolver")?;
        self.open_update_package_at(url, resolver).await
    }

    /// Resolve the update package at "url" using the given resolver.
    /// Returns a wrapper around the update package and a pinned PkgUrl.
    async fn open_update_package_at(
        &self,
        url: PkgUrl,
        resolver: PackageResolverProxy,
    ) -> Result<(UpdatePackage, PkgUrl), Error> {
        let mut policy = UpdatePolicy { fetch_if_absent: true, allow_old_versions: false };

        let (dir, remote) = fidl::endpoints::create_proxy::<DirectoryMarker>()
            .context("Creating directory proxy")?;
        resolver
            .resolve(&url.to_string(), &mut vec![].into_iter(), &mut policy, remote)
            .await
            .context("Sending FIDL request")?
            .map_err(Status::from_raw)
            .context("Resolving pacakge")?;

        let meta_proxy = io_util::open_file(
            &dir,
            std::path::Path::new("meta"),
            fidl_fuchsia_io::OPEN_RIGHT_READABLE,
        )
        .context("Opening meta in update package")?;
        let hash =
            io_util::file::read_to_string(&meta_proxy).await.context("Reading package hash")?;

        let hash = fuchsia_url::Hash::from_str(&hash).context("Parsing hash")?;
        let url = PkgUrl::new_package(url.host().to_string(), url.path().to_string(), Some(hash))
            .context("Creating pinned package URL")?;

        Ok((UpdatePackage::new(dir), url))
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl_fuchsia_pkg::PackageUrl,
        fidl_fuchsia_update_usb::{CheckerMarker, CheckerProxy},
        fuchsia_async as fasync,
        std::io::Write,
        version::Version as SemanticVersion,
    };

    struct TestEnv<'a> {
        checker: UsbUpdateChecker<'a>,
        stream: CheckerRequestStream,
    }

    impl TestEnv<'_> {
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

    #[fasync::run_singlethreaded(test)]
    async fn test_version_comparison() {
        let (mut env, _) = TestEnv::new();
        let mut version_file =
            tempfile::NamedTempFile::new().expect("Creating temporary file to succeed");
        version_file.write(b"0.20200101.2.4").expect("Write succeeds");
        version_file.flush().expect("Flush succeeds");

        let path = version_file.path();
        env.checker.build_info_path = path.to_str().unwrap();

        let newer = SystemVersion::Semantic(SemanticVersion::from([0, 20200102, 1, 1]));

        assert!(env.checker.is_update_required(newer).await.unwrap());

        let older = SystemVersion::Semantic(SemanticVersion::from([0, 20190101, 1, 1]));
        assert_eq!(env.checker.is_update_required(older).await.unwrap(), false);

        let same = SystemVersion::Semantic(SemanticVersion::from([0, 20200101, 2, 4]));
        assert_eq!(env.checker.is_update_required(same).await.unwrap(), false);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_version_comparison_no_file() {
        let (env, _) = TestEnv::new();
        let version = SystemVersion::Semantic(SemanticVersion::from([0, 20200102, 1, 1]));
        assert!(env.checker.is_update_required(version).await.is_err());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_version_comparison_dev_build() {
        let (mut env, _) = TestEnv::new();

        let mut version_file =
            tempfile::NamedTempFile::new().expect("Creating temporary file to succeed");
        version_file.write(b"2020-08-13T10:27+00:00").expect("Write succeeds");
        version_file.flush().expect("Flush succeeds");

        let path = version_file.path();
        env.checker.build_info_path = path.to_str().unwrap();
        let version = SystemVersion::Semantic(SemanticVersion::from([0, 20200102, 1, 1]));
        assert_eq!(env.checker.is_update_required(version).await, Err(CheckError::UpdateFailed));

        let version = SystemVersion::Semantic(SemanticVersion::from([99999, 99999, 99999, 99999]));
        assert_eq!(env.checker.is_update_required(version).await, Err(CheckError::UpdateFailed));
    }
}
