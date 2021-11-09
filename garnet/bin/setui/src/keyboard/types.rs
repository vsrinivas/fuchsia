// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use serde::{Deserialize, Serialize};
use std::convert::TryFrom;

#[derive(PartialEq, Debug, Clone, Copy, Serialize, Deserialize)]
pub struct KeyboardInfo {
    pub(crate) keymap: Option<KeymapId>,
}

#[derive(Copy, Clone, Debug, Eq, PartialEq, Ord, PartialOrd, Hash, Serialize, Deserialize)]
pub(crate) enum KeymapId {
    /// The US_QWERTY keymap. This is also the default if no settings are
    /// ever applied.
    UsQwerty,

    /// The FR AZERTY keymap.
    FrAzerty,

    /// The US Dvorak keymap.
    UsDvorak,
}

impl TryFrom<fidl_fuchsia_input::KeymapId> for KeymapId {
    type Error = String;

    fn try_from(src: fidl_fuchsia_input::KeymapId) -> Result<Self, Self::Error> {
        match src {
            fidl_fuchsia_input::KeymapId::UsQwerty => Ok(KeymapId::UsQwerty),
            fidl_fuchsia_input::KeymapId::FrAzerty => Ok(KeymapId::FrAzerty),
            fidl_fuchsia_input::KeymapId::UsDvorak => Ok(KeymapId::UsDvorak),
            _ => Err(format!("Received an invalid keymap id: {:?}.", src)),
        }
    }
}

impl From<KeymapId> for fidl_fuchsia_input::KeymapId {
    fn from(src: KeymapId) -> Self {
        match src {
            KeymapId::UsQwerty => fidl_fuchsia_input::KeymapId::UsQwerty,
            KeymapId::FrAzerty => fidl_fuchsia_input::KeymapId::FrAzerty,
            KeymapId::UsDvorak => fidl_fuchsia_input::KeymapId::UsDvorak,
        }
    }
}

#[cfg(test)]
mod tests {
    use crate::keyboard::types::KeymapId;
    use std::convert::TryFrom;

    #[test]
    fn test_try_from_keymapid() {
        assert!(KeymapId::try_from(fidl_fuchsia_input::KeymapId::UsQwerty).is_ok());

        assert!(KeymapId::try_from(fidl_fuchsia_input::KeymapId::unknown()).is_err());

        assert_eq!(
            KeymapId::try_from(fidl_fuchsia_input::KeymapId::FrAzerty).unwrap(),
            KeymapId::FrAzerty
        );
    }
}
