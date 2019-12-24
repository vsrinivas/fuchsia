// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{formatting::*, location::all_locations},
    anyhow::{format_err, Error},
    std::{env, str::FromStr},
};

#[derive(Clone, Debug)]
pub struct Options {
    pub mode: ModeCommand,
    pub global: GlobalOptions,
    pub formatting: FormattingOptions,
    pub recursive: bool,
    pub path: Vec<String>,
}

#[derive(Clone, Debug)]
pub struct GlobalOptions {
    pub dir: Option<String>,
}

#[derive(Clone, Debug)]
pub struct FormattingOptions {
    format: Format,
    sort: bool,
    path_format: PathFormat,
}

#[derive(Clone, Eq, PartialEq, Debug)]
pub enum ModeCommand {
    Cat,
    Find,
    Ls,
    Report,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Format {
    Text,
    Json,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum PathFormat {
    Undefined,
    Absolute,
    Full,
}

impl FromStr for Format {
    type Err = anyhow::Error;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s {
            "text" => Ok(Format::Text),
            "json" => Ok(Format::Json),
            _ => Err(format_err!("Unknown format")),
        }
    }
}

pub fn usage() -> String {
    let arg0 = env::args().next().unwrap_or("iquery".to_string());
    format!(
        "Usage: {:?} (--cat|--find|--ls) [--recursive] [--sort]
      [--format=<FORMAT>] [(--full_paths|--absolute_paths)] [--dir=<PATH>]
      PATH [...PATH]

  Utility for querying exposed object directories.

  Global options:
  --dir:     Change directory to the given PATH before executing commands.

  Mode options:
  --cat:    [DEFAULT] Print the data for the object(s) given by each PATH.
            Specifying --recursive will also output the children for that object.
  --find:   find all objects under PATH. For each sub-path, will stop at finding
            the first object. Specifying --recursive will search the whole tree.
  --ls:     List the children of the object(s) given by PATH. Specifying
            --recursive has no effect.
  --report: Output a default report for all components on the system. Ignores all
            settings other than --format.

  --recursive: Whether iquery should continue inside an object. See each mode's
               description to see how it modifies their behaviors.

  Formatting:
  --format: What formatter to use for output. Available options are:
    - text: [DEFAULT] Simple text output meant for manual inspection.
    - json: JSON format meant for machine consumption.

  --sort: Whether iquery should sort children by name before printing.

  --full_paths:     Include the full path in object names.
  --absolute_paths: Include full absolute path in object names.
                    Overrides --full_paths.

  PATH: paths where to look for targets. The interpretation of those depends
        on the mode.",
        arg0
    )
}

impl Options {
    fn new() -> Self {
        Self {
            mode: ModeCommand::Cat,
            global: GlobalOptions { dir: None },
            formatting: FormattingOptions {
                format: Format::Text,
                sort: false,
                path_format: PathFormat::Undefined,
            },
            recursive: false,
            path: vec![],
        }
    }

    pub fn read(mut args: Box<dyn Iterator<Item = String>>) -> Result<Self, Error> {
        // ignore arg[0]
        let _ = args.next();
        let mut opts = Options::new();
        while let Some(flag) = args.next() {
            let flag_str = flag.as_str();
            if !flag_str.starts_with("--") {
                opts.path = vec![flag.to_string()];
                opts.path.extend(args.map(|arg| arg.to_string()));
                break;
            }
            if flag_str.starts_with("--help") {
                println!("{}", usage());
                std::process::exit(0);
            } else if flag_str.starts_with("--dir") {
                let values = flag_str.split("=").collect::<Vec<&str>>();
                opts.global.dir = Some(values[1].to_string());
            } else if flag_str.starts_with("--format") {
                let values = flag_str.split("=").collect::<Vec<&str>>();
                match values[1] {
                    "json" => opts.formatting.format = Format::Json,
                    "text" => opts.formatting.format = Format::Text,
                    value => return Err(format_err!("Unexpected format: {}", value)),
                };
            } else {
                match flag_str {
                    "--recursive" => opts.recursive = true,
                    "--sort" => opts.formatting.sort = true,
                    "--full_paths" => opts.formatting.path_format = PathFormat::Full,
                    "--absolute_paths" => opts.formatting.path_format = PathFormat::Absolute,
                    "--cat" => opts.mode = ModeCommand::Cat,
                    "--find" => opts.mode = ModeCommand::Find,
                    "--ls" => opts.mode = ModeCommand::Ls,
                    "--report" => opts.mode = ModeCommand::Report,
                    flag => return Err(format_err!("Unknown flag: {}", flag)),
                }
            }
        }

        if opts.mode == ModeCommand::Report {
            Ok(opts.transform_for_report())
        } else if opts.path.is_empty() {
            // Print usage if path is empty
            println!("{}", usage());
            std::process::exit(0);
        } else {
            Ok(opts)
        }
    }

