// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_url::pkg_url::PkgUrl;
use omaha_client::{installer::Plan, protocol::request::InstallSource};
use std::fmt;

#[derive(Debug, PartialEq)]
pub enum UpdatePackageUrl {
    /// The pinned fuchsia update package URL, e.g. fuchsia-pkg://fuchsia.com/update/0?hash=...
    System(PkgUrl),
    /// The pinned package URL for eagerly updated package.
    Package(PkgUrl),
}

impl fmt::Display for UpdatePackageUrl {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            UpdatePackageUrl::System(url) => write!(f, "System({url})"),
            UpdatePackageUrl::Package(url) => write!(f, "Package({url})"),
        }
    }
}

#[derive(Debug, PartialEq)]
pub struct FuchsiaInstallPlan {
    pub update_package_urls: Vec<UpdatePackageUrl>,
    pub install_source: InstallSource,
    pub urgent_update: bool,
}

impl Plan for FuchsiaInstallPlan {
    fn id(&self) -> String {
        self.update_package_urls.iter().map(|url| url.to_string()).collect::<Vec<_>>().join(", ")
    }
}

impl FuchsiaInstallPlan {
    #[cfg(test)]
    pub fn new_test() -> Self {
        Self {
            update_package_urls: vec![UpdatePackageUrl::System(
                "fuchsia-pkg://fuchsia.com/update/0".parse().unwrap(),
            )],
            install_source: InstallSource::OnDemand,
            urgent_update: false,
        }
    }

    pub fn is_system_update(&self) -> bool {
        self.update_package_urls.iter().any(|u| matches!(u, UpdatePackageUrl::System(_)))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const TEST_URL_BASE: &str = "fuchsia-pkg://fuchsia.com/";
    const TEST_PACKAGE_NAME: &str = "update/0";

    #[test]
    fn test_install_plan_id_system_update() {
        let url = TEST_URL_BASE.to_string() + TEST_PACKAGE_NAME;
        let install_plan = FuchsiaInstallPlan {
            update_package_urls: vec![UpdatePackageUrl::System(url.parse().unwrap())],
            install_source: InstallSource::ScheduledTask,
            urgent_update: false,
        };

        assert_eq!(install_plan.id(), format!("System({url})"));
    }

    #[test]
    fn test_install_plan_id_package_groups() {
        let url1 = "fuchsia-pkg://foo.com/foo";
        let url2 = "fuchsia-pkg://bar.com/bar";
        let install_plan = FuchsiaInstallPlan {
            update_package_urls: vec![
                UpdatePackageUrl::System(PkgUrl::parse(&url1).unwrap()),
                UpdatePackageUrl::Package(PkgUrl::parse(&url2).unwrap()),
            ],
            install_source: InstallSource::ScheduledTask,
            urgent_update: false,
        };

        assert_eq!(install_plan.id(), format!("System({url1}), Package({url2})"));
    }
}
