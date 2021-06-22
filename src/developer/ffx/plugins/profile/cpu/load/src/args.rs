// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command, std::time::Duration};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "load",
    description = "Measure CPU load over the specified duration",
    example = "To measure the CPU load over a two second duration

    $ ffx profile cpu load --duration 2
    ",
    note = "This command uses the GetCpuLoad method of the fuchsia.kernel.Stats service to query the
the load from each active CPU core in the system. The measured CPU load from each core is
printed in the following format:

    CPU 0: 0.66%
    CPU 1: 1.56%
    CPU 2: 0.83%
    CPU 3: 0.71%
    Total: 3.76%

The valid range for each CPU load is [0-100]%. The \"Total\" value represents the summation
of the load percentages of all CPU cores and is valid in the range [0-100*[NUM_CPU]]%
"
)]
pub struct CpuLoadCommand {
    #[argh(option, long = "duration", short = 'd', from_str_fn(parse_duration))]
    /// duration over which to measure the CPU load
    pub duration: Duration,
}

fn parse_duration(value: &str) -> Result<Duration, String> {
    Ok(Duration::from_secs(
        value.parse().map_err(|e| format!("value '{}' is not a number: {}", value, e))?,
    ))
}
