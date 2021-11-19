// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Copied from //src/sys/pkg/bin/omaha-client.

use fuchsia_url::pkg_url::PkgUrl;
use omaha_client::{installer::Plan, protocol::request::InstallSource};

#[derive(Debug, PartialEq)]
pub struct FuchsiaInstallPlan {
    /// The fuchsia TUF repo URL, e.g. fuchsia-pkg://fuchsia.com/update/0?hash=...
    pub url: PkgUrl,
    pub install_source: InstallSource,
}

impl Plan for FuchsiaInstallPlan {
    fn id(&self) -> String {
        self.url.to_string()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const TEST_URL_BASE: &str = "fuchsia-pkg://fuchsia.com/";
    const TEST_PACKAGE_NAME: &str = "update/0";

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
