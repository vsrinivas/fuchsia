// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl_fuchsia_bluetooth,
    std::{
        fs::{File, OpenOptions},
        path::Path,
    },
};

/// Macro to help build bluetooth fidl statuses.
/// No Args is a success
/// One Arg is the error type
/// Two Args is the error type & a description
#[macro_export]
macro_rules! bt_fidl_status {
    () => {
        fidl_fuchsia_bluetooth::Status { error: None }
    };

    ($error_code:ident) => {
        fidl_fuchsia_bluetooth::Status {
            error: Some(Box::new(fidl_fuchsia_bluetooth::Error {
                description: None,
                protocol_error_code: 0,
                error_code: fidl_fuchsia_bluetooth::ErrorCode::$error_code,
            })),
        }
    };

    ($error_code:ident, $description:expr) => {
        fidl_fuchsia_bluetooth::Status {
            error: Some(Box::new(fidl_fuchsia_bluetooth::Error {
                description: Some($description.to_string()),
                protocol_error_code: 0,
                error_code: fidl_fuchsia_bluetooth::ErrorCode::$error_code,
            })),
        }
    };
}

/// Open a file with read and write permissions.
pub fn open_rdwr<P: AsRef<Path>>(path: P) -> Result<File, Error> {
    OpenOptions::new().read(true).write(true).open(path).map_err(|e| e.into())
}

/// The following functions allow FIDL types to be cloned. These are currently necessary as the
/// auto-generated binding types do not derive `Clone`.

/// Clone Bluetooth Fidl bool type
pub fn clone_bt_fidl_bool(a: &fidl_fuchsia_bluetooth::Bool) -> fidl_fuchsia_bluetooth::Bool {
    fidl_fuchsia_bluetooth::Bool { value: a.value }
}
