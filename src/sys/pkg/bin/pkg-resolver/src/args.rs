// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;

#[derive(Debug, Eq, FromArgs, PartialEq)]
/// Arguments for the package resolver.
pub struct Args {
    #[argh(option, default = "false")]
    /// if true, allow resolving packages using `fuchsia.pkg/LocalMirror`.
    pub allow_local_mirror: bool,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_local_mirror_allowed() {
        assert_eq!(
            Args::from_args(&["pkg-resolver"], &["--allow-local-mirror", "true"]),
            Ok(Args { allow_local_mirror: true })
        );
    }

    #[test]
    fn test_local_mirror_disallowed() {
        assert_eq!(
            Args::from_args(&["pkg-resolver"], &["--allow-local-mirror", "false"]),
            Ok(Args { allow_local_mirror: false })
        );
    }

    #[test]
    fn test_default() {
        assert_eq!(Args::from_args(&["pkg-resolver"], &[]), Ok(Args { allow_local_mirror: false }));
    }
}
