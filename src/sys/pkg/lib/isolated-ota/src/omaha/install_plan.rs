// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Copied from //src/sys/pkg/bin/omaha-client.

use fuchsia_url::pkg_url::PkgUrl;
use log::{error, warn};
use omaha_client::installer::Plan;
use omaha_client::{
    protocol::{
        request::InstallSource,
        response::{OmahaStatus, Response},
    },
    request_builder::RequestParams,
};
use thiserror::Error;

#[derive(Debug, PartialEq)]
pub struct FuchsiaInstallPlan {
    /// The fuchsia TUF repo URL, e.g. fuchsia-pkg://fuchsia.com/update/0?hash=...
    pub url: PkgUrl,
    pub install_source: InstallSource,
}

#[derive(Debug, Error, PartialEq)]
pub enum InstallPlanErrors {
    #[error("Fuchsia Install Plan could not be created from response")]
    Failed,
}

impl Plan for FuchsiaInstallPlan {
    type Error = InstallPlanErrors;

    fn try_create_from(
        request_params: &RequestParams,
        resp: &Response,
    ) -> Result<Self, Self::Error> {
        let (app, rest) = if let Some((app, rest)) = resp.apps.split_first() {
            (app, rest)
        } else {
            error!("No app in Omaha response.");
            return Err(InstallPlanErrors::Failed);
        };

        if !rest.is_empty() {
            warn!("Only 1 app is supported, found {}", resp.apps.len());
        }

        if app.status != OmahaStatus::Ok {
            error!("Found non-ok app status: {:?}", app.status);
            return Err(InstallPlanErrors::Failed);
        }

        let update_check = if let Some(update_check) = &app.update_check {
            update_check
        } else {
            error!("No update_check in Omaha response.");
            return Err(InstallPlanErrors::Failed);
        };

        let urls = match update_check.status {
            OmahaStatus::Ok => {
                if let Some(urls) = &update_check.urls {
                    &urls.url
                } else {
                    error!("No urls in Omaha response.");
                    return Err(InstallPlanErrors::Failed);
                }
            }
            OmahaStatus::NoUpdate => {
                error!("Was asked to create an install plan for a NoUpdate Omaha response");
                return Err(InstallPlanErrors::Failed);
            }
            _ => {
                warn!("Unexpected update check status: {:?}", update_check.status);
                if let Some(info) = &update_check.info {
                    warn!("update check status info: {}", info);
                }
                return Err(InstallPlanErrors::Failed);
            }
        };
        let (url, rest) = if let Some((url, rest)) = urls.split_first() {
            (url, rest)
        } else {
            error!("No url in Omaha response.");
            return Err(InstallPlanErrors::Failed);
        };

        if !rest.is_empty() {
            warn!("Only 1 url is supported, found {}", urls.len());
        }

        let manifest = if let Some(manifest) = &update_check.manifest {
            manifest
        } else {
            error!("No manifest in Omaha response.");
            return Err(InstallPlanErrors::Failed);
        };

        let (package, rest) = if let Some((package, rest)) = manifest.packages.package.split_first()
        {
            (package, rest)
        } else {
            error!("No package in Omaha response.");
            return Err(InstallPlanErrors::Failed);
        };

        if !rest.is_empty() {
            warn!("Only 1 package is supported, found {}", manifest.packages.package.len());
        }

        let full_url = url.codebase.clone() + &package.name;
        match PkgUrl::parse(&full_url) {
            Ok(url) => {
                Ok(FuchsiaInstallPlan { url, install_source: request_params.source.clone() })
            }
            Err(err) => {
                error!("Failed to parse {} to PkgUrl: {}", url.codebase, err);
                Err(InstallPlanErrors::Failed)
            }
        }
    }

