// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::utils::{self, Either, WatchOrSetResult};
use fidl_fuchsia_settings::{NightModeProxy, NightModeSettings};

pub async fn command(proxy: NightModeProxy, night_mode_enabled: Option<bool>) -> WatchOrSetResult {
    Ok(if let Some(night_mode_enabled_value) = night_mode_enabled {
        let mut settings = NightModeSettings::EMPTY;
        settings.night_mode_enabled = Some(night_mode_enabled_value);

        let mutate_result = proxy.set(settings).await?;
        Either::Set(match mutate_result {
            Ok(_) => {
                format!("Successfully set night_mode_enabled to {}", night_mode_enabled_value)
            }
            Err(err) => format!("{:#?}", err),
        })
    } else {
        Either::Watch(utils::watch_to_stream(proxy, |p| p.watch()))
    })
}
