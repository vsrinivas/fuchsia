// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(missing_docs)]

//! `cs` performs a Component Search on the current system.

use {
    anyhow::Error,
    cs::{
        log_stats::{LogSeverity, LogStats},
        v2::V2Component,
    },
    fuchsia_async as fasync,
    structopt::StructOpt,
};

#[derive(StructOpt, Debug)]
#[structopt(
    name = "Component Statistics (cs) Reporting Tool",
    about = "Displays information about components on the system."
)]
struct Opt {
    /// Output detailed information about all v1 and v2 components on the system.
    #[structopt(short = "d", long = "detailed")]
    detailed: bool,

    /// Show number of log messages for each component broken down by severity.
    #[structopt(long = "log-stats")]
    log_stats: bool,

    /// The minimum severity to show in the log stats.
    #[structopt(long = "min-severity", default_value = "info")]
    min_severity: LogSeverity,
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    // Visit the directory /hub and recursively traverse it, outputting information about the
    // component hierarchy. See https://fuchsia.dev/fuchsia-src/concepts/components/hub for more
    // information on the Hub directory structure.
    let opt = Opt::from_args();

    if opt.log_stats {
        let log_stats = LogStats::new(opt.min_severity).await?;
        println!("{}", log_stats);
        return Ok(());
    }

    // Print out the component tree (and maybe component details)
    let component = V2Component::new_root_component("/hub-v2".to_string()).await;
    let mut lines = component.generate_tree();

    if opt.detailed {
        lines.push("".to_string());
        lines.append(&mut component.generate_details());
    }

    let output = lines.join("\n");
    println!("{}", output);

    Ok(())
}
