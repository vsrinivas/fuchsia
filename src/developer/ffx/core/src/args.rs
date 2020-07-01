// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "list", description = "list connected devices")]
pub struct ListCommand {
    #[argh(positional)]
    pub nodename: Option<String>,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_list() {
        fn check(args: &[&str], nodename: String) {
            assert_eq!(
                ListCommand::from_args(&["list"], args),
                Ok(ListCommand { nodename: Some(nodename) })
            )
        }

        let nodename = String::from("thumb-set-human-neon");
        check(&[&nodename], nodename.clone());
    }
}
