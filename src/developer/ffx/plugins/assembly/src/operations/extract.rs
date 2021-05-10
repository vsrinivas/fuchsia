// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Result;
use ffx_assembly_args::ExtractArgs;

pub fn extract(_args: ExtractArgs) -> Result<()> {
    // Placeholder result.
    println!("This does nothing at the moment ^_^");
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::path::PathBuf;

    #[test]
    fn test_extract_no_args() {
        let result = extract(ExtractArgs { outdir: PathBuf::from(".") }).unwrap();
        assert_eq!(result, ());
    }
}
