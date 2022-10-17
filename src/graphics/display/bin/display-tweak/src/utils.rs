// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, Error};
use fidl;
use fuchsia_zircon as zx;

/// Unwraps the result of a FIDL call that errors out with a zx::result into a
/// Result<T, E>.
pub fn flatten_zx_error<T>(
    fidl_result: Result<Result<T, zx::zx_status_t>, fidl::Error>,
) -> Result<T, Error> {
    fidl_result?
        .map_err(|zx_status| anyhow!("Server response: {}", zx::Status::from_raw(zx_status)))
}

/// Unwraps the result of a FIDL call that returns a zx::result into a
/// Result<T, E>. If the result is an error, it has `context` attached to it.
pub fn flatten_zx_status(fidl_result: Result<zx::zx_status_t, fidl::Error>) -> Result<(), Error> {
    // Make the return type look like the FIDL call errors with zx.status.
    let zx_error_result =
        fidl_result
            .map(|zx_status| if zx_status == zx::sys::ZX_OK { Ok(()) } else { Err(zx_status) });
    flatten_zx_error(zx_error_result)
}

/// Helper for accepting boolean values as "off" / "on" strings.
pub fn on_off_to_bool(value: &str) -> Result<bool, String> {
    match value {
        "off" => Ok(false),
        "on" => Ok(true),
        _ => Err(String::from("Unrecognized value. Possible values are \"on\" and \"off\".")),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use assert_matches::assert_matches;

    #[fuchsia::test]
    fn flatten_zx_error_with_success() {
        let fidl_result: Result<Result<i32, zx::zx_status_t>, fidl::Error> = Ok(Ok(42));
        assert_matches!(flatten_zx_error(fidl_result), Ok(42));
    }

    #[fuchsia::test]
    fn flatten_zx_error_with_zx_error() {
        let fidl_result: Result<Result<i32, zx::zx_status_t>, fidl::Error> =
            Ok(Err(zx::sys::ZX_ERR_NOT_SUPPORTED));
        let result: Result<i32, Error> = flatten_zx_error(fidl_result);

        assert_matches!(result, Err(_));
        let result_error: Error = result.unwrap_err();
        assert_eq!(result_error.to_string(), "Server response: NOT_SUPPORTED");
    }

    #[fuchsia::test]
    fn flatten_zx_error_with_fidl_error() {
        let fidl_error = fidl::Error::ClientChannelClosed {
            status: zx::Status::PEER_CLOSED,
            protocol_name: "TestService",
        };
        let fidl_result: Result<Result<i32, zx::zx_status_t>, fidl::Error> =
            Err(fidl_error.clone());
        let result: Result<i32, Error> = flatten_zx_error(fidl_result);

        assert_matches!(result, Err(_));
        let result_error: Error = result.unwrap_err();
        assert_eq!(result_error.to_string(), format!("{}", fidl_error));
    }

    #[fuchsia::test]
    fn flatten_zx_status_success() {
        let fidl_result: Result<zx::zx_status_t, fidl::Error> = Ok(zx::sys::ZX_OK);
        assert_matches!(flatten_zx_status(fidl_result), Ok(()));
    }

    #[fuchsia::test]
    fn flatten_zx_status_annotates_zx_error() {
        let fidl_result: Result<zx::zx_status_t, fidl::Error> = Ok(zx::sys::ZX_ERR_NOT_SUPPORTED);
        let result: Result<(), Error> = flatten_zx_status(fidl_result);

        assert_matches!(result, Err(_));
        let result_error: Error = result.unwrap_err();
        assert_eq!(result_error.to_string(), "Server response: NOT_SUPPORTED");
    }

    #[fuchsia::test]
    fn flatten_zx_status_annotates_fidl_error() {
        let fidl_error = fidl::Error::ClientChannelClosed {
            status: zx::Status::PEER_CLOSED,
            protocol_name: "TestService",
        };
        let fidl_result: Result<zx::zx_status_t, fidl::Error> = Err(fidl_error.clone());
        let result: Result<(), Error> = flatten_zx_status(fidl_result);

        assert_matches!(result, Err(_));
        let result_error: Error = result.unwrap_err();
        assert_eq!(result_error.to_string(), format!("{}", fidl_error));
    }
}
