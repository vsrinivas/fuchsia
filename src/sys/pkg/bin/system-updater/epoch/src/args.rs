// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, std::path::Path};

#[derive(FromArgs)]
#[cfg_attr(test, derive(Debug, PartialEq))]
/// Program arguments.
pub struct Args {
    /// the path to the history file to read from.
    #[argh(option)]
    history: String,

    /// the path to write to.
    #[argh(option)]
    output: String,
}

impl Args {
    pub fn history(&self) -> &Path {
        Path::new(&self.history)
    }

    pub fn output(&self) -> &Path {
        Path::new(&self.output)
    }
}

#[cfg(test)]
mod tests {
    use {super::*, assert_matches::assert_matches};

    #[test]
    fn fail_unknown_option() {
        assert_matches!(Args::from_args(&["generate"], &["--unknown"]), Err(_));
    }

    #[test]
    fn fail_unknown_subcommand() {
        assert_matches!(Args::from_args(&["generate"], &["unknown"]), Err(_));
    }

    #[test]
    fn fail_missing_output_dir() {
        assert_matches!(Args::from_args(&["generate"], &["--history", "history_path"]), Err(_));
    }

    #[test]
    fn fail_missing_history() {
        assert_matches!(Args::from_args(&["generate"], &["--output", "output_path"]), Err(_));
    }

    #[test]
    fn success() {
        let args = Args::from_args(
            &["generate"],
            &["--history", "history_path", "--output", "output_path"],
        )
        .unwrap();
        assert_eq!(
            args,
            Args { history: "history_path".to_owned(), output: "output_path".to_owned() }
        );
    }
}
