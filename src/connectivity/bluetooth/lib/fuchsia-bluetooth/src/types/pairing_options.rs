// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    fidl_fuchsia_bluetooth as bt, fidl_fuchsia_bluetooth_control as control,
    fidl_fuchsia_bluetooth_sys as sys,
};

use crate::types::Technology;

/// Configuration Options for a Pairing Request
#[derive(Clone, Debug, PartialEq)]
pub struct PairingOptions {
    pub le_security_level: SecurityLevel,
    pub bondable: BondableMode,
    pub transport: Technology,
}

/// The security level required for this pairing - corresponds to the security
/// levels defined in the Security Manager Protocol in Vol 3, Part H, Section 2.3.1
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum SecurityLevel {
    /// Encrypted without MITM protection (unauthenticated)
    Encrypted,
    /// Encrypted with MITM protection (authenticated), although this level of security does not
    /// fully protect against passive eavesdroppers
    Authenticated,
}

/// Bondable Mode - whether to accept bonding initiated by a remote peer
/// As described in Core Spec v5.2 | Vol 3, Part C, Sec 4.3
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum BondableMode {
    Bondable,
    NonBondable,
}

impl From<sys::PairingSecurityLevel> for SecurityLevel {
    fn from(level: sys::PairingSecurityLevel) -> Self {
        match level {
            sys::PairingSecurityLevel::Encrypted => SecurityLevel::Encrypted,
            sys::PairingSecurityLevel::Authenticated => SecurityLevel::Authenticated,
        }
    }
}

impl From<SecurityLevel> for sys::PairingSecurityLevel {
    fn from(level: SecurityLevel) -> Self {
        match level {
            SecurityLevel::Encrypted => sys::PairingSecurityLevel::Encrypted,
            SecurityLevel::Authenticated => sys::PairingSecurityLevel::Authenticated,
        }
    }
}

// TODO(fxbug.dev/48051) - remove once fuchsia.bluetooth.control is retired
impl From<control::PairingSecurityLevel> for SecurityLevel {
    fn from(level: control::PairingSecurityLevel) -> Self {
        match level {
            control::PairingSecurityLevel::Encrypted => SecurityLevel::Encrypted,
            control::PairingSecurityLevel::Authenticated => SecurityLevel::Authenticated,
        }
    }
}
// TODO(fxbug.dev/48051) - remove once fuchsia.bluetooth.control is retired
impl From<SecurityLevel> for control::PairingSecurityLevel {
    fn from(level: SecurityLevel) -> Self {
        match level {
            SecurityLevel::Encrypted => control::PairingSecurityLevel::Encrypted,
            SecurityLevel::Authenticated => control::PairingSecurityLevel::Authenticated,
        }
    }
}

impl From<sys::PairingOptions> for PairingOptions {
    fn from(opts: sys::PairingOptions) -> Self {
        (&opts).into()
    }
}

impl From<&sys::PairingOptions> for PairingOptions {
    fn from(opts: &sys::PairingOptions) -> Self {
        let bondable = match opts.bondable_mode {
            Some(sys::BondableMode::NonBondable) => BondableMode::NonBondable,
            Some(sys::BondableMode::Bondable) | None => BondableMode::Bondable,
        };
        let le_security_level =
            opts.le_security_level.map_or(SecurityLevel::Encrypted, SecurityLevel::from);
        let transport = opts.transport.map_or(Technology::DualMode, Technology::from);
        PairingOptions { le_security_level, bondable, transport }
    }
}

impl From<&PairingOptions> for sys::PairingOptions {
    fn from(opts: &PairingOptions) -> Self {
        let bondable_mode = match opts.bondable {
            BondableMode::NonBondable => Some(sys::BondableMode::NonBondable),
            BondableMode::Bondable => Some(sys::BondableMode::Bondable),
        };
        let le_security_level = Some(opts.le_security_level.into());
        let transport = Some(opts.transport.into());
        sys::PairingOptions { le_security_level, bondable_mode, transport }
    }
}

impl From<PairingOptions> for sys::PairingOptions {
    fn from(opts: PairingOptions) -> Self {
        (&opts).into()
    }
}

// TODO(fxbug.dev/48051) - remove once fuchsia.bluetooth.control is retired
impl From<control::PairingOptions> for PairingOptions {
    fn from(opts: control::PairingOptions) -> Self {
        (&opts).into()
    }
}
// TODO(fxbug.dev/48051) - remove once fuchsia.bluetooth.control is retired
impl From<&control::PairingOptions> for PairingOptions {
    fn from(opts: &control::PairingOptions) -> Self {
        let bondable = match opts.non_bondable {
            Some(true) => BondableMode::NonBondable,
            Some(false) => BondableMode::Bondable,
            None => BondableMode::Bondable,
        };
        let le_security_level =
            opts.le_security_level.map_or(SecurityLevel::Encrypted, SecurityLevel::from);
        let transport = opts.transport.map_or(Technology::DualMode, Technology::from);
        PairingOptions { le_security_level, bondable, transport }
    }
}

// TODO(fxbug.dev/48051) - remove once fuchsia.bluetooth.control is retired
impl From<PairingOptions> for control::PairingOptions {
    fn from(opts: PairingOptions) -> Self {
        let non_bondable = match opts.bondable {
            BondableMode::NonBondable => Some(true),
            BondableMode::Bondable => Some(false),
        };
        let le_security_level = Some(opts.le_security_level.into());
        let transport = Some(opts.transport.into());
        control::PairingOptions { le_security_level, non_bondable, transport }
    }
}

#[cfg(test)]
pub mod tests {
    use super::*;
    use proptest::prelude::*;

    fn any_technology() -> impl Strategy<Value = Technology> {
        prop_oneof![Just(Technology::LE), Just(Technology::Classic), Just(Technology::DualMode)]
    }
    fn any_bondable() -> impl Strategy<Value = BondableMode> {
        prop_oneof![Just(BondableMode::Bondable), Just(BondableMode::NonBondable)]
    }
    fn any_security() -> impl Strategy<Value = SecurityLevel> {
        prop_oneof![Just(SecurityLevel::Encrypted), Just(SecurityLevel::Authenticated)]
    }

    prop_compose! {
        fn any_pairing_options()(
            le_security_level in any_security(),
            bondable in  any_bondable(),
            transport in any_technology()) -> PairingOptions {
            PairingOptions{ le_security_level, bondable, transport }
        }
    }

    proptest! {
        #[test]
        fn roundtrip(opts in any_pairing_options()) {
            let sys: sys::PairingOptions = (&opts).into();
            assert_eq!(opts, sys.into());
        }
    }
}
