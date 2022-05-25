// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Copied from //src/sys/pkg/bin/omaha-client.

use fuchsia_url::PinnedAbsolutePackageUrl;
use omaha_client::{installer::Plan, protocol::request::InstallSource};

#[derive(Debug, PartialEq)]
pub struct FuchsiaInstallPlan {
    /// The fuchsia TUF repo URL, e.g. fuchsia-pkg://fuchsia.com/update/0?hash=...
    pub url: PinnedAbsolutePackageUrl,
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

    const TEST_URL: &str = "fuchsia-pkg://fuchsia.com/update/0?hash=0000000000000000000000000000000000000000000000000000000000000000";

    #[test]
    fn test_install_plan_id() {
        let install_plan = FuchsiaInstallPlan {
            url: PinnedAbsolutePackageUrl::parse(TEST_URL).unwrap(),
            install_source: InstallSource::ScheduledTask,
        };

        assert_eq!(install_plan.id(), TEST_URL);
    }
}
