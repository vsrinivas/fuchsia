// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_url::PinnedAbsolutePackageUrl;
use omaha_client::{cup_ecdsa::RequestMetadata, installer::Plan, protocol::request::InstallSource};
use std::fmt;

#[derive(Debug, PartialEq, Eq)]
pub enum UpdatePackageUrl {
    /// The pinned fuchsia update package URL, e.g. fuchsia-pkg://fuchsia.example/update/0?hash=...
    System(PinnedAbsolutePackageUrl),
    /// The pinned package URL for eagerly updated package.
    Package(PinnedAbsolutePackageUrl),
}

impl fmt::Display for UpdatePackageUrl {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            UpdatePackageUrl::System(url) => write!(f, "System({url})"),
            UpdatePackageUrl::Package(url) => write!(f, "Package({url})"),
        }
    }
}

#[derive(Debug, Default, PartialEq, Eq)]
pub struct FuchsiaInstallPlan {
    pub update_package_urls: Vec<UpdatePackageUrl>,
    pub install_source: InstallSource,
    pub urgent_update: bool,
    pub omaha_response: Vec<u8>,
    pub request_metadata: Option<RequestMetadata>,
    pub ecdsa_signature: Option<Vec<u8>>,
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
                "fuchsia-pkg://fuchsia.test/update/0?hash=0000000000000000000000000000000000000000000000000000000000000000".parse().unwrap(),
            )],
            install_source: InstallSource::OnDemand,
            urgent_update: false,
            omaha_response: vec![],
            request_metadata: None,
            ecdsa_signature: None,
        }
    }

    pub fn is_system_update(&self) -> bool {
        self.update_package_urls.iter().any(|u| matches!(u, UpdatePackageUrl::System(_)))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_install_plan_id_system_update() {
        let url = "fuchsia-pkg://fuchsia.test/update/0?hash=0000000000000000000000000000000000000000000000000000000000000000";
        let install_plan = FuchsiaInstallPlan {
            update_package_urls: vec![UpdatePackageUrl::System(url.parse().unwrap())],
            ..FuchsiaInstallPlan::default()
        };

        assert_eq!(install_plan.id(), format!("System({url})"));
    }

    #[test]
    fn test_install_plan_id_package_groups() {
        let url1 = "fuchsia-pkg://foo.test/foo?hash=0000000000000000000000000000000000000000000000000000000000000000";
        let url2 = "fuchsia-pkg://bar.test/bar?hash=1111111111111111111111111111111111111111111111111111111111111111";
        let install_plan = FuchsiaInstallPlan {
            update_package_urls: vec![
                UpdatePackageUrl::System(PinnedAbsolutePackageUrl::parse(url1).unwrap()),
                UpdatePackageUrl::Package(PinnedAbsolutePackageUrl::parse(url2).unwrap()),
            ],
            ..FuchsiaInstallPlan::default()
        };

        assert_eq!(install_plan.id(), format!("System({url1}), Package({url2})"));
    }
}
