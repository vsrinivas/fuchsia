// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {crate::DEFAULT_TUF_METADATA_DEADLINE, argh::FromArgs, std::time::Duration};

#[derive(Debug, Eq, FromArgs, PartialEq)]
/// Arguments for the package resolver.
pub struct Args {
    #[argh(option, default = "false")]
    /// if true, allow resolving packages using `fuchsia.pkg/LocalMirror`.
    pub allow_local_mirror: bool,

    #[argh(
        option,
        default = "DEFAULT_TUF_METADATA_DEADLINE",
        from_str_fn(parse_tuf_metadata_deadline_seconds)
    )]
    /// the deadline, in seconds, to use when performing TUF metadata operations.
    /// The default is 240 seconds. This flag exists for integration testing and
    /// should not be used elsewhere.
    pub tuf_metadata_deadline_seconds: Duration,
}

fn parse_tuf_metadata_deadline_seconds(flag: &str) -> Result<Duration, String> {
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
    fn parse_tuf_metadata_deadline_seconds_success() {
        assert_matches!(
            Args::from_args(&["pkg-resolver"], &["--tuf-metadata-deadline-seconds", "23"]),
            Ok(Args { tuf_metadata_deadline_seconds, .. })
                if tuf_metadata_deadline_seconds.as_secs() == 23
        );
    }

    #[test]
    fn parse_tuf_metadata_deadline_seconds_failure() {
        assert_matches!(
            Args::from_args(
                &["pkg-resolver"],
                &["--tuf-metadata-deadline-seconds", "not-an-integer"]
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
                tuf_metadata_deadline_seconds: DEFAULT_TUF_METADATA_DEADLINE,
            })
        );
    }
}
