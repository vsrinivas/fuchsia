// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::io::Result;

/// Scrutiny is still a WIP this is just a placeholder for setting up the
/// build targets.
fn main() -> Result<()> {
    println!("scrutiny: work in progress replacement for fx component-graph");
    Ok(())
}

#[cfg(test)]
mod tests {
    /// Just an sanity check test to ensure host testing is hooked up correctly.
    #[test]
    fn test_sanity() {
        assert_eq!(1, 1);
    }
}
