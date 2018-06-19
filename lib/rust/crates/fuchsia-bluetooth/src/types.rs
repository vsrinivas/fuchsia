// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_bluetooth;
use std::fmt;

macro_rules! bt_fidl_wrap {
    ($x:ident) => {
        /// Wrapper for mapping fidl_fuchsia_bluetooth::$x to fuchsia_bluetooth::$x
        pub struct $x(fidl_fuchsia_bluetooth::$x);

        impl From<fidl_fuchsia_bluetooth::$x> for $x {
            fn from(b: fidl_fuchsia_bluetooth::$x) -> $x {
                $x(b)
            }
        }
        impl Into<fidl_fuchsia_bluetooth::$x> for $x {
            fn into(self) -> fidl_fuchsia_bluetooth::$x {
                self.0
            }
        }
    };
}

bt_fidl_wrap!(Status);
bt_fidl_wrap!(Bool);
bt_fidl_wrap!(Int8);
bt_fidl_wrap!(UInt16);

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
