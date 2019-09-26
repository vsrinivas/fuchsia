// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{data::Data, puppet, results, trials, validate},
    failure::{bail, Error},
};

pub async fn run_all_trials(url: &str, results: &mut results::Results) {
    let url_split: Vec<&str> = url.split("/").collect();
    let cmx_name = url_split[url_split.len() - 1];
    let name_split: Vec<&str> = cmx_name.split(".").collect();
    let name = name_split[0];
    let mut trial_set = trials::real_trials();
    for trial in trial_set.iter_mut() {
        match puppet::Puppet::connect(url).await {
            Ok(mut puppet) => {
                let mut data = Data::new();
                if let Err(e) = run_trial(&mut puppet, &mut data, name, trial, results).await {
                    results.error(format!("Running trial {}, got failure: {:?}", trial.name, e));
                }
            }
            Err(e) => {
                results.error(format!(
                    "Failed to form Puppet - error {:?} - URL may be invalid: {}.",
                    e, url
                ));
            }
        }
    }
}

async fn run_trial(
    puppet: &mut puppet::Puppet,
    data: &mut Data,
    puppet_name: &str,
    trial: &mut trials::Trial,
    results: &mut results::Results,
) -> Result<(), Error> {
    let trial_name = format!("{}:{}", puppet_name, trial.name);
    try_compare(data, puppet, &trial_name, -1, None, -1)?;
    for (step_index, step) in trial.steps.iter_mut().enumerate() {
        for (action_number, action) in step.actions.iter_mut().enumerate() {
            if let Err(e) = data.apply(action) {
                bail!(
                    "Local-apply error in trial {}, step {}, action {}: {:?} ",
                    trial.name,
                    step_index,
                    action_number,
                    e
                );
            }
            match puppet.apply(action).await {
                Err(e) => {
                    bail!(
                        "Puppet-apply error in trial {}, step {}, action {}: {:?} ",
                        trial.name,
                        step_index,
                        action_number,
                        e
                    );
                }
                Ok(validate::TestResult::Ok) => {}
                Ok(validate::TestResult::Unimplemented) => {
                    results.unimplemented(puppet_name, action);
                    return Ok(());
                }
                Ok(bad_result) => {
                    bail!(
                        "In trial {}, puppet {} reported action {:?} was {:?}",
                        trial.name,
                        puppet_name,
                        action,
                        bad_result
                    );
                }
            }
            try_compare(
                data,
                puppet,
                &trial_name,
                step_index as i32,
                Some(action),
                action_number as i32,
            )?;
        }
    }
    Ok(())
}

fn try_compare(
    data: &Data,
    puppet: &puppet::Puppet,
    trial_name: &str,
    step_index: i32,
    action: Option<&validate::Action>,
    action_number: i32,
) -> Result<(), Error> {
    match puppet.read_data() {
        Err(e) => {
            bail!(
                "Puppet-read error in trial {}, step {}, action {} {:?}: {:?} ",
                trial_name,
                step_index,
                action_number,
                action,
                e
            );
        }
        Ok(puppet_data) => {
            if let Err(e) = data.compare(&puppet_data) {
                bail!(
                    "Compare error in trial {}, step {}, action {} {:?}: {:?} ",
                    trial_name,
                    step_index,
                    action_number,
                    action,
                    e
                );
            }
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use {
        super::*, crate::trials::tests::trial_with_action, crate::*, fidl_test_inspect_validate::*,
        fuchsia_async as fasync,
    };

    #[fasync::run_singlethreaded(test)]
    async fn unimplemented_works() -> Result<(), Error> {
        let mut int_maker = trial_with_action(
            "foo",
            create_numeric_property!(
            parent: ROOT_ID, id: 1, name: "int", value: Number::IntT(0)),
        );
        let mut uint_maker = trial_with_action(
            "foo",
            create_numeric_property!(
            parent: ROOT_ID, id: 2, name: "uint", value: Number::UintT(0)),
        );
        let mut results = results::Results::new();
        let mut puppet = puppet::tests::local_incomplete_puppet().await?;
        let mut data = Data::new();
        // results contains a list of the _un_implemented actions. local_incomplete_puppet()
        // implements Int creation, but not Uint. So results should not include Int but should
        // include Uint.
        run_trial(&mut puppet, &mut data, "int", &mut int_maker, &mut results).await?;
        run_trial(&mut puppet, &mut data, "uint", &mut uint_maker, &mut results).await?;
        assert!(!results.to_json().contains("int: CreateProperty(Int)"));
        assert!(results.to_json().contains("uint: CreateProperty(Uint)"));
        Ok(())
    }
}
