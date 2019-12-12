// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {failure::Error, fidl_fuchsia_settings::*};

pub async fn command(
    proxy: DoNotDisturbProxy,
    user_dnd: Option<bool>,
    night_mode_dnd: Option<bool>,
) -> Result<String, Error> {
    let mut output = String::new();
    let mut settings = DoNotDisturbSettings::empty();

    settings.user_initiated_do_not_disturb = user_dnd;
    settings.night_mode_initiated_do_not_disturb = night_mode_dnd;

    if settings != DoNotDisturbSettings::empty() {
        let mutate_result = proxy.set(settings).await?;
        match mutate_result {
            Ok(_) => {
                let mut settings_clone = DoNotDisturbSettings::empty();
                settings_clone.user_initiated_do_not_disturb = user_dnd;
                settings_clone.night_mode_initiated_do_not_disturb = night_mode_dnd;
                output.push_str(&format!(
                    "Successfully set do_not_disturb to {:#?}",
                    describe_do_not_disturb_setting(&settings_clone)
                ));
            }
            Err(err) => output.push_str(&format!("{:?}", err)),
        }
    } else {
        let setting = proxy.watch().await?;
        let setting_string = describe_do_not_disturb_setting(&setting);
        output.push_str(&setting_string);
    }

    Ok(output)
}

fn describe_do_not_disturb_setting(do_not_disturb_setting: &DoNotDisturbSettings) -> String {
    let mut output = String::new();

    output.push_str("DoNotDisturb { ");

    if let Some(user_dnd) = do_not_disturb_setting.user_initiated_do_not_disturb {
        output.push_str(&format!("user_initiated_do_not_disturb: {} ", user_dnd))
    }

    if let Some(night_mode_dnd) = do_not_disturb_setting.night_mode_initiated_do_not_disturb {
        output.push_str(&format!("night_mode_initiated_do_not_disturb: {} ", night_mode_dnd))
    }

    output.push_str("}");

    output
}
