// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command, fidl_fuchsia_input as input};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "keyboard", description = "Modify keyboard settings")]
pub struct Command {
    #[argh(
        option,
        from_str_fn(to_keymap_id),
        description = "the keymap to use, one of US_QWERTY,FR_AZERTY,US_DVORAK"
    )]
    pub keymap: input::KeymapId,
}

fn to_keymap_id(value: &str) -> Result<input::KeymapId, String> {
    match value {
        "US_QWERTY" => Ok(input::KeymapId::UsQwerty),
        "FR_AZERTY" => Ok(input::KeymapId::FrAzerty),
        "US_DVORAK" => Ok(input::KeymapId::UsDvorak),
        _ => Err(format!("keymap identifier not recognized: {}", value)),
    }
}
