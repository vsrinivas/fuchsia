// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::utils::{self, Either, WatchOrSetResult};
use fidl_fuchsia_settings::{FactoryResetProxy, FactoryResetSettings};

pub async fn command(
    proxy: FactoryResetProxy,
    is_local_reset_allowed: Option<bool>,
) -> WatchOrSetResult {
    let mut settings = FactoryResetSettings::EMPTY;

    settings.is_local_reset_allowed = is_local_reset_allowed;

    Ok(if settings != FactoryResetSettings::EMPTY {
        Either::Set(if let Err(err) = proxy.set(settings.clone()).await? {
            format!("{:?}", err)
        } else {
            format!("Successfully set factory_reset to {:?}", settings)
        })
    } else {
        Either::Watch(utils::watch_to_stream(proxy, |p| p.watch()))
    })
}
