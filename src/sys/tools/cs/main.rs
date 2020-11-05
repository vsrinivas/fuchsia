// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(missing_docs)]

//! `cs` performs a Component Search on the current system.

use {
    anyhow::Error,
    cs::{
        freq::BlobFrequencies,
        log_stats::{LogSeverity, LogStats},
        v2::V2Component,
    },
    fuchsia_async as fasync,
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
    Tree,

    /// Output detailed information about components on the system.
    #[structopt(name = "info")]
    Info {
        /// Print information for any component whose URL/name matches this substring.
        #[structopt(short = "f", long = "filter", default_value = "")]
        filter: String,
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
            let path = PathBuf::from("/hub-v2");
            let component = V2Component::explore(path).await;
            component.print_details(&filter);
        }
        Opt::Tree => {
            let path = PathBuf::from("/hub-v2");
            let component = V2Component::explore(path).await;
            component.print_tree();
        }
        Opt::PageInFrequencies => {
            let frequencies = BlobFrequencies::collect().await;
            println!("{}", frequencies);
        }
    }
    Ok(())
}
