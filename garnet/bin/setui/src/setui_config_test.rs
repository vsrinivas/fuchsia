// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{bail, Context, Error};
use settings::ServiceConfiguration;
use std::fs::File;
use std::io::Read;

fn main() -> Result<(), Error> {
    let mut args = std::env::args();
    if args.len() < 2 {
        bail!("Not enough args");
    }

    // Skip program name.
    let _ = args.next();
    // Get path to config.
    let path = &args.next().unwrap();
    let mut file =
        File::open(path).with_context(|| format!("Couldn't open arg path `{}`", path))?;
    let mut config_string = String::new();
    file.read_to_string(&mut config_string).context("Couldn't read config")?;
    let _ = serde_json::from_str::<ServiceConfiguration>(&config_string)
        .context("Failed to deserialize config")?;
    Ok(())
}
