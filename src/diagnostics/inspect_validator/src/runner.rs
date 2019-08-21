// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{puppet, results, trials},
    failure::Error,
};

pub fn run(
    puppet: &mut puppet::Puppet,
    trials: trials::TrialSet,
    results: &mut results::Results,
) -> Result<(), Error> {
    for trial in trials::TrialSet::trials() {
        if let Err(e) = run_trial(puppet, &trial, trials.quirks(), results) {
            results.error(format!("Running test {}, got: {:?}", trial.name, e));
        }
    }
    Ok(())
}

fn run_trial(
    puppet: &mut puppet::Puppet,
    trial: &trials::Trial,
    _quirks: &trials::Quirks,
    results: &mut results::Results,
) -> Result<(), Error> {
    for step in trial.steps.iter() {
        puppet.apply(&step.actions, results);
        puppet.vmo_blocks(results)?;
    }
    Ok(())
}
