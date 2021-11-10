// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::utils::{self, Either, WatchOrSetResult};
use fidl_fuchsia_settings::{KeyboardProxy, KeyboardSettings};

pub async fn command(
    proxy: KeyboardProxy,
    keymap: Option<fidl_fuchsia_input::KeymapId>,
) -> WatchOrSetResult {
    if let Some(id) = keymap {
        let mut settings = KeyboardSettings::EMPTY;
        settings.keymap = Some(id);

        let mutate_result = proxy.set(settings).await?;
        Ok(Either::Set(match mutate_result {
            Ok(_) => format!("Successfully set keymap ID to {:?}", id),
            Err(err) => format!("{:?}", err),
        }))
    } else {
        Ok(Either::Watch(utils::watch_to_stream(proxy, |p| p.watch())))
    }
}
