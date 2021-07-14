// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {anyhow::*, ffx_core::ffx_plugin, ffx_selftest_experiment_args::ExperimentCommand};

#[ffx_plugin("selftest.experiment")]
pub async fn experiment(_cmd: ExperimentCommand) -> Result<()> {
    Ok(())
}
