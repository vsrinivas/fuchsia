// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::utils::{self, Either, WatchOrSetResult};
use crate::Keyboard;
use anyhow::format_err;
use fidl_fuchsia_settings::{KeyboardProxy, KeyboardSettings};

pub async fn command(proxy: KeyboardProxy, keyboard: Keyboard) -> WatchOrSetResult {
    if keyboard.autorepeat_delay < 0 || keyboard.autorepeat_period < 0 {
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
            autorepeat_delay: src.autorepeat.map(|a| a.delay).unwrap_or(0),
            autorepeat_period: src.autorepeat.map(|a| a.period).unwrap_or(0),
        }
    }
}

impl From<Keyboard> for KeyboardSettings {
    fn from(src: Keyboard) -> KeyboardSettings {
        KeyboardSettings {
            keymap: src.keymap,
            autorepeat: if src.autorepeat_delay == 0 && src.autorepeat_period == 0 {
                None
            } else {
                Some(fidl_fuchsia_settings::Autorepeat {
                    delay: src.autorepeat_delay,
                    period: src.autorepeat_period,
                })
            },
            ..KeyboardSettings::EMPTY
        }
    }
}
