// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {anyhow::Error, fidl_fuchsia_settings::*};

pub async fn command(
    proxy: NightModeProxy,
    night_mode_enabled: Option<bool>,
) -> Result<String, Error> {
    if let Some(night_mode_enabled_value) = night_mode_enabled {
        let mut settings = NightModeSettings::empty();
        settings.night_mode_enabled = Some(night_mode_enabled_value);

        let mutate_result = proxy.set(settings).await?;
        match mutate_result {
            Ok(_) => {
                Ok(format!("Successfully set night_mode_enabled to {}", night_mode_enabled_value))
            }
            Err(err) => Ok(format!("{:#?}", err)),
        }
    } else {
        let setting = proxy.watch().await?;
        Ok(format!("{:#?}", setting))
    }
}
