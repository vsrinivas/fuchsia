// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(missing_docs)]

//! `cs` performs a Component Search on the current system.

mod freq;
mod log_stats;

use {
    anyhow::{format_err, Error},
    cs::{io::Directory, v2::V2Component, Only, Subcommand, CS_INFO_HELP, CS_TREE_HELP},
    freq::BlobFrequencies,
    fuchsia_async as fasync,
    log_stats::{LogSeverity, LogStats},
    std::path::PathBuf,
    structopt::StructOpt,
};

#[derive(StructOpt, Debug)]
#[structopt(
    name = "Component Statistics (cs) Reporting Tool",
    about = "Displays information about components on the system."
)]
enum Opt {
    /// Output the component tree.
    #[structopt(name = "tree")]
    Tree {
        // Output only cmx/cml/running/stopped components depending on the flag.
        #[structopt(short = "o", long = "only")]
        only: Option<String>,
        // whether or not to display a column showing component type and a column
        // showing running/stopped.
        #[structopt(short = "v", long = "verbose")]
        verbose: bool,
    },

    /// Output detailed information about components on the system.
    #[structopt(name = "info")]
    Info {
        /// Print information for any component whose URL/name matches this substring.
        #[structopt(short = "f", long = "filter", default_value = "")]
        filter: String,
    },

    /// Output all components that expose a capability.
    #[structopt(name = "select")]
    Select {
        /// The capability to search for.
        #[structopt(short = "c", long = "capability", default_value = "")]
        capability: String,
    },

    /// Display per-component statistics for syslogs.
    #[structopt(name = "logs")]
    Logs {
        /// The minimum severity to show in the log stats.
        #[structopt(long = "min-severity", default_value = "info")]
        min_severity: LogSeverity,
    },

    /// Print out page-in frequencies for all blobs in CSV format
    #[structopt(name = "freq")]
    PageInFrequencies,
}

fn validate_hub_directory() -> Option<Directory> {
    let hub_path = PathBuf::from("/hub-v2");
    match Directory::from_namespace(hub_path.clone()) {
        Ok(hub_dir) => return Some(hub_dir),
        Err(e) => {
            eprintln!("`/hub-v2` could not be opened: {:?}", e);
            eprintln!("Do not run `cs` from the serial console. Use `fx shell` instead.");
            return None;
        }
    };
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    // Visit the directory /hub and recursively traverse it, outputting information about the
    // component hierarchy. See https://fuchsia.dev/fuchsia-src/concepts/components/hub for more
    // information on the Hub directory structure.
    let opt = Opt::from_args();

    match opt {
        Opt::Logs { min_severity } => {
            let log_stats = LogStats::new(min_severity).await?;
            println!("{}", log_stats);
        }
        Opt::Info { filter } => {
            println!("'cs info' is deprecated. Please use 'ffx component show' instead!");
            if let Some(hub_dir) = validate_hub_directory() {
                let component = V2Component::explore(hub_dir, Subcommand::Show).await;
                component.print_details(&filter).map_err(|e| {
                    format_err!(
                        "'{}' was not found in the component tree: {}\n{}",
                        filter,
                        e,
                        CS_INFO_HELP
                    )
                })?
            }
        }
        Opt::Select { capability } => {
            println!("'cs select' is deprecated. Please use 'ffx component select' instead!");
            if let Some(hub_dir) = validate_hub_directory() {
                let component = V2Component::explore(hub_dir, Subcommand::Select).await;
                component.print_components_exposing_capability(&capability);
            }
        }
        Opt::Tree { only, verbose } => {
            println!("'cs tree' is deprecated. Please use 'ffx component list' instead!");
            if let Some(hub_dir) = validate_hub_directory() {
                let component = V2Component::explore(hub_dir, Subcommand::List).await;
                if let Some(only) = only {
                    let only = Only::from_string(&only).map_err(|e| {
                        format_err!(
                            "Invalid argument '{}' for '--only': {}\n{}",
                            only,
                            e,
                            CS_TREE_HELP
                        )
                    })?;
                    component.print_tree(only, verbose);
                } else {
                    // Default option is printing all components
                    component.print_tree(Only::All, verbose);
                }
            }
        }
        Opt::PageInFrequencies => {
            let frequencies = BlobFrequencies::collect().await;
            println!("{}", frequencies);
        }
    }
    Ok(())
}
