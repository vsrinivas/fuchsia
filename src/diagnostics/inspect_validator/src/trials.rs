// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::validate;

pub struct Step {
    pub actions: Vec<validate::Action>,
    //metrics: Vec<metrics::Metric>, Ignore for now - will be in a future CL
}

pub struct Trial {
    pub name: String,
    pub steps: Vec<Step>,
}

/// Language- or library-specific quirks needed for proper prediction
/// of the VMO contents.
pub struct Quirks {
    /// In some languages (such as C++) creating two things with the same
    /// name creates two entries in the VMO. In others (such as Dart) the
    /// second entry will replace the first.
    replace_same_name: bool,
}

pub struct TrialSet {
    quirks: Quirks,
}

impl Step {
    /* Ignore for now - will be in a future CL
    pub fn run_metrics(
        &self,
        info: &InfoTree,
        results: &mut results::Results,
    ) -> Result<(), Error> {
        for metric in self.metrics.iter() {
            metric.process(info, results)?;
        }
        Ok(())
    }*/
}

impl TrialSet {
    /// Call this if the second create with the same name replaces the first entry.
    /// Don't call this if creating two of the same name makes two entries.
    #[allow(dead_code)]
    pub fn replace_same_name(&mut self) {
        self.quirks.replace_same_name = true;
    }

    pub fn trials() -> Vec<Trial> {
        vec![basic_trial()]
    }

    pub fn quirks(&self) -> &Quirks {
        &self.quirks
    }
}

impl Quirks {
    #[allow(dead_code)]
    pub fn does_same_name_replace(&self) -> bool {
        self.replace_same_name
    }
}

fn basic_trial() -> Trial {
    Trial {
        name: "Basic Trial".into(),
        steps: vec![Step {
            actions: vec![
                validate::Action::CreateNode(validate::CreateNode {
                    parent: validate::ROOT_ID,
                    id: 1,
                    name: "child".into(),
                }),
                validate::Action::DeleteNode(validate::DeleteNode { id: 1 }),
            ],
            //            metrics: vec![],
        }],
    }
}

pub fn trial_set() -> TrialSet {
    TrialSet { quirks: Quirks { replace_same_name: false } }
}
