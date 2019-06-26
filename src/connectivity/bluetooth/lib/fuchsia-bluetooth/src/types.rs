// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(warnings)]
use {fidl_fuchsia_bluetooth, std::fmt};

pub use {adapter_info::*, bonding_data::*, remote_device::*};
mod adapter_info;
mod bonding_data;
/// Bluetooth LowEnergy types
pub mod le;
mod remote_device;

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
    fn fmt(&self, fmt: &mut fmt::Formatter) -> fmt::Result {
        write!(fmt, "{}", self.0.value)
    }
}

impl fmt::Display for Status {
    fn fmt(&self, fmt: &mut fmt::Formatter) -> fmt::Result {
        write!(fmt, "{:?}", self.0.error)
    }
}

impl fmt::Debug for DeviceClass {
    fn fmt(&self, fmt: &mut fmt::Formatter) -> fmt::Result {
        write!(fmt, "{:?}", self.0)
    }
}