    fn id(&self) -> String {
        self.url.to_string()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use omaha_client::protocol::response::{App, Manifest, Package, Packages, UpdateCheck};

    const TEST_URL_BASE: &str = "fuchsia-pkg://fuchsia.com/";
    const TEST_PACKAGE_NAME: &str = "update/0";

    #[test]
    fn test_simple_response() {
        let request_params = RequestParams::default();
        let mut update_check = UpdateCheck::ok(vec![TEST_URL_BASE.to_string()]);
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

        let install_plan = FuchsiaInstallPlan::try_create_from(&request_params, &response).unwrap();
        assert_eq!(install_plan.url.to_string(), TEST_URL_BASE.to_string() + TEST_PACKAGE_NAME);
        assert_eq!(install_plan.install_source, request_params.source);
    }

    #[test]
    fn test_no_app() {
        let request_params = RequestParams::default();
        let response = Response::default();

        assert_eq!(
            FuchsiaInstallPlan::try_create_from(&request_params, &response),
            Err(InstallPlanErrors::Failed)
        );
    }

    #[test]
    fn test_multiple_app() {
        let request_params = RequestParams::default();
        let mut update_check = UpdateCheck::ok(vec![TEST_URL_BASE.to_string()]);
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

        let install_plan = FuchsiaInstallPlan::try_create_from(&request_params, &response).unwrap();
        assert_eq!(install_plan.url.to_string(), TEST_URL_BASE.to_string() + TEST_PACKAGE_NAME);
        assert_eq!(install_plan.install_source, request_params.source);
    }

    #[test]
    fn test_no_update_check() {
        let request_params = RequestParams::default();
        let response = Response { apps: vec![App::default()], ..Response::default() };

        assert_eq!(
            FuchsiaInstallPlan::try_create_from(&request_params, &response),
            Err(InstallPlanErrors::Failed)
        );
    }

    #[test]
    fn test_no_urls() {
        let request_params = RequestParams::default();
        let response = Response {
            apps: vec![App { update_check: Some(UpdateCheck::default()), ..App::default() }],
            ..Response::default()
        };

        assert_eq!(
            FuchsiaInstallPlan::try_create_from(&request_params, &response),
            Err(InstallPlanErrors::Failed)
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

        assert_eq!(
            FuchsiaInstallPlan::try_create_from(&request_params, &response),
            Err(InstallPlanErrors::Failed)
        );
    }

    #[test]
    fn test_no_update() {
        let request_params = RequestParams::default();
        let response = Response {
            apps: vec![App { update_check: Some(UpdateCheck::no_update()), ..App::default() }],
            ..Response::default()
        };

        assert_eq!(
            FuchsiaInstallPlan::try_create_from(&request_params, &response),
            Err(InstallPlanErrors::Failed)
        );
    }

    #[test]
    fn test_invalid_url() {
        let request_params = RequestParams::default();
        let response = Response {
            apps: vec![App {
                update_check: Some(UpdateCheck::ok(vec!["invalid-url".to_string()])),
                ..App::default()
            }],
            ..Response::default()
        };
        assert_eq!(
            FuchsiaInstallPlan::try_create_from(&request_params, &response),
            Err(InstallPlanErrors::Failed)
        );
    }

    #[test]
    fn test_no_manifest() {
        let request_params = RequestParams::default();
        let response = Response {
            apps: vec![App {
                update_check: Some(UpdateCheck::ok(vec![TEST_URL_BASE.to_string()])),
                ..App::default()
            }],
            ..Response::default()
        };

        assert_eq!(
            FuchsiaInstallPlan::try_create_from(&request_params, &response),
            Err(InstallPlanErrors::Failed)
        );
    }

    #[test]
    fn test_install_plan_id() {
        let url = TEST_URL_BASE.to_string() + TEST_PACKAGE_NAME;
        let install_plan = FuchsiaInstallPlan {
            url: PkgUrl::parse(&url).unwrap(),
            install_source: InstallSource::ScheduledTask,
        };

        assert_eq!(install_plan.id(), url);
    }
}
