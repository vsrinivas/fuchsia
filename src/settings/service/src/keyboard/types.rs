// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use serde::{Deserialize, Serialize};
use std::convert::TryFrom;

#[derive(PartialEq, Debug, Clone, Copy, Serialize, Deserialize)]
pub struct KeyboardInfo {
    pub(crate) keymap: Option<KeymapId>,
    pub(crate) autorepeat: Option<Autorepeat>,
}

impl KeyboardInfo {
    pub(crate) fn is_valid(&self) -> bool {
        self.autorepeat.map_or(true, |x| x.is_valid())
    }
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

    /// The US Colemak keymap.
    UsColemak,
}

impl TryFrom<fidl_fuchsia_input::KeymapId> for KeymapId {
    type Error = String;

    fn try_from(src: fidl_fuchsia_input::KeymapId) -> Result<Self, Self::Error> {
        match src {
            fidl_fuchsia_input::KeymapId::UsQwerty => Ok(KeymapId::UsQwerty),
            fidl_fuchsia_input::KeymapId::FrAzerty => Ok(KeymapId::FrAzerty),
            fidl_fuchsia_input::KeymapId::UsDvorak => Ok(KeymapId::UsDvorak),
            fidl_fuchsia_input::KeymapId::UsColemak => Ok(KeymapId::UsColemak),
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
            KeymapId::UsColemak => fidl_fuchsia_input::KeymapId::UsColemak,
        }
    }
}

#[derive(PartialEq, Debug, Clone, Copy, Serialize, Deserialize)]
pub(crate) struct Autorepeat {
    /// The delay between key actuation and autorepeat actuation. Meaningful values are positive
    /// integers. Zero means the field has been cleared.
    pub delay: i64,
    /// The period between two successive autorepeat actuations (1/rate). Meaningful values are
    /// positive integers. Zero means the field has been cleared.
    pub period: i64,
}

impl Autorepeat {
    pub(crate) fn is_valid(&self) -> bool {
        if self.delay >= 0 && self.period >= 0 {
            true
        } else {
            false
        }
    }
}

impl From<fidl_fuchsia_settings::Autorepeat> for Autorepeat {
    fn from(src: fidl_fuchsia_settings::Autorepeat) -> Self {
        Autorepeat { delay: src.delay, period: src.period }
    }
}

impl From<Autorepeat> for fidl_fuchsia_settings::Autorepeat {
    fn from(src: Autorepeat) -> Self {
        fidl_fuchsia_settings::Autorepeat { delay: src.delay, period: src.period }
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
