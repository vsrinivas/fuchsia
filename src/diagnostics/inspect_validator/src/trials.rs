// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::validate::{self, ROOT_ID};

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

    #[allow(dead_code)]
    pub fn new() -> Quirks {
        Quirks { replace_same_name: false }
    }
}

#[macro_export]
macro_rules! create_node {
    (parent: $parent:expr, id: $id:expr, name: $name:expr) => {
        validate::Action::CreateNode(validate::CreateNode {
            parent: $parent,
            id: $id,
            name: $name.into(),
        })
    };
}

#[macro_export]
macro_rules! delete_node {
    (id: $id:expr) => {
        validate::Action::DeleteNode(validate::DeleteNode { id: $id })
    };
}

#[macro_export]
macro_rules! create_int_property {
    (parent: $parent:expr, id: $id:expr, name: $name:expr, value: $value:expr) => {
        validate::Action::CreateIntProperty(validate::CreateIntProperty {
            parent: $parent,
            id: $id,
            name: $name.into(),
            value: $value,
        })
    };
}

#[macro_export]
macro_rules! create_string_property {
    (parent: $parent:expr, id: $id:expr, name: $name:expr, value: $value:expr) => {
        validate::Action::CreateStringProperty(validate::CreateStringProperty {
            parent: $parent,
            id: $id,
            name: $name.into(),
            value: $value.into(),
        })
    };
}

#[macro_export]
macro_rules! delete_property {
    (id: $id:expr) => {
        validate::Action::DeleteProperty(validate::DeleteProperty { id: $id })
    };
}

fn basic_trial() -> Trial {
    Trial {
        name: "Basic Trial".into(),
        steps: vec![Step {
            actions: vec![
                create_node!(parent: ROOT_ID, id: 1, name: "child"),
                create_string_property!(parent: ROOT_ID, id:1, name: "str", value: "foo"),
                create_int_property!(parent: ROOT_ID, id:2, name: "answer", value: 42),
                create_node!(parent: ROOT_ID, id: 2, name: "grandchild"),
                create_string_property!(parent: 1, id:3, name: "str2", value: "bar"),
                create_int_property!(parent: 2, id:4, name: "question", value: 7),
                delete_property!(id: 1),
                delete_property!(id: 2),
                delete_property!(id: 3),
                delete_property!(id: 4),
                delete_node!( id: 2),
                delete_node!( id: 1 ),
            ],
        }],
    }
}

pub fn trial_set() -> TrialSet {
    TrialSet { quirks: Quirks { replace_same_name: false } }
}
