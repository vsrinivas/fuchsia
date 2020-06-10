// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {anyhow::Result, component_plugin_lib::PackageDataCollector, futures_executor::block_on};

fn main() -> Result<()> {
    // TODO: Replace this with a plugin rather than separate executable.

    println!("Collecting data for fuchsia archive files.");

    let _res = block_on(PackageDataCollector::collect_manifests())?;

    println!("Done!");
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_main() {
        assert!(!main().is_err());
    }
}