    /// Get the formatter.
    pub fn formatter(&self) -> Box<dyn Formatter> {
        match self.formatting.format {
            Format::Text => Box::new(TextFormatter::new(
                self.formatting.path_format.clone(),
                self.max_depth(),
                self.formatting.sort,
            )),
            Format::Json => Box::new(JsonFormatter::new(
                self.formatting.path_format.clone(),
                self.max_depth(),
                self.formatting.sort,
            )),
        }
    }

    /// Specifies the depth of the hierarchy to fetch based on the mode.
    /// A depth of 0 is the root node.
    /// None means, fetch everything.
    pub fn max_depth(&self) -> Option<u64> {
        match self.mode {
            ModeCommand::Cat => Some(0).filter(|_| self.recursive),
            // This case is special. None means search the whole tree, otherwise
            // the number doesn't matter, it'll recursively search all directories
            // but will stop when the roots are found.
            ModeCommand::Find => Some(0).filter(|_| !self.recursive),
            ModeCommand::Ls => None,
            ModeCommand::Report => None,
        }
    }

    fn transform_for_report(self) -> Self {
        let mut path = self.path.clone();
        path.extend(
            all_locations("/hub")
                .map(|loc| loc.path.to_string_lossy().to_string())
                .filter(|p| !p.contains("/system_objects")),
        );
        Self {
            mode: ModeCommand::Cat,
            global: GlobalOptions { dir: None },
            formatting: FormattingOptions {
                format: self.formatting.format,
                sort: true,
                path_format: PathFormat::Absolute,
            },
            recursive: true,
            path,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parse_opts() {
        let opts =
            "iquery --ls --recursive --sort --format=json --full_paths --dir=DIR PATH1 PATH2"
                .split(" ")
                .map(|s| s.to_string());
        let options = Options::read(Box::new(opts.into_iter())).expect("failed to read options");
        assert_eq!(options.mode, ModeCommand::Ls);
        assert_eq!(options.global.dir, Some("DIR".to_string()));
        assert_eq!(options.formatting.format, Format::Json);
        assert!(options.formatting.sort);
        assert_eq!(options.formatting.path_format, PathFormat::Full);
        assert!(options.recursive);
        assert_eq!(options.path, vec!["PATH1", "PATH2"]);
    }

    #[test]
    fn default_opts() {
        let opts = "iquery PATH".split(" ").map(|s| s.to_string());
        let options = Options::read(Box::new(opts.into_iter())).expect("failed to read options");
        assert_eq!(options.mode, ModeCommand::Cat);
        assert_eq!(options.global.dir, None);
        assert_eq!(options.formatting.format, Format::Text);
        assert!(!options.formatting.sort);
        assert_eq!(options.formatting.path_format, PathFormat::Undefined);
        assert!(!options.recursive);
        assert_eq!(options.path, vec!["PATH"]);
    }

    #[test]
    fn parse_report() {
        let opts = "iquery --report PATH".split(" ").map(|s| s.to_string());
        let options = Options::read(Box::new(opts.into_iter())).expect("failed to read options");
        assert_eq!(options.mode, ModeCommand::Cat);
        assert_eq!(options.global.dir, None);
        assert_eq!(options.formatting.format, Format::Text);
        assert!(options.formatting.sort);
        assert_eq!(options.formatting.path_format, PathFormat::Absolute);
        assert!(options.recursive);
        assert_eq!(options.path, vec!["PATH"]);
    }
}
