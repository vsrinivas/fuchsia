// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::utils::{self, Either, WatchOrSetResult};
use fidl_fuchsia_settings::{PrivacyProxy, PrivacySettings};

pub async fn command(
    proxy: PrivacyProxy,
    user_data_sharing_consent: Option<bool>,
) -> WatchOrSetResult {
    Ok(if let Some(user_data_sharing_consent_value) = user_data_sharing_consent {
        let mut settings = PrivacySettings::EMPTY;
        settings.user_data_sharing_consent = Some(user_data_sharing_consent_value);

        let mutate_result = proxy.set(settings).await?;
        Either::Set(match mutate_result {
            Ok(_) => format!(
                "Successfully set user_data_sharing_consent to {}",
                user_data_sharing_consent_value
            ),
            Err(err) => format!("{:?}", err),
        })
    } else {
        Either::Watch(utils::watch_to_stream(proxy, |p| p.watch()))
    })
}
