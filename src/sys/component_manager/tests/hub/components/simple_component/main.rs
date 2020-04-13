// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {anyhow::Error, fuchsia_syslog as syslog, log::*};

fn main() -> Result<(), Error> {
    syslog::init_with_tags(&["simple_component"])?;
    info!("Child created!");
    Ok(())
}
