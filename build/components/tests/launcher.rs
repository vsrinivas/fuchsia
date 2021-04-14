// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
mod tests {
    use anyhow::{Context as _, Error};
    use fuchsia_async as fasync;
    use fuchsia_component::client::{launch, launcher};

    const LAUNCH_URLS: &[&str] = &[
        // The target_name is used for the package and component names, when both are unspecified.
        "fuchsia-pkg://fuchsia.com/return-zero-unnamed#meta/return-zero-unnamed.cmx",
        // The target_name is used for both names, when only the package name is specified.
        "fuchsia-pkg://fuchsia.com/return-zero#meta/return-zero.cmx",
        // The target_name is used for the package name, if only the component name is specified.
        "fuchsia-pkg://fuchsia.com/return-zero-component-named#meta/return-zero.cmx",
        // When both names are specified, they are applied as is to the manifest name.
        "fuchsia-pkg://fuchsia.com/return-zero-package-name#meta/return-zero-component-name.cmx",
    ];

    #[fasync::run_singlethreaded(test)]
    async fn launch_components() -> Result<(), Error> {
        // Each CFv1 launch URL in the above list corresponds to a single package created via the
        // `fuchsia_package_with_single_component`. Packages created with this template follow this
        // naming convention:
        //
        // ------------------------------------------------------------------------
        // | package_name | component_name | manifest                             |
        // ------------------------------------------------------------------------
        // | unspecified  | unspecified    | target_name#meta/target_name.cmx     |
        // | specified    | unspecified    | package_name#meta/package_name.cmx   |
        // | unspecified  | specified      | target_name#meta/component_name.cmx  |
        // | specified    | specified      | package_name#meta/component_name.cmx |
        // ------------------------------------------------------------------------
        //
        // This test launches each component (all of which are simply the `return_zero` binary) and
        // verifies they exit with the expected return code. This is an indirect validation that the
        // unique package/component combinations were created.
        let launcher = launcher().context("Failed to open launcher service")?;
        for launch_url in LAUNCH_URLS.iter() {
            let exit_status =
                launch(&launcher, launch_url.to_string(), None).unwrap().wait().await?;
            assert_eq!(exit_status.code(), 0);
        }
        Ok(())
    }
}
