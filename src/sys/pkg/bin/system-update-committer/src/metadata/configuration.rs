// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_paver as paver;

/// Wrapper for only configurations we support in verification and commits.
#[derive(Debug, Clone, PartialEq)]
pub enum Configuration {
    A,
    B,
}

impl From<&Configuration> for paver::Configuration {
    fn from(config: &Configuration) -> Self {
        match *config {
            Configuration::A => Self::A,
            Configuration::B => Self::B,
        }
    }
}

impl Configuration {
    pub fn to_alternate(&self) -> &Self {
        match *self {
            Self::A => &Self::B,
            Self::B => &Self::A,
        }
    }
}

#[cfg(test)]
impl From<&paver::Configuration> for Configuration {
    fn from(config: &paver::Configuration) -> Self {
        match *config {
            paver::Configuration::A => Self::A,
            paver::Configuration::B => Self::B,
            other => panic!("unsupported config: {:?}", other),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn to_alternate() {
        assert_eq!(Configuration::A.to_alternate(), &Configuration::B);
        assert_eq!(Configuration::B.to_alternate(), &Configuration::A);
    }

    #[test]
    fn fidl_conversion() {
        assert_eq!(paver::Configuration::from(&Configuration::A), paver::Configuration::A);
        assert_eq!(paver::Configuration::from(&Configuration::B), paver::Configuration::B);
    }
}
