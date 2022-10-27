// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    // TODO(fxbug.dev/51770): FuchsiaInstallPlan should be shared with omaha-client.
    super::install_plan::FuchsiaInstallPlan,
    crate::{cache::Cache, resolver::Resolver, updater::Updater},
    anyhow::anyhow,
    fidl::endpoints::ClientEnd,
    fidl_fuchsia_io as fio,
    fuchsia_url::PinnedAbsolutePackageUrl,
    futures::future::LocalBoxFuture,
    futures::prelude::*,
    omaha_client::{
        cup_ecdsa::RequestMetadata,
        installer::{AppInstallResult, Installer, ProgressObserver},
        protocol::response::{OmahaStatus, Response},
        request_builder::RequestParams,
    },
    std::sync::Arc,
    thiserror::Error,
    tracing::warn,
};

/// An Omaha Installer implementation that uses the `isolated-swd` Updater to perform the OTA
/// installation.
/// This Installer implementation does not reboot when `perform_reboot` is called, as the caller of
/// `isolated-swd` is expected to do that.
pub struct IsolatedInstaller {
    blobfs: Option<ClientEnd<fio::DirectoryMarker>>,
    paver_connector: Option<ClientEnd<fio::DirectoryMarker>>,
    cache: Arc<Cache>,
    resolver: Arc<Resolver>,
    board_name: String,
    updater_url: String,
}

impl IsolatedInstaller {
    pub fn new(
        blobfs: ClientEnd<fio::DirectoryMarker>,
        paver_connector: ClientEnd<fio::DirectoryMarker>,
        cache: Arc<Cache>,
        resolver: Arc<Resolver>,
        board_name: String,
        updater_url: String,
    ) -> Self {
        IsolatedInstaller {
            blobfs: Some(blobfs),
            paver_connector: Some(paver_connector),
            cache,
            resolver,
            board_name,
            updater_url,
        }
    }
}

#[derive(Debug, Error)]
pub enum IsolatedInstallError {
    #[error("running updater failed")]
    Failure(#[source] anyhow::Error),

    #[error("can only run the installer once")]
    AlreadyRun,

    #[error("create install plan")]
    InstallPlan(#[source] anyhow::Error),
}

impl Installer for IsolatedInstaller {
    type InstallPlan = FuchsiaInstallPlan;
    type Error = IsolatedInstallError;
    type InstallResult = ();

    fn perform_install<'a>(
        &'a mut self,
        install_plan: &FuchsiaInstallPlan,
        observer: Option<&'a dyn ProgressObserver>,
    ) -> LocalBoxFuture<'_, (Self::InstallResult, Vec<AppInstallResult<Self::Error>>)> {
        if let Some(o) = observer.as_ref() {
            o.receive_progress(None, 0., None, None);
        }

        if self.blobfs.is_none() {
            return async {
                ((), vec![AppInstallResult::Failed(IsolatedInstallError::AlreadyRun)])
            }
            .boxed();
        }

        let updater = Updater::launch_with_components(
            self.blobfs.take().unwrap(),
            self.paver_connector.take().unwrap(),
            Arc::clone(&self.resolver),
            Arc::clone(&self.cache),
            &self.board_name,
            &self.updater_url,
        );
        let url = install_plan.url.clone();

        async move {
            let mut updater = updater.await.map_err(IsolatedInstallError::Failure)?;
            let () = updater
                .install_update(Some(&url.clone().into()))
                .await
                .map_err(IsolatedInstallError::Failure)?;
            if let Some(o) = observer.as_ref() {
                o.receive_progress(None, 1., None, None);
            }
            Ok(())
        }
        .map(|result| ((), vec![result.into()]))
        .boxed_local()
    }

    fn perform_reboot(&mut self) -> LocalBoxFuture<'_, Result<(), anyhow::Error>> {
        // We don't actually reboot here. The caller of isolated-swd is responsible for performing
        // a reboot after the update is installed.
        // Tell Omaha that the reboot was successful so that it finishes the update check
        // and omaha::install_update() can return.
        async move { Ok(()) }.boxed_local()
    }

    fn try_create_install_plan<'a>(
        &'a self,
        request_params: &'a RequestParams,
        _request_metadata: Option<&'a RequestMetadata>,
        response: &'a Response,
        _response_bytes: Vec<u8>,
        _ecdsa_signature: Option<Vec<u8>>,
    ) -> LocalBoxFuture<'a, Result<Self::InstallPlan, Self::Error>> {
        async move { try_create_install_plan(request_params, response) }.boxed_local()
    }
}

