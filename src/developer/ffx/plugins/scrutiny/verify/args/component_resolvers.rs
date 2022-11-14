// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    std::path::PathBuf,
    {argh::FromArgs, ffx_core::ffx_command},
};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "component-resolvers",
    description = "Verifies that component configured to use custom component resolvers are permitted by an allowlist.",
    example = "To verify component resolvers on your current eng build:

    $ ffx scrutiny verify component-resolvers \
        --product-bundle $(fx get-build-dir)/obj/build/images/fuchsia/product_bundle \
        --allowlist ../../src/security/policy/component_resolvers_policy.json5",
    note = "Verifies all components that use a custom component resolver."
)]
pub struct Command {
    /// absolute or working directory-relative path to a product bundle.
    #[argh(option)]
    pub product_bundle: PathBuf,
    /// absolute or working directory-relative path to allowlist file that specifies which components
    /// may use particular custom component resolvers.
    #[argh(option)]
    pub allowlist: PathBuf,
}
