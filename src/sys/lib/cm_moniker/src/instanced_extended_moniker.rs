// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::instanced_abs_moniker::InstancedAbsoluteMoniker,
    core::cmp::Ord,
    moniker::{AbsoluteMonikerBase, MonikerError},
    std::fmt,
};

/// One of:
/// - An instanced absolute moniker
/// - A marker representing component manager's realm
#[derive(Eq, Ord, PartialOrd, PartialEq, Debug, Clone, Hash)]
pub enum InstancedExtendedMoniker {
    ComponentInstance(InstancedAbsoluteMoniker),
    ComponentManager,
}

/// The string representation of InstancedExtendedMoniker::ComponentManager
const EXTENDED_MONIKER_COMPONENT_MANAGER_STR: &'static str = "<component_manager>";

impl InstancedExtendedMoniker {
    pub fn parse_str(rep: &str) -> Result<Self, MonikerError> {
        if rep == EXTENDED_MONIKER_COMPONENT_MANAGER_STR {
            Ok(InstancedExtendedMoniker::ComponentManager)
        } else {
            Ok(InstancedExtendedMoniker::ComponentInstance(InstancedAbsoluteMoniker::parse_str(
                rep,
            )?))
        }
    }

    pub fn unwrap_instance_moniker_or<E: std::error::Error>(
        &self,
        error: E,
    ) -> Result<&InstancedAbsoluteMoniker, E> {
        match self {
            Self::ComponentManager => Err(error),
            Self::ComponentInstance(moniker) => Ok(moniker),
        }
    }

    pub fn contains_in_realm(&self, other: &InstancedExtendedMoniker) -> bool {
        match (self, other) {
            (Self::ComponentManager, _) => true,
            (Self::ComponentInstance(_), Self::ComponentManager) => false,
            (Self::ComponentInstance(a), Self::ComponentInstance(b)) => a.contains_in_realm(b),
        }
    }
}

impl fmt::Display for InstancedExtendedMoniker {
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

impl From<InstancedAbsoluteMoniker> for InstancedExtendedMoniker {
    fn from(m: InstancedAbsoluteMoniker) -> Self {
        Self::ComponentInstance(m)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn instanced_extended_monikers_parse() {
        assert_eq!(
            InstancedExtendedMoniker::parse_str(EXTENDED_MONIKER_COMPONENT_MANAGER_STR).unwrap(),
            InstancedExtendedMoniker::ComponentManager
        );
        assert_eq!(
            InstancedExtendedMoniker::parse_str("/foo:0/bar:0").unwrap(),
            InstancedExtendedMoniker::ComponentInstance(
                InstancedAbsoluteMoniker::parse_str("/foo:0/bar:0").unwrap()
            )
        );
        assert!(InstancedExtendedMoniker::parse_str("").is_err(), "cannot be empty");
        assert!(InstancedExtendedMoniker::parse_str("foo:0/bar:0").is_err(), "must start with /");
    }

    #[test]
    fn to_string_functions() {
        let cm_moniker =
            InstancedExtendedMoniker::parse_str(EXTENDED_MONIKER_COMPONENT_MANAGER_STR).unwrap();
        let foobar_moniker = InstancedExtendedMoniker::parse_str("/foo:0/bar:0").unwrap();
        let empty_moniker = InstancedExtendedMoniker::parse_str("/").unwrap();

        assert_eq!(format!("{}", cm_moniker), EXTENDED_MONIKER_COMPONENT_MANAGER_STR.to_string());
        assert_eq!(cm_moniker.to_string(), EXTENDED_MONIKER_COMPONENT_MANAGER_STR.to_string());
        assert_eq!(format!("{}", foobar_moniker), "/foo:0/bar:0".to_string());
        assert_eq!(format!("{}", empty_moniker), "/".to_string());
    }
}
