// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        abs_moniker::{AbsoluteMoniker, AbsoluteMonikerBase},
        error::MonikerError,
    },
    core::cmp::Ord,
    std::fmt,
};

#[cfg(feature = "serde")]
use serde::{Deserialize, Serialize};

/// One of:
/// - An absolute moniker
/// - A marker representing component manager's realm
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize), serde(rename_all = "snake_case"))]
#[derive(Eq, Ord, PartialOrd, PartialEq, Debug, Clone, Hash)]
pub enum ExtendedMoniker {
    ComponentInstance(AbsoluteMoniker),
    ComponentManager,
}

/// The string representation of ExtendedMoniker::ComponentManager
const EXTENDED_MONIKER_COMPONENT_MANAGER_STR: &'static str = "<component_manager>";

impl ExtendedMoniker {
    pub fn parse_string_without_instances(rep: &str) -> Result<Self, MonikerError> {
        if rep == EXTENDED_MONIKER_COMPONENT_MANAGER_STR {
            Ok(ExtendedMoniker::ComponentManager)
        } else {
            Ok(ExtendedMoniker::ComponentInstance(AbsoluteMoniker::parse_string_without_instances(
                rep,
            )?))
        }
    }

    pub fn unwrap_instance_moniker_or<E: std::error::Error>(
        &self,
        error: E,
    ) -> Result<&AbsoluteMoniker, E> {
        match self {
            Self::ComponentManager => Err(error),
            Self::ComponentInstance(moniker) => Ok(moniker),
        }
    }

    pub fn contains_in_realm(&self, other: &ExtendedMoniker) -> bool {
        match (self, other) {
            (Self::ComponentManager, _) => true,
            (Self::ComponentInstance(_), Self::ComponentManager) => false,
            (Self::ComponentInstance(a), Self::ComponentInstance(b)) => a.contains_in_realm(b),
        }
    }
}

impl fmt::Display for ExtendedMoniker {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::ComponentInstance(m) => {
                write!(f, "{}", m)?;
            }
            Self::ComponentManager => {
                write!(f, "{}", EXTENDED_MONIKER_COMPONENT_MANAGER_STR)?;
            }
        }
        Ok(())
    }
}

impl From<AbsoluteMoniker> for ExtendedMoniker {
    fn from(m: AbsoluteMoniker) -> Self {
        Self::ComponentInstance(m)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn extended_monikers_parse() {
        assert_eq!(
            ExtendedMoniker::parse_string_without_instances(EXTENDED_MONIKER_COMPONENT_MANAGER_STR)
                .unwrap(),
            ExtendedMoniker::ComponentManager
        );
        assert_eq!(
            ExtendedMoniker::parse_string_without_instances("/foo/bar").unwrap(),
            ExtendedMoniker::ComponentInstance(
                AbsoluteMoniker::parse_string_without_instances("/foo/bar").unwrap()
            )
        );
        assert!(ExtendedMoniker::parse_string_without_instances("").is_err(), "cannot be empty");
    }
}
