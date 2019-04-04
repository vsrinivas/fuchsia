// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_uri::pkg_uri::FuchsiaPkgUri;
use log::{error, info, warn};
use omaha_client::install_plan::InstallPlan;
use omaha_client::protocol::response::{OmahaStatus, Response};

#[derive(Debug, PartialEq)]
pub struct FuchsiaInstallPlan {
    /// The fuchsia TUF repo URI, e.g. fuchsia-pkg://fuchsia.com/update/0?hash=...
    pub uri: FuchsiaPkgUri,
}

impl InstallPlan for FuchsiaInstallPlan {
    fn from_response(resp: &Response) -> Option<Self> {
        let (app, rest) = if let Some((app, rest)) = resp.apps.split_first() {
            (app, rest)
        } else {
            error!("No app in Omaha response.");
            return None;
        };

        if !rest.is_empty() {
            warn!("Only 1 app is supported, found {}", resp.apps.len());
        }

        if app.status != OmahaStatus::Ok {
            error!("Found non-ok app status: {:?}", app.status);
            return None;
        }

        let update_check = if let Some(update_check) = &app.update_check {
            update_check
        } else {
            error!("No update_check in Omaha response.");
            return None;
        };

        let urls = match update_check.status {
            OmahaStatus::Ok => {
                if let Some(urls) = &update_check.urls {
                    &urls.url
                } else {
                    error!("No urls in Omaha response.");
                    return None;
                }
            }
            OmahaStatus::NoUpdate => {
                info!("Omaha returned that there is no update available at this time.");
                return None;
            }
            _ => {
                warn!("Unexpected update check status: {:?}", update_check.status);
                if let Some(info) = &update_check.info {
                    warn!("update check status info: {}", info);
                }
                return None;
            }
        };
        let (url, rest) = if let Some((url, rest)) = urls.split_first() {
            (url, rest)
        } else {
            error!("No url in Omaha response.");
            return None;
        };

        if !rest.is_empty() {
            warn!("Only 1 url is supported, found {}", urls.len());
        }

        match FuchsiaPkgUri::parse(&url.codebase) {
            Ok(uri) => Some(FuchsiaInstallPlan { uri: uri }),
            Err(err) => {
                error!("Failed to parse {} to FuchsiaPkgUri: {}", url.codebase, err);
                None
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use omaha_client::protocol::response::{App, UpdateCheck};

    const TEST_URI: &str = "fuchsia-pkg://fuchsia.com/update/0";

    #[test]
    fn test_simple_response() {
        let response = Response {
            apps: vec![App {
                update_check: Some(UpdateCheck::ok(vec![TEST_URI.to_string()])),
                ..App::default()
            }],
            ..Response::default()
        };

        let install_plan = FuchsiaInstallPlan::from_response(&response).unwrap();
        assert_eq!(install_plan.uri.to_string(), TEST_URI);
    }

    #[test]
    fn test_no_app() {
        let response = Response::default();

        assert_eq!(FuchsiaInstallPlan::from_response(&response), None);
    }

    #[test]
    fn test_multiple_app() {
        let response = Response {
            apps: vec![
                App {
                    update_check: Some(UpdateCheck::ok(vec![TEST_URI.to_string()])),
                    ..App::default()
                },
                App::default(),
            ],
            ..Response::default()
        };

        let install_plan = FuchsiaInstallPlan::from_response(&response).unwrap();
        assert_eq!(install_plan.uri.to_string(), TEST_URI);
    }

    #[test]
    fn test_no_update_check() {
        let response = Response { apps: vec![App::default()], ..Response::default() };

        assert_eq!(FuchsiaInstallPlan::from_response(&response), None);
    }

    #[test]
    fn test_no_urls() {
        let response = Response {
            apps: vec![App { update_check: Some(UpdateCheck::default()), ..App::default() }],
            ..Response::default()
        };

        assert_eq!(FuchsiaInstallPlan::from_response(&response), None);
    }

    #[test]
    fn test_app_error_status() {
        let response = Response {
            apps: vec![App {
                status: OmahaStatus::Error("error-unknownApplication".to_string()),
                ..App::default()
            }],
            ..Response::default()
        };

        assert_eq!(FuchsiaInstallPlan::from_response(&response), None);
    }

    #[test]
    fn test_no_update() {
        let response = Response {
            apps: vec![App { update_check: Some(UpdateCheck::no_update()), ..App::default() }],
            ..Response::default()
        };

        assert_eq!(FuchsiaInstallPlan::from_response(&response), None);
    }

    #[test]
    fn test_invalid_uri() {
        let response = Response {
            apps: vec![App {
                update_check: Some(UpdateCheck::ok(vec!["invalid-uri".to_string()])),
                ..App::default()
            }],
            ..Response::default()
        };
        assert_eq!(FuchsiaInstallPlan::from_response(&response), None);
    }

}
