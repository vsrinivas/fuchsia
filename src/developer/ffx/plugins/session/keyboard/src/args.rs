// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command, fidl_fuchsia_input_keymap as fkeymap};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "keyboard", description = "Modify keyboard settings")]
pub struct Command {
    #[argh(
        option,
        from_str_fn(to_keymap_id),
        description = "the keymap to use, one of US_QWERTY,FR_AZERTY"
    )]
    pub keymap: fkeymap::Id,
}

fn to_keymap_id(value: &str) -> Result<fkeymap::Id, String> {
    match value {
        "US_QWERTY" => Ok(fkeymap::Id::UsQwerty),
        "FR_AZERTY" => Ok(fkeymap::Id::FrAzerty),
        _ => Err(format!("keymap identifier not recognized: {}", value)),
    }
}
