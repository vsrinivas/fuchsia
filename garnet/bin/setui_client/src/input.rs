// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl_fuchsia_settings::{InputDeviceSettings, InputProxy, Microphone},
};

pub async fn command(proxy: InputProxy, mic_muted: Option<bool>) -> Result<String, Error> {
    let mut output = String::new();
    let mut input_settings = InputDeviceSettings::EMPTY;
    let mut microphone = Microphone::EMPTY;

    if mic_muted.is_some() {
        microphone.muted = mic_muted;
        input_settings.microphone = Some(microphone);
    }

    if input_settings == InputDeviceSettings::EMPTY {
        let setting_value = proxy.watch().await?;
        output.push_str(&format!("{:#?}", setting_value));
    } else {
        if let Err(err) = proxy.set(input_settings).await? {
            output.push_str(&format!("{:?}", err));
        } else if let Some(muted) = mic_muted {
            output.push_str(&format!("Successfully set mic mute to {:?}\n", muted));
        }
    }
    Ok(output)
}
