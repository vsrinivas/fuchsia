// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::utils::{self, Either, WatchOrSetResult};
use fidl_fuchsia_settings::{DoNotDisturbProxy, DoNotDisturbSettings};

pub async fn command(
    proxy: DoNotDisturbProxy,
    user_dnd: Option<bool>,
    night_mode_dnd: Option<bool>,
) -> WatchOrSetResult {
    let mut settings = DoNotDisturbSettings::EMPTY;

    settings.user_initiated_do_not_disturb = user_dnd;
    settings.night_mode_initiated_do_not_disturb = night_mode_dnd;

    Ok(if settings != DoNotDisturbSettings::EMPTY {
        let mutate_result = proxy.set(settings).await?;
        Either::Set(match mutate_result {
            Ok(_) => {
                let mut settings_clone = DoNotDisturbSettings::EMPTY;
                settings_clone.user_initiated_do_not_disturb = user_dnd;
                settings_clone.night_mode_initiated_do_not_disturb = night_mode_dnd;
                format!(
                    "Successfully set do_not_disturb to {:#?}",
                    describe_do_not_disturb_setting(&settings_clone)
                )
            }
            Err(err) => format!("{:?}", err),
        })
    } else {
        Either::Watch(utils::formatted_watch_to_stream(
            proxy,
            |p| p.watch(),
            |s| describe_do_not_disturb_setting(&s),
        ))
    })
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
