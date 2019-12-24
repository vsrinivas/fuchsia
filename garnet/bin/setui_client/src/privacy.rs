// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {anyhow::Error, fidl_fuchsia_settings::*};

pub async fn command(
    proxy: PrivacyProxy,
    user_data_sharing_consent: Option<bool>,
) -> Result<String, Error> {
    if let Some(user_data_sharing_consent_value) = user_data_sharing_consent {
        let mut settings = PrivacySettings::empty();
        settings.user_data_sharing_consent = Some(user_data_sharing_consent_value);

        let mutate_result = proxy.set(settings).await?;
        match mutate_result {
            Ok(_) => Ok(format!(
                "Successfully set user_data_sharing_consent to {}",
                user_data_sharing_consent_value
            )),
            Err(err) => Ok(format!("{:?}", err)),
        }
    } else {
        let setting = proxy.watch().await?;

        match setting {
            Ok(setting_value) => Ok(format!("{:?}", setting_value)),
            Err(err) => Ok(format!("{:?}", err)),
        }
    }
}
