// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Error, Result},
    argh::FromArgs,
    ffx_core::ffx_command,
};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "list", description = "list connected devices")]
pub struct ListCommand {
    #[argh(positional)]
    pub nodename: Option<String>,

    #[argh(option, default = "Format::Tabular")]
    /// determines the output format for the targets. Default is the "tabular"
    /// format. Expects either "tabular" or "simple". Tabular format includes
    /// the most verbose information. Simple format includes the SSH address of
    /// a target and its nodename in two unlabeled columns.
    ///
    /// Accepted variations:
    /// -- "simple" or "s" for simple format.
    /// -- "tabular" or "table" or "tab" or "t" for tabular format.
    pub format: Format,
}

#[derive(Debug, PartialEq)]
pub enum Format {
    Tabular,
    Simple,
}

impl std::str::FromStr for Format {
    type Err = Error;

    fn from_str(s: &str) -> Result<Format> {
        match s {
            "tabular" | "table" | "tab" | "t" => Ok(Format::Tabular),
            "simple" | "s" => Ok(Format::Simple),
            _ => Err(anyhow!("expected 'tabular' or 'simple'")),
        }
    }
}
