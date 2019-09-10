// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{data::Data, puppet, results, trials},
    failure::Error,
};

pub async fn run(
    puppet: &mut puppet::Puppet,
    trials: trials::TrialSet,
    results: &mut results::Results,
) -> Result<(), Error> {
    let mut data = Data::new();
    for trial in trials::TrialSet::trials().iter_mut() {
        if let Err(e) = run_trial(puppet, &mut data, trial, trials.quirks()).await {
            results.error(format!("Running test {}, got: {:?}", trial.name, e));
        }
    }
    Ok(())
}

async fn run_trial(
    puppet: &mut puppet::Puppet,
    data: &mut Data,
    trial: &mut trials::Trial,
    _quirks: &trials::Quirks,
) -> Result<(), Error> {
    println!("Starting run_trial, name {}, data {}", &trial.name, data.to_string());
    try_compare(data, puppet, &trial.name, -1, -1)?;
    for (step_index, step) in trial.steps.iter_mut().enumerate() {
        for (action_number, action) in step.actions.iter_mut().enumerate() {
            if let Err(e) = puppet.apply(action).await {
                println!(
                    "Puppet-apply error in trial {}, step {}, action {}: {:?} ",
                    trial.name, step_index, action_number, e
                );
                return Err(e);
            }
            if let Err(e) = data.apply(action) {
                println!(
                    "Local-apply error in trial {}, step {}, action {}: {:?} ",
                    trial.name, step_index, action_number, e
                );
                return Err(e);
            }
            try_compare(data, puppet, &trial.name, step_index as i32, action_number as i32)?;
        }
    }
    Ok(())
}

fn try_compare(
    data: &Data,
    puppet: &puppet::Puppet,
    trial_name: &str,
    step_index: i32,
    action_number: i32,
) -> Result<(), Error> {
    match puppet.read_data() {
        Err(e) => {
            println!(
                "Puppet-read error in trial {}, step {}, action {}: {:?} ",
                trial_name, step_index, action_number, e
            );
            return Err(e);
        }
        Ok(puppet_data) => {
            if let Err(e) = data.compare(&puppet_data) {
                println!(
                    "Compare error in trial {}, step {}, action {}: {:?} ",
                    trial_name, step_index, action_number, e
                );
                return Err(e);
            }
        }
    }
    Ok(())
}
