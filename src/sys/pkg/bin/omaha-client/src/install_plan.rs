// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_url::pkg_url::PkgUrl;
use omaha_client::{installer::Plan, protocol::request::InstallSource};

#[derive(Debug, PartialEq)]
pub enum UpdatePackageUrls {
    /// The pinned fuchsia update package URL, e.g. fuchsia-pkg://fuchsia.com/update/0?hash=...
    System(PkgUrl),
    /// List of pinned package URLs for eagerly updated packages.
    Packages(Vec<PkgUrl>),
}

#[derive(Debug, PartialEq)]
pub struct FuchsiaInstallPlan {
    pub update_package_urls: UpdatePackageUrls,
    pub install_source: InstallSource,
    pub urgent_update: bool,
}

impl Plan for FuchsiaInstallPlan {
    fn id(&self) -> String {
        match &self.update_package_urls {
            UpdatePackageUrls::System(url) => url.to_string(),
            UpdatePackageUrls::Packages(urls) => {
                urls.iter().map(|url| url.to_string()).collect::<Vec<_>>().join(", ")
            }
        }
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
            update_package_urls: UpdatePackageUrls::System(url.parse().unwrap()),
            install_source: InstallSource::ScheduledTask,
            urgent_update: false,
        };

        assert_eq!(install_plan.id(), url);
    }

    #[test]
    fn test_install_plan_id_package_groups() {
        let url1 = "fuchsia-pkg://foo.com/foo";
        let url2 = "fuchsia-pkg://bar.com/bar";
        let urls = [url1, url2].map(|url| PkgUrl::parse(&url).unwrap()).to_vec();
        let install_plan = FuchsiaInstallPlan {
            update_package_urls: UpdatePackageUrls::Packages(urls),
            install_source: InstallSource::ScheduledTask,
            urgent_update: false,
        };

        assert_eq!(install_plan.id(), format!("{url1}, {url2}"));
    }
}
