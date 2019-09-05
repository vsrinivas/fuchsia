// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {failure::Error, fidl_fuchsia_settings::*};

pub async fn command(
    proxy: PrivacyProxy,
    user_data_sharing_consent: Option<bool>,
) -> Result<String, Error> {
    let mut output = String::new();

    if let Some(user_data_sharing_consent_value) = user_data_sharing_consent {
        let mut settings = PrivacySettings::empty();
        settings.user_data_sharing_consent = Some(user_data_sharing_consent_value);

        let mutate_result = proxy.set(settings).await?;
        match mutate_result {
            Ok(_) => output.push_str(&format!(
                "Successfully set user_data_sharing_consent to {}",
                user_data_sharing_consent_value
            )),
            Err(err) => output.push_str(&format!("{:?}", err)),
        }
    } else {
        let setting = proxy.watch().await?;

        match setting {
            Ok(setting_value) => {
                let setting_string = describe_privacy_setting(&setting_value);
                output.push_str(&setting_string);
            }
            Err(err) => output.push_str(&format!("{:?}", err)),
        }
    }

    Ok(output)
}

fn describe_privacy_setting(privacy_setting: &PrivacySettings) -> String {
    let mut output = String::new();

    output.push_str("Privacy { ");

    if let Some(user_data_sharing_consent) = privacy_setting.user_data_sharing_consent {
        output.push_str(&format!("user_data_sharing_consent: {} ", user_data_sharing_consent));
    }

    output.push_str("}");

    return output;
}
