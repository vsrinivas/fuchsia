// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {fidl_fuchsia_wlan_common as fidl_common, std::collections::HashMap};

// The ExperimentType, ExperimentId, and Experiments represent the internal telemetry accounting
// for any active WLAN policy experiments.  The rest of the telemetry module will see an
// Experiments struct that can emit integers representing active experiments.

#[derive(Debug, Eq, Hash, PartialEq)]
enum ExperimentType {
    Power,
}

#[derive(Debug, PartialEq)]
struct ExperimentId(u32);

pub struct Experiments {
    experiments: HashMap<ExperimentType, ExperimentId>,
}

impl Experiments {
    pub fn new() -> Self {
        let mut experiments = HashMap::new();

        // Manually populate all of the default experiment IDs.  The hash map is guaranteed to be
        // empty at this point, so the return can be safely ignored.
        let _ = experiments.insert(ExperimentType::Power, CONTROL_POWER_EXPERIMENT_ID);

        Experiments { experiments }
    }

    pub fn get_experiment_ids(&self) -> Vec<u32> {
        let mut experiments = Vec::new();
        for experiment_id in self.experiments.values() {
            experiments.push(experiment_id.0)
        }

        experiments
    }

    pub fn update_experiment(&mut self, update: ExperimentUpdate) {
        let (experiment, id) = update.as_key_value();

        // The experiment enum is already expected to exist in the hashmap, so this return value is
        // of no interest.
        let _ = self.experiments.insert(experiment, id);
    }
}

pub fn default_experiments() -> Vec<u32> {
    let experiments = Experiments::new();
    experiments.get_experiment_ids()
}

// The ExperimentUpdate is the outward facing enum through which the rest of the policy layer may
// communicate changes to experiment status to the telemetry module.  The rest of the policy layer
// will be able to construct this enum with other publicly visible FIDL enums or structs and this
// mod will deal with the translation to experiment IDs.
#[derive(Debug, PartialEq)]
pub enum ExperimentUpdate {
    Power(fidl_common::PowerSaveType),
}

impl ExperimentUpdate {
    fn as_key_value(&self) -> (ExperimentType, ExperimentId) {
        match *self {
            ExperimentUpdate::Power(power_save_type) => {
                (ExperimentType::Power, ExperimentId::from(power_save_type))
            }
        }
    }
}

// Power-related experiment IDs.
const CONTROL_POWER_EXPERIMENT_ID: ExperimentId = ExperimentId(49331091);
const ULTRA_LOW_POWER_EXPERIMENT_ID: ExperimentId = ExperimentId(49331092);
const LOW_POWER_EXPERIMENT_ID: ExperimentId = ExperimentId(49331142);
const BALANCED_EXPERIMENT_ID: ExperimentId = ExperimentId(49331143);
const PERFORMANCE_EXPERIMENT_ID: ExperimentId = ExperimentId(49331144);

impl From<fidl_common::PowerSaveType> for ExperimentId {
    fn from(ps_type: fidl_common::PowerSaveType) -> Self {
        match ps_type {
            fidl_common::PowerSaveType::PsModeUltraLowPower => ULTRA_LOW_POWER_EXPERIMENT_ID,
            fidl_common::PowerSaveType::PsModeLowPower => LOW_POWER_EXPERIMENT_ID,
            fidl_common::PowerSaveType::PsModeBalanced => BALANCED_EXPERIMENT_ID,
            fidl_common::PowerSaveType::PsModePerformance => PERFORMANCE_EXPERIMENT_ID,
        }
    }
}

#[cfg(test)]
mod tests {
    use {super::*, test_case::test_case};

    #[test_case(ExperimentUpdate::Power(fidl_common::PowerSaveType::PsModePerformance), (ExperimentType::Power, PERFORMANCE_EXPERIMENT_ID))]
    fn test_key_value_conversion(
        update: ExperimentUpdate,
        expected: (ExperimentType, ExperimentId),
    ) {
        assert_eq!(update.as_key_value(), expected)
    }

    #[test_case(fidl_common::PowerSaveType::PsModeUltraLowPower, ULTRA_LOW_POWER_EXPERIMENT_ID)]
    #[test_case(fidl_common::PowerSaveType::PsModeLowPower, LOW_POWER_EXPERIMENT_ID)]
    #[test_case(fidl_common::PowerSaveType::PsModeBalanced, BALANCED_EXPERIMENT_ID)]
    #[test_case(fidl_common::PowerSaveType::PsModePerformance, PERFORMANCE_EXPERIMENT_ID)]
    fn test_power_save_type_conversions(
        ps_type: fidl_common::PowerSaveType,
        expected: ExperimentId,
    ) {
        assert_eq!(ExperimentId::from(ps_type), expected)
    }

    #[test]
    fn test_experiments_initialization() {
        // Ensure that the experiment IDs are initialized properly
        let experiments = Experiments::new();
        assert_eq!(experiments.experiments.len(), 1);
        assert_eq!(experiments.experiments[&ExperimentType::Power], CONTROL_POWER_EXPERIMENT_ID);
        assert_eq!(experiments.get_experiment_ids(), vec![CONTROL_POWER_EXPERIMENT_ID.0]);
    }

    #[test]
    fn test_power_save_update() {
        let mut experiments = Experiments::new();

        // Verify the initial state of the power save experiment
        assert_eq!(experiments.experiments[&ExperimentType::Power], CONTROL_POWER_EXPERIMENT_ID);
        assert_eq!(experiments.get_experiment_ids(), vec![CONTROL_POWER_EXPERIMENT_ID.0]);

        // Change the experiment type.
        experiments
            .update_experiment(ExperimentUpdate::Power(fidl_common::PowerSaveType::PsModeBalanced));

        // Verify that the experiments have been updated.
        assert_eq!(experiments.get_experiment_ids(), vec![BALANCED_EXPERIMENT_ID.0])
    }
}
