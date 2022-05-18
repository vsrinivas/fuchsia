// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod args;

use {
    anyhow::{format_err, Result},
    args::RestartCommand,
    fidl_fuchsia_driver_development as fdd,
    std::io::Write,
};

pub async fn restart(
    cmd: RestartCommand,
    writer: &mut impl Write,
    driver_development_proxy: fdd::DriverDevelopmentProxy,
) -> Result<()> {
    writeln!(writer, "Restarting driver hosts containing {}", cmd.driver_path)?;
    match driver_development_proxy.restart_driver_hosts(&mut cmd.driver_path.to_string()).await? {
        Ok(n) => {
            if n == 0 {
                writeln!(
                    writer,
                    "Did not find any matching driver hosts. Is the driver running and listed by `ffx driver list --loaded`?"
                )?;
            } else {
                writeln!(writer, "Restarted {} driver host{}.", n, if n == 1 { "" } else { "s" })?;
            }
            Ok(())
        }
        Err(err) => Err(format_err!("{:?}", err)),
    }
}
