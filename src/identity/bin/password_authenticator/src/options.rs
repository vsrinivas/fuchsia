// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, Error};
use std::fmt::Debug;

/// command line arguments supplied to Password Authenticator.
#[derive(argh::FromArgs, Debug)]
pub struct Options {
    /// whether to allow provisioning and getting accounts with an empty password and a null
    /// encryption key.
    #[argh(switch)]
    pub allow_null: bool,
    /// whether to allow getting accounts with a non-empty password and an scrypt-based encryption
    /// key. Accounts will be provisioned with an scrypt-based encryption key if the password is not
    /// empty, allow_scrypt=true, and allow_pinweaver=false.
    #[argh(switch)]
    pub allow_scrypt: bool,
    /// whether to allow provisioning and getting accounts with a non-empty password and a
    /// pinweaver-based encryption key.
    #[argh(switch)]
    pub allow_pinweaver: bool,
}

impl Options {
    /// Ensures that a set of options are self consistent.
    pub fn validate(&self) -> Result<(), Error> {
        // At least one type of account must be supported.
        if !(self.allow_null || self.allow_scrypt || self.allow_pinweaver) {
            Err(anyhow!("Command line options did not allow any types of account"))
        } else if self.allow_pinweaver {
            // TODO(zarvox): Remove this check when pinweaver is supported.
            Err(anyhow!("Pinweaver is not yet supported"))
        } else {
            Ok(())
        }
    }
}

#[cfg(test)]
pub mod test {
    use super::*;

    #[fuchsia::test]
    fn test_validate() {
        // Not allowing any types is invalid
        assert!(Options { allow_null: false, allow_scrypt: false, allow_pinweaver: false }
            .validate()
            .is_err());
        // Allowing a single type is valid
        assert!(Options { allow_null: false, allow_scrypt: true, allow_pinweaver: false }
            .validate()
            .is_ok());
        // Today, allowing pinweaver is invalid
        assert!(Options { allow_null: false, allow_scrypt: false, allow_pinweaver: true }
            .validate()
            .is_err());
    }
}
