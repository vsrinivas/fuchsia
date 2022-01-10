// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, bail, Result},
    log::warn,
    std::env,
    std::path::PathBuf,
};

pub fn fuchsia_build_dir() -> Result<PathBuf> {
    let fuchsia_build_dir = match env::var_os("FUCHSIA_BUILD_DIR") {
        Some(ostr) => {
            let str = ostr
                .into_string()
                .map_err(|_| anyhow!("FUCHSIA_BUILD_DIR contained invalid Unicode"))?;
            PathBuf::from(str)
        }
        None => {
            warn!("$FUCHSIA_BUILD_DIR not set, defaulting to \"$FUCHSIA_DIR/out/default\"");
            let fuchsia_dir = match env::var_os("FUCHSIA_DIR") {
                Some(ostr) => ostr.into_string().map_err(|_| anyhow!("FUCHSIA_DIR contained invalid Unicode"))?,
                None => bail!("At least one of $FUCHSIA_BUILD_DIR nor $FUCHSIA_DIR must be set in environment"),
            };
            let mut dir = PathBuf::from(fuchsia_dir);
            dir.push("out/default");
            dir
        }
    };
    Ok(fuchsia_build_dir)
}
