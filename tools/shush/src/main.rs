// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, Error};
use argh::FromArgs;

use std::{
    env,
    fs::File,
    io::{self, BufRead, BufReader},
    path::PathBuf,
    process::Command,
};

mod allow;
mod fix;
mod lint;
mod owners;
mod span;

#[derive(Debug, FromArgs)]
/// Silence rustc and clippy lints with allow attributes and autofixes
struct Args {
    #[argh(subcommand)]
    action: Action,
    /// don't modify source files
    #[argh(switch)]
    dryrun: bool,
    /// modify files even if there are local uncommitted changes
    #[argh(switch)]
    force: bool,
    /// lint (or category) to deal with e.g. clippy::needless_return
    #[argh(option)]
    lint: Vec<String>,
    /// path to the root dir of the fuchsia source tree
    #[argh(option)]
    fuchsia_dir: Option<PathBuf>,
    /// file containing json lints (uses stdin if not given)
    #[argh(positional)]
    lint_file: Option<PathBuf>,
}

#[derive(Debug, FromArgs)]
#[argh(subcommand)]
enum Action {
    Fix(Fix),
    Allow(Allow),
}

/// use rustfix to auto-fix the lints
#[derive(FromArgs, Debug)]
#[argh(subcommand, name = "fix")]
struct Fix {}

/// add allow attributes
#[derive(FromArgs, Debug)]
#[argh(subcommand, name = "allow")]
struct Allow {
    /// emit markdown links rather than plaintext spans
    #[argh(switch)]
    markdown: bool,
}

fn main() -> Result<(), Error> {
    let args: Args = argh::from_env();
    if args.lint.is_empty() {
        return Err(anyhow!("Must filter on at least one lint or category with '--lint'"));
    }
    let mut reader: Box<dyn BufRead> = if let Some(f) = &args.lint_file {
        Box::new(BufReader::new(File::open(f).unwrap()))
    } else {
        Box::new(BufReader::new(io::stdin()))
    };
    let root = &args
        .fuchsia_dir
        .or_else(|| env::var("FUCHSIA_DIR").ok().map(Into::into))
        .map(|d| {
            env::set_current_dir(&d).expect("couldn't change dir");
            d
        })
        .unwrap_or_else(|| std::env::current_dir().unwrap().canonicalize().unwrap());
    let clean_tree = Command::new("jiri")
        .args(["runp", "-exit-on-error", "git", "diff-index", "--quiet", "HEAD"])
        .output()?
        .status
        .success();
    if !(args.dryrun || args.force || clean_tree) {
        return Err(anyhow!(
            "The current directory is dirty, pass the --force flag or commit the local changes"
        ));
    }

    match args.action {
        Action::Fix(_) => fix::fix(&mut reader, &args.lint, args.dryrun),
        Action::Allow(Allow { markdown }) => {
            allow::allow(&mut reader, &args.lint, &root, args.dryrun, markdown)
        }
    }
}
