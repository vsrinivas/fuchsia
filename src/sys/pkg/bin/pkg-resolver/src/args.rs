// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        DEFAULT_BLOB_NETWORK_BODY_TIMEOUT, DEFAULT_BLOB_NETWORK_HEADER_TIMEOUT,
        DEFAULT_TUF_METADATA_TIMEOUT,
    },
    argh::FromArgs,
    std::time::Duration,
};

#[derive(Debug, Eq, FromArgs, PartialEq)]
/// Arguments for the package resolver.
pub struct Args {
    #[argh(option, default = "false")]
    /// if true, allow resolving packages using `fuchsia.pkg/LocalMirror`.
    pub allow_local_mirror: bool,

    #[argh(
        option,
        default = "DEFAULT_TUF_METADATA_TIMEOUT",
        from_str_fn(parse_unsigned_integer_as_seconds),
        long = "tuf-metadata-timeout-seconds"
    )]
    /// the timeout, in seconds, to use when performing TUF metadata operations.
    /// The default is 240 seconds. This flag exists for integration testing and
    /// should not be used elsewhere.
    pub tuf_metadata_timeout: Duration,

    #[argh(
        option,
        default = "DEFAULT_BLOB_NETWORK_HEADER_TIMEOUT",
        from_str_fn(parse_unsigned_integer_as_seconds),
        long = "blob-network-header-timeout-seconds"
    )]
    /// the timeout, in seconds, to use when GET'ing a blob http header.
    /// The default is 30 seconds. This flag exists for integration testing and
    /// should not be used elsewhere.
    pub blob_network_header_timeout: Duration,

    #[argh(
        option,
        default = "DEFAULT_BLOB_NETWORK_BODY_TIMEOUT",
        from_str_fn(parse_unsigned_integer_as_seconds),
        long = "blob-network-body-timeout-seconds"
    )]
    /// the timeout, in seconds, to use when waiting for a blob's http body bytes.
    /// The default is 30 seconds. This flag exists for integration testing and
    /// should not be used elsewhere.
    pub blob_network_body_timeout: Duration,
}

fn parse_unsigned_integer_as_seconds(flag: &str) -> Result<Duration, String> {
    if let Ok(duration) = flag.parse::<u64>() {
        Ok(Duration::from_secs(duration))
    } else {
        Err(String::from("value should be an unsigned integer"))
    }
}

#[cfg(test)]
mod tests {
    use {super::*, matches::assert_matches};

    #[test]
    fn local_mirror_allowed() {
        assert_matches!(
            Args::from_args(&["pkg-resolver"], &["--allow-local-mirror", "true"]),
            Ok(Args { allow_local_mirror: true, .. })
        );
    }

    #[test]
    fn local_mirror_disallowed() {
        assert_matches!(
            Args::from_args(&["pkg-resolver"], &["--allow-local-mirror", "false"]),
            Ok(Args { allow_local_mirror: false, .. })
        );
    }

    #[test]
    fn tuf_metadata_timeout_seconds_success() {
        assert_matches!(
            Args::from_args(&["pkg-resolver"], &["--tuf-metadata-timeout-seconds", "23"]),
            Ok(Args { tuf_metadata_timeout, .. })
                if tuf_metadata_timeout == Duration::from_secs(23)
        );
    }

    #[test]
    fn tuf_metadata_timeout_seconds_failure() {
        assert_matches!(
            Args::from_args(
                &["pkg-resolver"],
                &["--tuf-metadata-timeout-seconds", "not-an-integer"]
            ),
            Err(_)
        );
    }

    #[test]
    fn blob_network_header_timeout_seconds_success() {
        assert_matches!(
            Args::from_args(&["pkg-resolver"], &["--blob-network-header-timeout-seconds", "24"]),
            Ok(Args { blob_network_header_timeout, .. })
                if blob_network_header_timeout == Duration::from_secs(24)
        );
    }

    #[test]
    fn blob_network_header_timeout_seconds_failure() {
        assert_matches!(
            Args::from_args(
                &["pkg-resolver"],
                &["--blob-network-header-timeout-seconds", "also-not-an-integer"]
            ),
            Err(_)
        );
    }

    #[test]
    fn blob_network_body_timeout_seconds_success() {
        assert_matches!(
            Args::from_args(&["pkg-resolver"], &["--blob-network-body-timeout-seconds", "25"]),
            Ok(Args { blob_network_body_timeout, .. })
                if blob_network_body_timeout == Duration::from_secs(25)
        );
    }

    #[test]
    fn blob_network_body_timeout_seconds_failure() {
        assert_matches!(
            Args::from_args(
                &["pkg-resolver"],
                &["--blob-network-body-timeout-seconds", "also-not-an-integer-too"]
            ),
            Err(_)
        );
    }

    #[test]
    fn default() {
        assert_eq!(
            Args::from_args(&["pkg-resolver"], &[]),
            Ok(Args {
                allow_local_mirror: false,
                tuf_metadata_timeout: DEFAULT_TUF_METADATA_TIMEOUT,
                blob_network_body_timeout: DEFAULT_BLOB_NETWORK_BODY_TIMEOUT,
                blob_network_header_timeout: DEFAULT_BLOB_NETWORK_HEADER_TIMEOUT,
            })
        );
    }
}
