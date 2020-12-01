// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fidl_fuchsia_settings::{FactoryResetProxy, FactoryResetSettings};

pub async fn command(
    proxy: FactoryResetProxy,
    is_local_reset_allowed: Option<bool>,
) -> Result<String, Error> {
    let mut output = String::new();
    let mut settings = FactoryResetSettings::EMPTY;

    settings.is_local_reset_allowed = is_local_reset_allowed;

    if settings != FactoryResetSettings::EMPTY {
        if let Err(err) = proxy.set(settings).await? {
            output.push_str(&format!("{:?}", err))
        } else {
            let mut settings = FactoryResetSettings::EMPTY;
            settings.is_local_reset_allowed = is_local_reset_allowed;
            output.push_str(&format!("Successfully set factory_reset to {:?}", settings));
        }
    } else {
        let settings = proxy.watch().await?;
        output.push_str(&format!("{:?}", settings));
    }

    Ok(output)
}
