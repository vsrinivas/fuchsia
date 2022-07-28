// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Result;
use fidl_fuchsia_settings::AccessibilityProxy;
use utils::handle_mixed_result;
use utils::{self, Either, WatchOrSetResult};

pub async fn watch(accessibility_proxy: AccessibilityProxy) -> Result<()> {
    handle_mixed_result("AccessibilityWatch", command(accessibility_proxy).await).await
}

async fn command(proxy: AccessibilityProxy) -> WatchOrSetResult {
    Ok(Either::Watch(utils::watch_to_stream(proxy, |p| p.watch())))
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::setup_fake_accessibility_proxy;
    use fidl_fuchsia_settings::{AccessibilityRequest, AccessibilitySettings};

    #[fuchsia_async::run_singlethreaded(test)]
    async fn validate_accessibility_watch() -> Result<()> {
        let proxy = setup_fake_accessibility_proxy(move |req| match req {
            AccessibilityRequest::Set { .. } => {
                panic!("Unexpected call to set");
            }
            AccessibilityRequest::Watch { responder } => {
                let _ = responder.send(AccessibilitySettings::EMPTY);
            }
        });

        let output = utils::assert_watch!(command(proxy));
        assert_eq!(output, format!("{:#?}", AccessibilitySettings::EMPTY));
        Ok(())
    }
}
