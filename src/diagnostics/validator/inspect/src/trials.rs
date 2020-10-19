// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::validate::{self, Action, Number, NumberType, ROOT_ID};

pub enum Step {
    Actions(Vec<validate::Action>),
    LazyActions(Vec<validate::LazyAction>),
    WithMetrics(Vec<validate::Action>, String),
}

pub struct Trial {
    pub name: String,
    pub steps: Vec<Step>,
}

pub fn real_trials() -> Vec<Trial> {
    vec![
        basic_node(),
        basic_int(),
        basic_uint(),
        basic_double(),
        basic_string(),
        basic_bytes(),
        basic_bool(),
        basic_int_array(),
        basic_uint_array(),
        basic_double_array(),
        int_histogram_ops_trial(),
        uint_histogram_ops_trial(),
        double_histogram_ops_trial(),
        deletions_trial(),
        lazy_nodes_trial(),
        repeated_names(),
    ]
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
macro_rules! create_bool_property {
    (parent: $parent:expr, id: $id:expr, name: $name:expr, value: $value:expr) => {
        validate::Action::CreateBoolProperty(validate::CreateBoolProperty {
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
macro_rules! set_bool {
    (id: $id:expr, value: $value:expr) => {
        validate::Action::SetBool(validate::SetBool { id: $id, value: $value.into() })
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

#[macro_export]
macro_rules! create_array_property {
    (parent: $parent:expr, id: $id:expr, name: $name:expr, slots: $slots:expr, type: $type:expr) => {
        validate::Action::CreateArrayProperty(validate::CreateArrayProperty {
            parent: $parent,
            id: $id,
            name: $name.into(),
            slots: $slots,
            number_type: $type,
        })
    };
}

#[macro_export]
macro_rules! array_set {
    (id: $id:expr, index: $index:expr, value: $value:expr) => {
        validate::Action::ArraySet(validate::ArraySet { id: $id, index: $index, value: $value })
    };
}

#[macro_export]
macro_rules! array_add {
    (id: $id:expr, index: $index:expr, value: $value:expr) => {
        validate::Action::ArrayAdd(validate::ArrayAdd { id: $id, index: $index, value: $value })
    };
}

#[macro_export]
macro_rules! array_subtract {
    (id: $id:expr, index: $index:expr, value: $value:expr) => {
        validate::Action::ArraySubtract(validate::ArraySubtract {
            id: $id,
            index: $index,
            value: $value,
        })
    };
}

#[macro_export]
macro_rules! create_linear_histogram {
    (parent: $parent:expr, id: $id:expr, name: $name:expr, floor: $floor:expr,
        step_size: $step_size:expr, buckets: $buckets:expr, type: $type:ident) => {
        validate::Action::CreateLinearHistogram(validate::CreateLinearHistogram {
            parent: $parent,
            id: $id,
            name: $name.into(),
            floor: Number::$type($floor),
            step_size: Number::$type($step_size),
            buckets: $buckets,
        })
    };
}

#[macro_export]
macro_rules! create_exponential_histogram {
    (parent: $parent:expr, id: $id:expr, name: $name:expr, floor: $floor:expr,
        initial_step: $initial_step:expr, step_multiplier: $step_multiplier:expr,
        buckets: $buckets:expr, type: $type:ident) => {
        validate::Action::CreateExponentialHistogram(validate::CreateExponentialHistogram {
            parent: $parent,
            id: $id,
            name: $name.into(),
            floor: Number::$type($floor),
            initial_step: Number::$type($initial_step),
            step_multiplier: Number::$type($step_multiplier),
            buckets: $buckets,
        })
    };
}

#[macro_export]
macro_rules! insert {
    (id: $id:expr, value: $value:expr) => {
        validate::Action::Insert(validate::Insert { id: $id, value: $value })
    };
}

#[macro_export]
macro_rules! insert_multiple {
    (id: $id:expr, value: $value:expr, count: $count:expr) => {
        validate::Action::InsertMultiple(validate::InsertMultiple {
            id: $id,
            value: $value,
            count: $count,
        })
    };
}

#[macro_export]
macro_rules! create_lazy_node {
    (parent: $parent:expr, id: $id:expr, name: $name:expr, disposition: $disposition:expr, actions: $actions:expr) => {
        validate::LazyAction::CreateLazyNode(validate::CreateLazyNode {
            parent: $parent,
            id: $id,
            name: $name.into(),
            disposition: $disposition,
            actions: $actions,
        })
    };
}

#[macro_export]
macro_rules! delete_lazy_node {
    (id: $id:expr) => {
        validate::LazyAction::DeleteLazyNode(validate::DeleteLazyNode { id: $id })
    };
}

fn basic_node() -> Trial {
    Trial {
        name: "Basic Node".into(),
        steps: vec![Step::Actions(vec![
            create_node!(parent: ROOT_ID, id: 1, name: "child"),
            create_node!(parent: 1, id: 2, name: "grandchild"),
            delete_node!( id: 2),
            delete_node!( id: 1 ),
            // Verify they can be deleted in either order.
            create_node!(parent: ROOT_ID, id: 1, name: "child"),
            create_node!(parent: 1, id: 2, name: "grandchild"),
            delete_node!( id: 1),
            delete_node!( id: 2 ),
        ])],
    }
}

fn basic_string() -> Trial {
    Trial {
        name: "Basic String".into(),
        steps: vec![Step::Actions(vec![
            create_string_property!(parent: ROOT_ID, id:1, name: "str", value: "foo"),
            set_string!(id: 1, value: "bar"),
            set_string!(id: 1, value: "This Is A Longer String"),
            set_string!(id: 1, value: "."),
            // Make sure it can hold a string bigger than the biggest block (3000 chars > 2040)
            set_string!(id: 1, value: ["1234567890"; 300].to_vec().join("")),
            delete_property!(id: 1),
        ])],
    }
}

fn basic_bytes() -> Trial {
    Trial {
        name: "Basic bytes".into(),
        steps: vec![Step::Actions(vec![
            create_bytes_property!(parent: ROOT_ID, id: 8, name: "bytes", value: vec![1u8, 2u8]),
            set_bytes!(id: 8, value: vec![3u8, 4, 5, 6, 7]),
            set_bytes!(id: 8, value: vec![8u8]),
            delete_property!(id: 8),
        ])],
    }
}

fn basic_bool() -> Trial {
    Trial {
        name: "Basic Bool".into(),
        steps: vec![Step::Actions(vec![
            create_bool_property!(parent: ROOT_ID, id: 1, name: "bool", value: true),
            set_bool!(id: 1, value: false),
            set_bool!(id: 1, value: true),
            delete_property!(id: 1),
        ])],
    }
}

fn basic_int() -> Trial {
    Trial {
        name: "Basic Int".into(),
        steps: vec![Step::Actions(vec![
            create_numeric_property!(parent: ROOT_ID, id: 5, name: "int", value: Number::IntT(10)),
            set_number!(id: 5, value: Number::IntT(std::i64::MAX)),
            subtract_number!(id: 5, value: Number::IntT(3)),
            set_number!(id: 5, value: Number::IntT(std::i64::MIN)),
            add_number!(id: 5, value: Number::IntT(2)),
            delete_property!(id: 5),
        ])],
    }
}

fn basic_uint() -> Trial {
    Trial {
        name: "Basic Uint".into(),
        steps: vec![Step::Actions(vec![
            create_numeric_property!(parent: ROOT_ID, id: 5, name: "uint", value: Number::UintT(1)),
            set_number!(id: 5, value: Number::UintT(std::u64::MAX)),
            subtract_number!(id: 5, value: Number::UintT(3)),
            set_number!(id: 5, value: Number::UintT(0)),
            add_number!(id: 5, value: Number::UintT(2)),
            delete_property!(id: 5),
        ])],
    }
}

fn basic_double() -> Trial {
    Trial {
        name: "Basic Double".into(),
        steps: vec![Step::Actions(vec![
            create_numeric_property!(parent: ROOT_ID, id: 5, name: "double",
                                     value: Number::DoubleT(1.0)),
            set_number!(id: 5, value: Number::DoubleT(std::f64::MAX)),
            subtract_number!(id: 5, value: Number::DoubleT(std::f64::MAX/10_f64)),
            set_number!(id: 5, value: Number::DoubleT(std::f64::MIN)),
            add_number!(id: 5, value: Number::DoubleT(std::f64::MAX / 10_f64)),
            delete_property!(id: 5),
        ])],
    }
}

fn repeated_names() -> Trial {
    let mut actions = vec![create_node!(parent: ROOT_ID, id: 1, name: "measurements")];

    for i in 100..120 {
        actions.push(create_node!(parent: 1, id: i, name: format!("{}", i)));
        actions.push(create_numeric_property!(parent: i, id: i + 1000, name: "count", value: Number::UintT(i as u64 * 2)));
        actions.push(create_numeric_property!(parent: i, id: i + 2000, name: "time_spent", value: Number::UintT(i as u64 * 1000 + 10)));
    }

    Trial {
        name: "Many repeated names".into(),
        steps: vec![Step::WithMetrics(actions, "Many repeated names".into())],
    }
}

fn array_indexes_to_test() -> Vec<u64> {
    let mut ret: Vec<u64> = (0..10).collect();
    ret.push(1000);
    ret.push(10000);
    ret.push(std::u64::MAX);
    ret
}

fn basic_int_array() -> Trial {
    let mut actions = vec![create_array_property!(parent: ROOT_ID, id: 5, name: "int", slots: 5,
                                       type: NumberType::Int)];
    for index in array_indexes_to_test().iter() {
        actions.push(array_add!(id: 5, index: *index, value: Number::IntT(7)));
        actions.push(array_subtract!(id: 5, index: *index, value: Number::IntT(3)));
        actions.push(array_set!(id: 5, index: *index, value: Number::IntT(19)));
    }
    actions.push(delete_property!(id: 5));
    Trial { name: "Int Array Ops".into(), steps: vec![Step::Actions(actions)] }
}

fn basic_uint_array() -> Trial {
    let mut actions = vec![create_array_property!(parent: ROOT_ID, id: 6, name: "uint", slots: 5,
                                       type: NumberType::Uint)];
    for index in array_indexes_to_test().iter() {
        actions.push(array_add!(id: 6, index: *index, value: Number::UintT(11)));
        actions.push(array_subtract!(id: 6, index: *index, value: Number::UintT(3)));
        actions.push(array_set!(id: 6, index: *index, value: Number::UintT(19)));
    }
    actions.push(delete_property!(id: 6));
    Trial { name: "Unt Array Ops".into(), steps: vec![Step::Actions(actions)] }
}

fn basic_double_array() -> Trial {
    let mut actions = vec![create_array_property!(parent: ROOT_ID, id: 4, name: "float", slots: 5,
                                       type: NumberType::Double)];
    for index in array_indexes_to_test().iter() {
        actions.push(array_add!(id: 4, index: *index, value: Number::DoubleT(2.0)));
        actions.push(array_subtract!(id: 4, index: *index, value: Number::DoubleT(3.5)));
        actions.push(array_set!(id: 4, index: *index, value: Number::DoubleT(19.0)));
    }
    actions.push(delete_property!(id: 4));
    Trial { name: "Int Array Ops".into(), steps: vec![Step::Actions(actions)] }
}

fn int_histogram_ops_trial() -> Trial {
    fn push_ops(actions: &mut Vec<validate::Action>, value: i64) {
        actions.push(insert!(id: 4, value: Number::IntT(value)));
        actions.push(insert_multiple!(id: 4, value: Number::IntT(value), count: 3));
        actions.push(insert!(id: 5, value: Number::IntT(value)));
        actions.push(insert_multiple!(id: 5, value: Number::IntT(value), count: 3));
    }
    let mut actions = vec![
        create_linear_histogram!(parent: ROOT_ID, id: 4, name: "Lhist", floor: -5,
                                 step_size: 3, buckets: 3, type: IntT),
        create_exponential_histogram!(parent: ROOT_ID, id: 5, name: "Ehist", floor: -5,
                                 initial_step: 2, step_multiplier: 4,
                                 buckets: 3, type: IntT),
    ];
    for value in &[std::i64::MIN, std::i64::MAX, 0] {
        push_ops(&mut actions, *value);
    }
    for value in vec![-10_i64, -5_i64, 0_i64, 3_i64, 100_i64] {
        push_ops(&mut actions, value);
    }
    actions.push(delete_property!(id: 4));
    actions.push(delete_property!(id: 5));
    Trial { name: "Int Histogram Ops".into(), steps: vec![Step::Actions(actions)] }
}

fn uint_histogram_ops_trial() -> Trial {
    fn push_ops(actions: &mut Vec<validate::Action>, value: u64) {
        actions.push(insert!(id: 4, value: Number::UintT(value)));
        actions.push(insert_multiple!(id: 4, value: Number::UintT(value), count: 3));
        actions.push(insert!(id: 5, value: Number::UintT(value)));
        actions.push(insert_multiple!(id: 5, value: Number::UintT(value), count: 3));
    }
    let mut actions = vec![
        create_linear_histogram!(parent: ROOT_ID, id: 4, name: "Lhist", floor: 5,
                                 step_size: 3, buckets: 3, type: UintT),
        create_exponential_histogram!(parent: ROOT_ID, id: 5, name: "Ehist", floor: 5,
                                 initial_step: 2, step_multiplier: 4,
                                 buckets: 3, type: UintT),
    ];
    for value in &[std::u64::MAX, 0] {
        push_ops(&mut actions, *value);
    }
    for value in vec![0_u64, 5_u64, 8_u64, 20u64, 200_u64] {
        push_ops(&mut actions, value);
    }
    actions.push(delete_property!(id: 4));
    actions.push(delete_property!(id: 5));
    Trial { name: "Uint Histogram Ops".into(), steps: vec![Step::Actions(actions)] }
}

fn double_histogram_ops_trial() -> Trial {
    fn push_ops(actions: &mut Vec<validate::Action>, value: f64) {
        actions.push(insert!(id: 4, value: Number::DoubleT(value)));
        actions.push(insert_multiple!(id: 4, value: Number::DoubleT(value), count: 3));
        actions.push(insert!(id: 5, value: Number::DoubleT(value)));
        actions.push(insert_multiple!(id: 5, value: Number::DoubleT(value), count: 3));
    }
    let mut actions = vec![
        // Create exponential first in this test, so that if histograms aren't supported, both
        // linear and exponential will be reported as unsupported.
        create_exponential_histogram!(parent: ROOT_ID, id: 5, name: "Ehist",
                                floor: std::f64::consts::PI, initial_step: 2.0,
                                step_multiplier: 4.0, buckets: 3, type: DoubleT),
        create_linear_histogram!(parent: ROOT_ID, id: 4, name: "Lhist", floor: 5.0,
                                 step_size: 3.0, buckets: 3, type: DoubleT),
    ];
    for value in &[std::f64::MIN, std::f64::MAX, std::f64::MIN_POSITIVE, 0.0] {
        push_ops(&mut actions, *value);
    }
    for value in vec![3.0, 3.15, 5.0, 10.0] {
        push_ops(&mut actions, value as f64);
    }
    actions.push(delete_property!(id: 4));
    actions.push(delete_property!(id: 5));
    Trial { name: "Double Histogram Ops".into(), steps: vec![Step::Actions(actions)] }
}

fn deletions_trial() -> Trial {
    // Action, being a FIDL struct, doesn't implement Clone, so we have to build a new
    // Action each time we want to invoke it.
    fn n1() -> Action {
        create_node!(parent: ROOT_ID, id: 1, name: "root_child")
    }
    fn n2() -> Action {
        create_node!(parent: 1, id: 2, name: "parent")
    }
    fn n3() -> Action {
        create_node!(parent: 2, id: 3, name: "child")
    }
    fn p1() -> Action {
        create_numeric_property!(parent: 1, id: 4, name: "root_int", value: Number::IntT(1))
    }
    fn p2() -> Action {
        create_numeric_property!(parent: 2, id: 5, name: "parent_int", value: Number::IntT(2))
    }
    fn p3() -> Action {
        create_numeric_property!(parent: 3, id: 6, name: "child_int", value: Number::IntT(3))
    }
    fn create() -> Vec<Action> {
        vec![n1(), n2(), n3(), p1(), p2(), p3()]
    }
    fn create2() -> Vec<Action> {
        vec![n1(), p1(), n2(), p2(), n3(), p3()]
    }
    fn d1() -> Action {
        delete_node!(id: 1)
    }
    fn d2() -> Action {
        delete_node!(id: 2)
    }
    fn d3() -> Action {
        delete_node!(id: 3)
    }
    fn x1() -> Action {
        delete_property!(id: 4)
    }
    fn x2() -> Action {
        delete_property!(id: 5)
    }
    fn x3() -> Action {
        delete_property!(id: 6)
    }
    let mut steps = Vec::new();
    steps.push(Step::Actions(create()));
    steps.push(Step::Actions(vec![d3(), d2(), d1(), x3(), x2(), x1()]));
    steps.push(Step::Actions(create2()));
    steps.push(Step::WithMetrics(vec![d1(), d2()], "Delete Except Grandchild".into()));
    steps.push(Step::WithMetrics(vec![d3(), x3(), x2(), x1()], "Deleted Grandchild".into()));
    // This list tests all 6 sequences of node deletion.
    // TODO(fxbug.dev/40843): Get the permutohedron crate and test all 720 sequences.
    steps.push(Step::Actions(create()));
    steps.push(Step::Actions(vec![d1(), d2(), d3(), x3(), x2(), x1()]));
    steps.push(Step::Actions(create2()));
    steps.push(Step::Actions(vec![d1(), x3(), d2(), x1(), d3(), x2()]));
    steps.push(Step::Actions(create()));
    steps.push(Step::Actions(vec![d1(), x2(), d3(), x3(), d2(), x1()]));
    steps.push(Step::Actions(create2()));
    steps.push(Step::Actions(vec![x1(), x3(), d2(), d1(), d3(), x2()]));
    steps.push(Step::Actions(create()));
    steps.push(Step::Actions(vec![d2(), x3(), x2(), x1(), d3(), d1()]));
    steps.push(Step::Actions(create2()));
    steps.push(Step::Actions(vec![d3(), x3(), d2(), x1(), d1(), x2()]));
    steps.push(Step::Actions(create2()));
    steps.push(Step::Actions(vec![x3(), d3(), d1(), x1(), d2(), x2()]));
    steps.push(Step::WithMetrics(vec![], "Everything should be gone".into()));
    Trial { name: "Delete With Metrics".into(), steps }
}

fn lazy_nodes_trial() -> Trial {
    Trial {
        name: "Lazy Nodes".into(),
        steps: vec![Step::LazyActions(vec![
            // Create sibling node with same name and same content
            create_lazy_node!(
                parent: ROOT_ID,
                id: 1,
                name: "child",
                disposition: validate::LinkDisposition::Child,
                actions: vec![create_bytes_property!(parent: ROOT_ID, id: 1, name: "child_bytes",value: vec!(3u8, 4u8))]
            ),
            create_lazy_node!(
                parent: ROOT_ID,
                id: 2,
                name: "child",
                disposition: validate::LinkDisposition::Child,
                actions: vec![create_bytes_property!(parent: ROOT_ID, id: 1, name: "child_bytes",value: vec!(3u8, 4u8))]
            ),
            delete_lazy_node!(id: 1),
            delete_lazy_node!(id: 2),
            // Recreate child node with new values
            create_lazy_node!(
                parent: ROOT_ID,
                id: 1,
                name: "child",
                disposition: validate::LinkDisposition::Child,
                actions: vec![create_bytes_property!(parent: ROOT_ID, id: 1, name: "child_bytes_new",value: vec!(1u8, 2u8))]
            ),
            delete_lazy_node!(id: 1),
            // Create child node with inline disposition
            create_lazy_node!(
                parent: ROOT_ID,
                id: 1,
                name: "inline_child",
                disposition: validate::LinkDisposition::Inline,
                actions: vec![create_bytes_property!(parent: ROOT_ID, id: 1, name: "inline_child",value: vec!(1u8, 2u8))]
            ),
            delete_lazy_node!(id: 1),
        ])],
    }
}

#[cfg(test)]
pub(crate) mod tests {
    use {super::*, fidl_test_inspect_validate::*};

    pub fn trial_with_action(name: &str, action: Action) -> Trial {
        Trial { name: name.into(), steps: vec![Step::Actions(vec![action])] }
    }
}
