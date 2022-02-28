// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Result;
use ffx_core::ffx_plugin;
use ffx_setui_accessibility_args::{Accessibility, SubcommandEnum};
use fidl_fuchsia_settings::AccessibilityProxy;

pub use utils;

mod add_caption;
mod set;
mod watch;

#[ffx_plugin(
    "setui",
    AccessibilityProxy = "core/setui_service:expose:fuchsia.settings.Accessibility"
)]
pub async fn run_command(
    accessibility_proxy: AccessibilityProxy,
    accessibility: Accessibility,
) -> Result<()> {
    match accessibility.subcommand {
        SubcommandEnum::AddCaption(args) => {
            add_caption::add_caption(accessibility_proxy, args).await
        }
        SubcommandEnum::Set(args) => set::set(accessibility_proxy, args).await,
        SubcommandEnum::Watch(_) => watch::watch(accessibility_proxy).await,
    }
}
