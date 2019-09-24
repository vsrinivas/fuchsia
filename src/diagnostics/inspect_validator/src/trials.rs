// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::validate::{self, Number, ROOT_ID};

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
        vec![simple_ops_trial(), basic_trial()]
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
macro_rules! create_numeric_property {
    (parent: $parent:expr, id: $id:expr, name: $name:expr, value: $value:expr) => {
        validate::Action::CreateNumericProperty(validate::CreateNumericProperty {
            parent: $parent,
            id: $id,
            name: $name.into(),
            value: $value,
        })
    };
}

#[macro_export]
macro_rules! create_bytes_property {
    (parent: $parent:expr, id: $id:expr, name: $name:expr, value: $value:expr) => {
        validate::Action::CreateBytesProperty(validate::CreateBytesProperty {
            parent: $parent,
            id: $id,
            name: $name.into(),
            value: $value.into(),
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
macro_rules! set_string {
    (id: $id:expr, value: $value:expr) => {
        validate::Action::SetString(validate::SetString { id: $id, value: $value.into() })
    };
}

#[macro_export]
macro_rules! set_bytes {
    (id: $id:expr, value: $value:expr) => {
        validate::Action::SetBytes(validate::SetBytes { id: $id, value: $value.into() })
    };
}

#[macro_export]
macro_rules! set_number {
    (id: $id:expr, value: $value:expr) => {
        validate::Action::SetNumber(validate::SetNumber { id: $id, value: $value })
    };
}

#[macro_export]
macro_rules! add_number {
    (id: $id:expr, value: $value:expr) => {
        validate::Action::AddNumber(validate::AddNumber { id: $id, value: $value })
    };
}

#[macro_export]
macro_rules! subtract_number {
    (id: $id:expr, value: $value:expr) => {
        validate::Action::SubtractNumber(validate::SubtractNumber { id: $id, value: $value })
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
                create_numeric_property!(parent: ROOT_ID, id:2, name: "answer", value: Number::IntT(42)),
                create_node!(parent: ROOT_ID, id: 2, name: "grandchild"),
                create_string_property!(parent: 1, id:3, name: "str2", value: "bar"),
                create_numeric_property!(parent: 2, id:4, name: "question", value: Number::IntT(7)),
                create_numeric_property!(parent: 2, id:5, name: "uint", value: Number::UintT(8)),
                create_numeric_property!(parent: 2, id: 6, name: "double-double", value: Number::DoubleT(0.5)),
                create_bytes_property!(parent: 2, id: 7, name: "byte byte byte", value: vec![1, 2, 3]),
                delete_property!(id: 1),
                delete_property!(id: 2),
                delete_property!(id: 3),
                delete_property!(id: 4),
                delete_property!(id: 5),
                delete_property!(id: 6),
                delete_property!(id: 7),
                delete_node!( id: 2),
                delete_node!( id: 1 ),
            ],
        }],
    }
}

fn simple_ops_trial() -> Trial {
    Trial {
        name: "Simple Ops".into(),
        steps: vec![Step {
            actions: vec![
                create_numeric_property!(parent: ROOT_ID, id: 4, name: "float", value: Number::DoubleT(1.0)),
                add_number!(id: 4, value: Number::DoubleT(2.0)),
                subtract_number!(id: 4, value: Number::DoubleT(3.5)),
                set_number!(id: 4, value: Number::DoubleT(19.0)),
                create_numeric_property!(parent: ROOT_ID, id: 5, name: "int", value: Number::IntT(1)),
                add_number!(id: 5, value: Number::IntT(2)),
                subtract_number!(id: 5, value: Number::IntT(3)),
                set_number!(id: 5, value: Number::IntT(19)),
                create_numeric_property!(parent: ROOT_ID, id: 6, name: "uint", value: Number::UintT(1)),
                add_number!(id: 6, value: Number::UintT(2)),
                subtract_number!(id: 6, value: Number::UintT(3)),
                set_number!(id: 6, value: Number::UintT(19)),
                create_string_property!(parent: ROOT_ID, id: 7, name: "str", value: "foo"),
                set_string!(id: 7, value: "bar"),
                create_bytes_property!(parent: ROOT_ID, id: 8, name: "bytes", value: vec![1u8, 2u8]),
                set_bytes!(id: 8, value: vec![3u8]),
            ],
        }],
    }
}

pub fn trial_set() -> TrialSet {
    TrialSet { quirks: Quirks { replace_same_name: false } }
}
