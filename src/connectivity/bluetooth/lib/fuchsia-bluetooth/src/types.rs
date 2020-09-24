// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(warnings)]
use {
    fidl_fuchsia_bluetooth, fidl_fuchsia_bluetooth_control as control,
    fidl_fuchsia_bluetooth_sys as sys,
    std::{fmt, str::FromStr},
};

pub use {
    self::uuid::*, adapter_info::*, address::*, bonding_data::*, channel::*, host_info::*, id::*,
    peer::*,
};

mod adapter_info;
mod address;
/// Types related to bonding data. This module defines helper functions for unit tests that utilize
/// proptest.
pub mod bonding_data;
/// Channel type
mod channel;
/// Bluetooth HCI emulator protocol types
pub mod emulator;
pub mod host_info;
mod id;
pub mod io_capabilities;
/// Bluetooth LowEnergy types
pub mod le;
pub mod pairing_options;
mod peer;
mod uuid;

macro_rules! bt_fidl_wrap {
    ($x:ident) => {
        bt_fidl_wrap!($x, fidl_fuchsia_bluetooth::$x);
    };
    ($outer:ident, $inner:ty) => {
        /// Wrapper for mapping $inner to fuchsia_bluetooth::$outer
        pub struct $outer($inner);

        impl From<$inner> for $outer {
            fn from(b: $inner) -> $outer {
                $outer(b)
            }
        }

        impl Into<$inner> for $outer {
            fn into(self) -> $inner {
                self.0
            }
        }
    };
}

bt_fidl_wrap!(Status);
bt_fidl_wrap!(Bool);
bt_fidl_wrap!(Int8);
bt_fidl_wrap!(UInt16);
bt_fidl_wrap!(DeviceClass, fidl_fuchsia_bluetooth_control::DeviceClass);

impl fmt::Display for Bool {
    fn fmt(&self, fmt: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(fmt, "{}", self.0.value)
    }
}

impl fmt::Display for Status {
    fn fmt(&self, fmt: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(fmt, "{:?}", self.0.error)
    }
}

impl fmt::Debug for DeviceClass {
    fn fmt(&self, fmt: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(fmt, "{:?}", self.0)
    }
}

/// A struct indicating either A or B or Both, but not neither - at least one must be present
/// Useful when indicating support for Le or BrEdr, where dual mode is also supported but we
/// require at least one. This avoids extra error checking that would be required if two options
/// were used.
#[derive(Clone, Debug, PartialEq)]
pub enum OneOrBoth<L, R> {
    Left(L),
    Both(L, R),
    Right(R),
}

impl<L, R> OneOrBoth<L, R> {
    pub fn left(&self) -> Option<&L> {
        match &self {
            OneOrBoth::Left(l) => Some(l),
            OneOrBoth::Both(l, _) => Some(l),
            OneOrBoth::Right(_) => None,
        }
    }
    pub fn right(&self) -> Option<&R> {
        match &self {
            OneOrBoth::Left(_) => None,
            OneOrBoth::Both(_, r) => Some(r),
            OneOrBoth::Right(r) => Some(r),
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub enum Technology {
    LE,
    Classic,
    DualMode,
}

impl From<sys::TechnologyType> for Technology {
    fn from(tech: sys::TechnologyType) -> Self {
        match tech {
            sys::TechnologyType::LowEnergy => Technology::LE,
            sys::TechnologyType::Classic => Technology::Classic,
            sys::TechnologyType::DualMode => Technology::DualMode,
        }
    }
}

impl From<Technology> for sys::TechnologyType {
    fn from(tech: Technology) -> Self {
        match tech {
            Technology::LE => sys::TechnologyType::LowEnergy,
            Technology::Classic => sys::TechnologyType::Classic,
            Technology::DualMode => sys::TechnologyType::DualMode,
        }
    }
}

// TODO(fxbug.dev/48051) - remove once fuchsia.bluetooth.control is retired
impl From<control::TechnologyType> for Technology {
    fn from(tech: control::TechnologyType) -> Self {
        match tech {
            control::TechnologyType::LowEnergy => Technology::LE,
            control::TechnologyType::Classic => Technology::Classic,
            control::TechnologyType::DualMode => Technology::DualMode,
        }
    }
}

// TODO(fxbug.dev/48051) - remove once fuchsia.bluetooth.control is retired
impl From<Technology> for control::TechnologyType {
    fn from(tech: Technology) -> Self {
        match tech {
            Technology::LE => control::TechnologyType::LowEnergy,
            Technology::Classic => control::TechnologyType::Classic,
            Technology::DualMode => control::TechnologyType::DualMode,
        }
    }
}