fn try_create_install_plan(
    request_params: &RequestParams,
    response: &Response,
) -> Result<FuchsiaInstallPlan, IsolatedInstallError> {
    let (app, rest) = if let Some((app, rest)) = response.apps.split_first() {
        (app, rest)
    } else {
        return Err(IsolatedInstallError::InstallPlan(anyhow!("No app in Omaha response")));
    };

    if !rest.is_empty() {
        warn!(found = response.apps.len(), "Only 1 app is supported");
    }

    if app.status != OmahaStatus::Ok {
        return Err(IsolatedInstallError::InstallPlan(anyhow!(
            "Found non-ok app status: {:?}",
            app.status
        )));
    }

    let update_check = if let Some(update_check) = &app.update_check {
        update_check
    } else {
        return Err(IsolatedInstallError::InstallPlan(anyhow!(
            "No update_check in Omaha response"
        )));
    };

    let mut urls = match update_check.status {
        OmahaStatus::Ok => update_check.get_all_url_codebases(),
        OmahaStatus::NoUpdate => {
            return Err(IsolatedInstallError::InstallPlan(anyhow!(
                "Was asked to create an install plan for a NoUpdate Omaha response"
            )));
        }
        _ => {
            if let Some(info) = &update_check.info {
                warn!("update check status info: {}", info);
            }
            return Err(IsolatedInstallError::InstallPlan(anyhow!(
                "Unexpected update check status: {:?}",
                update_check.status
            )));
        }
    };
    let url = urls
        .next()
        .ok_or_else(|| IsolatedInstallError::InstallPlan(anyhow!("No url in Omaha response")))?;

    let rest_count = urls.count();
    if rest_count != 0 {
        warn!(found = rest_count + 1, "Only 1 url is supported");
    }

    let mut packages = update_check.get_all_packages();
    let package = packages.next().ok_or_else(|| {
        IsolatedInstallError::InstallPlan(anyhow!("No package in Omaha response"))
    })?;

    let rest_count = packages.count();
    if rest_count != 0 {
        warn!(found = rest_count + 1, "Only 1 package is supported");
    }

    let full_url = url.to_owned() + &package.name;

    match PinnedAbsolutePackageUrl::parse(&full_url) {
        Ok(url) => Ok(FuchsiaInstallPlan { url, install_source: request_params.source }),
        Err(err) => Err(IsolatedInstallError::InstallPlan(anyhow!(
            "Failed to parse {} to PinnedAbsolutePackageUrl: {:#}",
            full_url,
            anyhow!(err),
        ))),
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        assert_matches::assert_matches,
        omaha_client::protocol::response::{App, Manifest, Package, Packages, UpdateCheck},
    };

    const TEST_URL_BASE: &str = "fuchsia-pkg://fuchsia.com/";
    const TEST_PACKAGE_NAME: &str =
        "update/0?hash=0000000000000000000000000000000000000000000000000000000000000000";

    #[test]
    fn test_simple_response() {
        let request_params = RequestParams::default();
        let mut update_check = UpdateCheck::ok([TEST_URL_BASE]);
        update_check.manifest = Some(Manifest {
            packages: Packages {
                package: vec![Package {
                    name: TEST_PACKAGE_NAME.to_string(),
                    ..Package::default()
                }],
            },
            ..Manifest::default()
        });
        let response = Response {
            apps: vec![App { update_check: Some(update_check), ..App::default() }],
            ..Response::default()
        };

        let install_plan = try_create_install_plan(&request_params, &response).unwrap();
        assert_eq!(install_plan.url.to_string(), TEST_URL_BASE.to_string() + TEST_PACKAGE_NAME);
        assert_eq!(install_plan.install_source, request_params.source);
    }

    #[test]
    fn test_no_app() {
        let request_params = RequestParams::default();
        let response = Response::default();

        assert_matches!(
            try_create_install_plan(&request_params, &response),
            Err(IsolatedInstallError::InstallPlan(_))
        );
    }

    #[test]
    fn test_multiple_app() {
        let request_params = RequestParams::default();
        let mut update_check = UpdateCheck::ok([TEST_URL_BASE]);
        update_check.manifest = Some(Manifest {
            packages: Packages {
                package: vec![Package {
                    name: TEST_PACKAGE_NAME.to_string(),
                    ..Package::default()
                }],
            },
            ..Manifest::default()
        });
        let response = Response {
            apps: vec![App { update_check: Some(update_check), ..App::default() }],
            ..Response::default()
        };

        let install_plan = try_create_install_plan(&request_params, &response).unwrap();
        assert_eq!(install_plan.url.to_string(), TEST_URL_BASE.to_string() + TEST_PACKAGE_NAME);
        assert_eq!(install_plan.install_source, request_params.source);
    }

    #[test]
    fn test_no_update_check() {
        let request_params = RequestParams::default();
        let response = Response { apps: vec![App::default()], ..Response::default() };

        assert_matches!(
            try_create_install_plan(&request_params, &response),
            Err(IsolatedInstallError::InstallPlan(_))
        );
    }

    #[test]
    fn test_no_urls() {
        let request_params = RequestParams::default();
        let response = Response {
            apps: vec![App { update_check: Some(UpdateCheck::default()), ..App::default() }],
            ..Response::default()
        };

        assert_matches!(
            try_create_install_plan(&request_params, &response),
            Err(IsolatedInstallError::InstallPlan(_))
        );
    }

    #[test]
    fn test_app_error_status() {
        let request_params = RequestParams::default();
        let response = Response {
            apps: vec![App {
                status: OmahaStatus::Error("error-unknownApplication".to_string()),
                ..App::default()
            }],
            ..Response::default()
        };

        assert_matches!(
            try_create_install_plan(&request_params, &response),
            Err(IsolatedInstallError::InstallPlan(_))
        );
    }

    #[test]
    fn test_no_update() {
        let request_params = RequestParams::default();
        let response = Response {
            apps: vec![App { update_check: Some(UpdateCheck::no_update()), ..App::default() }],
            ..Response::default()
        };

        assert_matches!(
            try_create_install_plan(&request_params, &response),
            Err(IsolatedInstallError::InstallPlan(_))
        );
    }

    #[test]
    fn test_invalid_url() {
        let request_params = RequestParams::default();
        let response = Response {
            apps: vec![App {
                update_check: Some(UpdateCheck::ok(["invalid-url"])),
                ..App::default()
            }],
            ..Response::default()
        };
        assert_matches!(
            try_create_install_plan(&request_params, &response),
            Err(IsolatedInstallError::InstallPlan(_))
        );
    }

    #[test]
    fn test_no_manifest() {
        let request_params = RequestParams::default();
        let response = Response {
            apps: vec![App {
                update_check: Some(UpdateCheck::ok([TEST_URL_BASE])),
                ..App::default()
            }],
            ..Response::default()
        };

        assert_matches!(
            try_create_install_plan(&request_params, &response),
            Err(IsolatedInstallError::InstallPlan(_))
        );
    }
}
