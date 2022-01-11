// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::utils::{self, Either, WatchOrSetResult};
use crate::Keyboard;
use anyhow::format_err;
use fidl_fuchsia_settings::{KeyboardProxy, KeyboardSettings};

pub async fn command(proxy: KeyboardProxy, keyboard: Keyboard) -> WatchOrSetResult {
    if keyboard.autorepeat_delay.unwrap_or(0) < 0 || keyboard.autorepeat_period.unwrap_or(0) < 0 {
        return Err(format_err!("Negative values are invalid for autorepeat values."));
    }
    let settings = KeyboardSettings::from(keyboard);

    if settings == KeyboardSettings::EMPTY {
        // No values set, perform a watch loop instead.
        Ok(Either::Watch(utils::watch_to_stream(proxy, |p| p.watch())))
    } else {
        let mutate_result = proxy.set(settings).await?;
        Ok(Either::Set(match mutate_result {
            Ok(_) => format!("Successfully set Keyboard to {:#?}", keyboard),
            Err(err) => format!("{:?}", err),
        }))
    }
}

impl From<KeyboardSettings> for Keyboard {
    fn from(src: KeyboardSettings) -> Self {
        Keyboard {
            keymap: src.keymap,
            autorepeat_delay: src.autorepeat.map(|a| a.delay),
            autorepeat_period: src.autorepeat.map(|a| a.period),
        }
    }
}

impl From<Keyboard> for KeyboardSettings {
    fn from(src: Keyboard) -> KeyboardSettings {
        KeyboardSettings {
            keymap: src.keymap,
            autorepeat: if src.autorepeat_delay.is_none() && src.autorepeat_period.is_none() {
                None
            } else {
                Some(fidl_fuchsia_settings::Autorepeat {
                    delay: src.autorepeat_delay.unwrap_or(0),
                    period: src.autorepeat_period.unwrap_or(0),
                })
            },
            ..KeyboardSettings::EMPTY
        }
    }
}
