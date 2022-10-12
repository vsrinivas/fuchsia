// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Result},
    std::fs,
    std::path::Path,
};

/// Verifies that the input was actually written and matches its expected contents.
pub fn verify_saved<P: AsRef<Path>>(saved: P, data: &[u8]) -> Result<()> {
    let saved = saved.as_ref();
    let actual =
        fs::read(saved).with_context(|| format!("failed to read '{}'", saved.to_string_lossy()))?;
    assert_eq!(actual, data);
    Ok(())
}
