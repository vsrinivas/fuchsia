// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{
        metrics::Metrics,
        validate::{self, Number, ROOT_ID},
        DiffType,
    },
    anyhow::{bail, format_err, Error},
    base64, difference,
    fuchsia_inspect::{self, format::block::ArrayFormat},
    fuchsia_inspect_node_hierarchy::{
        ArrayContent, LinkNodeDisposition, NodeHierarchy, Property as iProperty,
    },
    num_derive::{FromPrimitive, ToPrimitive},
    std::{
        self,
        collections::{HashMap, HashSet},
        convert::{From, TryInto},
    },
};

mod scanner;
pub use scanner::Scanner;
mod fetch;
pub use fetch::LazyNode;

#[cfg(test)]
use num_traits::ToPrimitive;

const ROOT_NAME: &str = "root";

/// A local store of Inspect-like data which can be built by Action or filled
/// from a VMO.
///
/// For now, Data assumes it will not be given two sibling-nodes or
/// properties with the same name, and does not truncate any data or names.
#[derive(Debug, Clone)]
pub struct Data {
    nodes: HashMap<u32, Node>,
    properties: HashMap<u32, Property>,
    tombstone_nodes: HashSet<u32>,
    tombstone_properties: HashSet<u32>,
}

// Data is the only public struct in this file. The internal data structures are
// a bit complicated...
//
// Node, Property, and Payload are the "clean data" holders - they can be created
// either by reading from a VMO, or by applying Actions (the same actions that
// are sent to the puppets and act on their VMOs).
//
// The Actions specify arbitrary u32 keys to refer to nodes and properties to
// create, modify, and delete. It's an error to misuse a key (e.g. double-create
// a key or delete a missing key).
//
// In both Data-from-Actions and Data-from-VMO, the "root" node is virtual; nodes
// and properties with a "parent" ID of 0 are directly under the "root" of the tree.
// A placeholder Node is placed at index 0 upon creation to hold the real Nodes and
// properties added during scanning VMO or processing Actions.

#[derive(Debug, Clone)]
pub struct Node {
    name: String,
    parent: u32,
    children: HashSet<u32>,
    properties: HashSet<u32>,
}

#[derive(Debug, Clone)]
pub struct Property {
    name: String,
    id: u32,
    parent: u32,
    payload: Payload,
}

#[derive(Debug, Clone)]
enum Payload {
    String(String),
    Bytes(Vec<u8>),
    Int(i64),
    Uint(u64),
    Double(f64),
    Bool(bool),
    IntArray(Vec<i64>, ArrayFormat),
    UintArray(Vec<u64>, ArrayFormat),
    DoubleArray(Vec<f64>, ArrayFormat),
    Link { disposition: LinkNodeDisposition, parsed_data: Data },

    // Used when parsing from JSON. We have trouble identifying numeric types and types of
    // histograms from the output. We can use these generic types to be safe for comparison from
    // JSON.
    GenericNumber(String),
    GenericArray(Vec<String>),
    GenericHistogram(Vec<String>),
}

fn to_string<T: std::fmt::Display>(v: T) -> String {
    format!("{}", v)
}

fn to_string_array<T: std::fmt::Display>(values: Vec<T>) -> Vec<String> {
    values.into_iter().map(|x| to_string(x)).collect()
}

fn handle_array(values: Vec<String>, format: ArrayFormat) -> Payload {
    match format {
        ArrayFormat::LinearHistogram => {
            Payload::GenericHistogram(values.into_iter().skip(2).collect())
        }
        ArrayFormat::ExponentialHistogram => {
            Payload::GenericHistogram(values.into_iter().skip(3).collect())
        }
        _ => Payload::GenericArray(values),
    }
}

trait ToGeneric {
    fn to_generic(self) -> Payload;
}

impl ToGeneric for Payload {
    /// Convert this payload into a generic representation that is compatible with JSON.
    fn to_generic(self) -> Self {
        match self {
            Payload::IntArray(val, format) => handle_array(to_string_array(val), format),
            Payload::UintArray(val, format) => handle_array(to_string_array(val), format),
            Payload::DoubleArray(val, format) => handle_array(to_string_array(val), format),
            Payload::Bytes(val) => Payload::String(format!("b64:{}", base64::encode(&val))),
            Payload::Int(val) => Payload::GenericNumber(to_string(val)),
            Payload::Uint(val) => Payload::GenericNumber(to_string(val)),
            Payload::Double(val) => Payload::GenericNumber(to_string(val)),
            Payload::Link { disposition, parsed_data } => {
                Payload::Link { disposition, parsed_data: parsed_data.clone_generic() }
            }
            val => val,
        }
    }
}

impl<T: std::fmt::Display> ToGeneric for ArrayContent<T> {
    fn to_generic(self) -> Payload {
        match self {
            ArrayContent::Values(values) => Payload::GenericArray(to_string_array(values)),
            ArrayContent::Buckets(buckets) => Payload::GenericHistogram(to_string_array(
                buckets.into_iter().map(|bucket| bucket.count).collect(),
            )),
        }
    }
}

struct FormattedEntries {
    nodes: Vec<String>,
    properties: Vec<String>,
}

impl Property {
    fn to_string(&self, prefix: &str) -> String {
        format!("{}{}: {:?}", prefix, self.name, &self.payload)
    }

    /// Formats this property and any additional properties and nodes it may contain (in the case
    /// of links).
    fn format_entries(&self, prefix: &str) -> FormattedEntries {
        match &self.payload {
            Payload::Link { disposition, parsed_data } => match disposition {
                // Return a node for the child, replacing its name.
                LinkNodeDisposition::Child => FormattedEntries {
                    nodes: vec![format!(
                        "{}{} ->\n{}",
                        prefix,
                        self.name,
                        parsed_data.nodes[&0].to_string(&prefix, &parsed_data, true)
                    )],
                    properties: vec![],
                },
                // Return the nodes and properties (which may themselves have linked nodes) inline
                // from this property.
                LinkNodeDisposition::Inline => {
                    let root = &parsed_data.nodes[&0];
                    let mut nodes = root
                        .children
                        .iter()
                        .map(|v| {
                            parsed_data.nodes.get(v).map_or("Missing child".into(), |n| {
                                n.to_string(&prefix, &parsed_data, false)
                            })
                        })
                        .collect::<Vec<_>>();
                    let mut properties = vec![];
                    for FormattedEntries { nodes: mut n, properties: mut p } in
                        root.properties.iter().map(|v| {
                            parsed_data.properties.get(v).map_or(
                                FormattedEntries {
                                    nodes: vec![],
                                    properties: vec!["Missing property".into()],
                                },
                                |p| p.format_entries(&prefix),
                            )
                        })
                    {
                        nodes.append(&mut n);
                        properties.append(&mut p);
                    }
                    FormattedEntries { nodes, properties }
                }
            },
            // Non-link property, just format as the only returned property.
            _ => FormattedEntries { nodes: vec![], properties: vec![self.to_string(prefix)] },
        }
    }
}

impl Node {
    /// If `hide_root` is true and the node is the root,
    /// then the name and and prefix of the generated string is omitted.
    /// This is used for lazy nodes wherein we don't what to show the label "root" for lazy nodes.
    fn to_string(&self, prefix: &str, tree: &Data, hide_root: bool) -> String {
        let sub_prefix = format!("{}> ", prefix);
        let mut nodes = vec![];
        for node_id in self.children.iter() {
            nodes.push(
                tree.nodes
                    .get(node_id)
                    .map_or("Missing child".into(), |n| n.to_string(&sub_prefix, tree, hide_root)),
            );
        }
        let mut properties = vec![];

        for property_id in self.properties.iter() {
            let FormattedEntries { nodes: mut n, properties: mut p } =
                tree.properties.get(property_id).map_or(
                    FormattedEntries {
                        nodes: vec![],
                        properties: vec!["Missing property".to_string()],
                    },
                    |p| p.format_entries(&sub_prefix),
                );
            properties.append(&mut p);
            nodes.append(&mut n);
        }

        nodes.sort_unstable();
        properties.sort_unstable();

        let mut output_lines = vec![];

        if self.name != ROOT_NAME || !hide_root {
            output_lines.push(format!("{}{} ->", prefix, self.name));
        }
        output_lines.append(&mut properties);
        output_lines.append(&mut nodes);

        output_lines.join("\n")
    }
}

struct Op {
    int: fn(i64, i64) -> i64,
    uint: fn(u64, u64) -> u64,
    double: fn(f64, f64) -> f64,
    name: &'static str,
}

const ADD: Op = Op { int: |a, b| a + b, uint: |a, b| a + b, double: |a, b| a + b, name: "add" };
const SUBTRACT: Op =
    Op { int: |a, b| a - b, uint: |a, b| a - b, double: |a, b| a - b, name: "subtract" };
const SET: Op = Op { int: |_a, b| b, uint: |_a, b| b, double: |_a, b| b, name: "set" };

macro_rules! insert_linear_fn {
    ($name:ident, $type:ident) => {
        fn $name(numbers: &mut Vec<$type>, value: $type, count: u64) -> Result<(), Error> {
            let buckets: $type = (numbers.len() as i32 - 4).try_into().unwrap();
            let floor = numbers[0];
            let step_size = numbers[1];
            let index: usize = if value < floor {
                2
            } else if value >= floor + buckets * step_size {
                numbers.len() - 1
            } else {
                (((value - floor) / step_size) as $type + 3 as $type) as i32 as usize
            };
            numbers[index] += count as $type;
            Ok(())
        }
    };
}

insert_linear_fn! {insert_linear_i, i64}
insert_linear_fn! {insert_linear_u, u64}
insert_linear_fn! {insert_linear_d, f64}

// DO NOT USE this algorithm in non-test libraries!
// It's good to implement the test with a different algorithm than the code being tested.
// But this is a BAD algorithm in real life.
// 1) Too many casts - extreme values may not be handled correctly.
// 2) Floating point math is imprecise; int/uint values over 2^56 or so won't be
//     calculated correctly because they can't be expressed precisely, and the log2/log2
//     division may come down on the wrong side of the bucket boundary. That's why there's
//     a fudge factor added to int results - but that's only correct up to a million or so.
macro_rules! insert_exponential_fn {
    ($name:ident, $type:ident, $fudge_factor:expr) => {
        fn $name(numbers: &mut Vec<$type>, value: $type, count: u64) -> Result<(), Error> {
            let buckets = numbers.len() - 5;
            let floor = numbers[0];
            let initial_step = numbers[1];
            let step_multiplier = numbers[2];
            let index = if value < floor {
                3
            } else if value < floor + initial_step {
                4
            } else if value
                >= floor + initial_step * (step_multiplier as f64).powi(buckets as i32 - 1) as $type
            {
                numbers.len() - 1
            } else {
                ((((value as f64 - floor as f64) / initial_step as f64) as f64).log2()
                    / (step_multiplier as f64 + $fudge_factor).log2())
                .trunc() as usize
                    + 5
            };
            numbers[index] += count as $type;
            Ok(())
        }
    };
}

insert_exponential_fn! {insert_exponential_i, i64, 0.0000000000000000000001}
insert_exponential_fn! {insert_exponential_u, u64, 0.0000000000000000000001}
insert_exponential_fn! {insert_exponential_d, f64, 0.0}

impl Data {
    // ***** Here are the functions to apply Actions to a Data.

    /// Applies the given action to this in-memory state.
    pub fn apply(&mut self, action: &validate::Action) -> Result<(), Error> {
        match action {
            validate::Action::CreateNode(validate::CreateNode { parent, id, name }) => {
                self.create_node(*parent, *id, name)
            }
            validate::Action::DeleteNode(validate::DeleteNode { id }) => self.delete_node(*id),
            validate::Action::CreateNumericProperty(validate::CreateNumericProperty {
                parent,
                id,
                name,
                value,
            }) => self.add_property(
                *parent,
                *id,
                name,
                match value {
                    validate::Number::IntT(value) => Payload::Int(*value),
                    validate::Number::UintT(value) => Payload::Uint(*value),
                    validate::Number::DoubleT(value) => Payload::Double(*value),
                    unknown => return Err(format_err!("Unknown number type {:?}", unknown)),
                },
            ),
            validate::Action::CreateBytesProperty(validate::CreateBytesProperty {
                parent,
                id,
                name,
                value,
            }) => self.add_property(*parent, *id, name, Payload::Bytes(value.clone())),
            validate::Action::CreateStringProperty(validate::CreateStringProperty {
                parent,
                id,
                name,
                value,
            }) => self.add_property(*parent, *id, name, Payload::String(value.to_string())),
            validate::Action::CreateBoolProperty(validate::CreateBoolProperty {
                parent,
                id,
                name,
                value,
            }) => self.add_property(*parent, *id, name, Payload::Bool(*value)),
            validate::Action::DeleteProperty(validate::DeleteProperty { id }) => {
                self.delete_property(*id)
            }
            validate::Action::SetNumber(validate::SetNumber { id, value }) => {
                self.modify_number(*id, value, SET)
            }
            validate::Action::AddNumber(validate::AddNumber { id, value }) => {
                self.modify_number(*id, value, ADD)
            }
            validate::Action::SubtractNumber(validate::SubtractNumber { id, value }) => {
                self.modify_number(*id, value, SUBTRACT)
            }
            validate::Action::SetBytes(validate::SetBytes { id, value }) => {
                self.set_bytes(*id, value)
            }
            validate::Action::SetString(validate::SetString { id, value }) => {
                self.set_string(*id, value)
            }
            validate::Action::SetBool(validate::SetBool { id, value }) => {
                self.set_bool(*id, *value)
            }
            validate::Action::CreateArrayProperty(validate::CreateArrayProperty {
                parent,
                id,
                name,
                slots,
                number_type,
            }) => self.add_property(
                *parent,
                *id,
                name,
                match number_type {
                    validate::NumberType::Int => {
                        Payload::IntArray(vec![0; *slots as usize], ArrayFormat::Default)
                    }
                    validate::NumberType::Uint => {
                        Payload::UintArray(vec![0; *slots as usize], ArrayFormat::Default)
                    }
                    validate::NumberType::Double => {
                        Payload::DoubleArray(vec![0.0; *slots as usize], ArrayFormat::Default)
                    }
                },
            ),
            validate::Action::ArrayAdd(validate::ArrayAdd { id, index, value }) => {
                self.modify_array(*id, *index, value, ADD)
            }
            validate::Action::ArraySubtract(validate::ArraySubtract { id, index, value }) => {
                self.modify_array(*id, *index, value, SUBTRACT)
            }
            validate::Action::ArraySet(validate::ArraySet { id, index, value }) => {
                self.modify_array(*id, *index, value, SET)
            }
            validate::Action::CreateLinearHistogram(validate::CreateLinearHistogram {
                parent,
                id,
                name,
                floor,
                step_size,
                buckets,
            }) => self.add_property(
                *parent,
                *id,
                name,
                match (floor, step_size) {
                    (validate::Number::IntT(floor), validate::Number::IntT(step_size)) => {
                        let mut data = vec![0; *buckets as usize + 4];
                        data[0] = *floor;
                        data[1] = *step_size;
                        Payload::IntArray(data, ArrayFormat::LinearHistogram)
                    }
                    (validate::Number::UintT(floor), validate::Number::UintT(step_size)) => {
                        let mut data = vec![0; *buckets as usize + 4];
                        data[0] = *floor;
                        data[1] = *step_size;
                        Payload::UintArray(data, ArrayFormat::LinearHistogram)
                    }
                    (validate::Number::DoubleT(floor), validate::Number::DoubleT(step_size)) => {
                        let mut data = vec![0.0; *buckets as usize + 4];
                        data[0] = *floor;
                        data[1] = *step_size;
                        Payload::DoubleArray(data, ArrayFormat::LinearHistogram)
                    }
                    unexpected => {
                        return Err(format_err!(
                            "Bad types in CreateLinearHistogram: {:?}",
                            unexpected
                        ))
                    }
                },
            ),
            validate::Action::CreateExponentialHistogram(
                validate::CreateExponentialHistogram {
                    parent,
                    id,
                    name,
                    floor,
                    initial_step,
                    step_multiplier,
                    buckets,
                },
            ) => self.add_property(
                *parent,
                *id,
                name,
                match (floor, initial_step, step_multiplier) {
                    (
                        validate::Number::IntT(floor),
                        validate::Number::IntT(initial_step),
                        validate::Number::IntT(step_multiplier),
                    ) => {
                        let mut data = vec![0i64; *buckets as usize + 5];
                        data[0] = *floor;
                        data[1] = *initial_step;
                        data[2] = *step_multiplier;
                        Payload::IntArray(data, ArrayFormat::ExponentialHistogram)
                    }
                    (
                        validate::Number::UintT(floor),
                        validate::Number::UintT(initial_step),
                        validate::Number::UintT(step_multiplier),
                    ) => {
                        let mut data = vec![0u64; *buckets as usize + 5];
                        data[0] = *floor;
                        data[1] = *initial_step;
                        data[2] = *step_multiplier;
                        Payload::UintArray(data, ArrayFormat::ExponentialHistogram)
                    }
                    (
                        validate::Number::DoubleT(floor),
                        validate::Number::DoubleT(initial_step),
                        validate::Number::DoubleT(step_multiplier),
                    ) => {
                        let mut data = vec![0.0f64; *buckets as usize + 5];
                        data[0] = *floor;
                        data[1] = *initial_step;
                        data[2] = *step_multiplier;
                        Payload::DoubleArray(data, ArrayFormat::ExponentialHistogram)
                    }
                    unexpected => {
                        return Err(format_err!(
                            "Bad types in CreateExponentialHistogram: {:?}",
                            unexpected
                        ))
                    }
                },
            ),
            validate::Action::Insert(validate::Insert { id, value }) => {
                if let Some(mut property) = self.properties.get_mut(&id) {
                    match (&mut property, value) {
                        (
                            Property {
                                payload: Payload::IntArray(numbers, ArrayFormat::LinearHistogram),
                                ..
                            },
                            Number::IntT(value),
                        ) => insert_linear_i(numbers, *value, 1),
                        (
                            Property {
                                payload:
                                    Payload::IntArray(numbers, ArrayFormat::ExponentialHistogram),
                                ..
                            },
                            Number::IntT(value),
                        ) => insert_exponential_i(numbers, *value, 1),
                        (
                            Property {
                                payload: Payload::UintArray(numbers, ArrayFormat::LinearHistogram),
                                ..
                            },
                            Number::UintT(value),
                        ) => insert_linear_u(numbers, *value, 1),
                        (
                            Property {
                                payload:
                                    Payload::UintArray(numbers, ArrayFormat::ExponentialHistogram),
                                ..
                            },
                            Number::UintT(value),
                        ) => insert_exponential_u(numbers, *value, 1),
                        (
                            Property {
                                payload: Payload::DoubleArray(numbers, ArrayFormat::LinearHistogram),
                                ..
                            },
                            Number::DoubleT(value),
                        ) => insert_linear_d(numbers, *value, 1),
                        (
                            Property {
                                payload:
                                    Payload::DoubleArray(numbers, ArrayFormat::ExponentialHistogram),
                                ..
                            },
                            Number::DoubleT(value),
                        ) => insert_exponential_d(numbers, *value, 1),
                        unexpected => {
                            return Err(format_err!(
                                "Type mismatch {:?} trying to insert",
                                unexpected
                            ))
                        }
                    }
                } else {
                    return Err(format_err!(
                        "Tried to insert number on nonexistent property {}",
                        id
                    ));
                }
            }
            validate::Action::InsertMultiple(validate::InsertMultiple { id, value, count }) => {
                if let Some(mut property) = self.properties.get_mut(&id) {
                    match (&mut property, value) {
                        (
                            Property {
                                payload: Payload::IntArray(numbers, ArrayFormat::LinearHistogram),
                                ..
                            },
                            Number::IntT(value),
                        ) => insert_linear_i(numbers, *value, *count),
                        (
                            Property {
                                payload:
                                    Payload::IntArray(numbers, ArrayFormat::ExponentialHistogram),
                                ..
                            },
                            Number::IntT(value),
                        ) => insert_exponential_i(numbers, *value, *count),
                        (
                            Property {
                                payload: Payload::UintArray(numbers, ArrayFormat::LinearHistogram),
                                ..
                            },
                            Number::UintT(value),
                        ) => insert_linear_u(numbers, *value, *count),
                        (
                            Property {
                                payload:
                                    Payload::UintArray(numbers, ArrayFormat::ExponentialHistogram),
                                ..
                            },
                            Number::UintT(value),
                        ) => insert_exponential_u(numbers, *value, *count),
                        (
                            Property {
                                payload: Payload::DoubleArray(numbers, ArrayFormat::LinearHistogram),
                                ..
                            },
                            Number::DoubleT(value),
                        ) => insert_linear_d(numbers, *value, *count),
                        (
                            Property {
                                payload:
                                    Payload::DoubleArray(numbers, ArrayFormat::ExponentialHistogram),
                                ..
                            },
                            Number::DoubleT(value),
                        ) => insert_exponential_d(numbers, *value, *count),
                        unexpected => {
                            return Err(format_err!(
                                "Type mismatch {:?} trying to insert multiple",
                                unexpected
                            ))
                        }
                    }
                } else {
                    return Err(format_err!(
                        "Tried to insert_multiple number on nonexistent property {}",
                        id
                    ));
                }
            }
            _ => return Err(format_err!("Unknown action {:?}", action)),
        }
    }

    pub fn apply_lazy(&mut self, lazy_action: &validate::LazyAction) -> Result<(), Error> {
        match lazy_action {
            validate::LazyAction::CreateLazyNode(validate::CreateLazyNode {
                parent,
                id,
                name,
                disposition,
                actions,
            }) => self.add_lazy_node(*parent, *id, name, disposition, actions),
            validate::LazyAction::DeleteLazyNode(validate::DeleteLazyNode { id }) => {
                self.delete_property(*id)
            }
            _ => Err(format_err!("Unknown lazy action {:?}", lazy_action)),
        }
    }

    fn create_node(&mut self, parent: u32, id: u32, name: &str) -> Result<(), Error> {
        let node = Node {
            name: name.to_owned(),
            parent,
            children: HashSet::new(),
            properties: HashSet::new(),
        };
        if self.tombstone_nodes.contains(&id) {
            return Err(format_err!("Tried to create implicitly deleted node {}", id));
        }
        if let Some(_) = self.nodes.insert(id, node) {
            return Err(format_err!("Create called when node already existed at {}", id));
        }
        if let Some(parent_node) = self.nodes.get_mut(&parent) {
            parent_node.children.insert(id);
        } else {
            return Err(format_err!("Parent {} of created node {} doesn't exist", parent, id));
        }
        Ok(())
    }

    fn delete_node(&mut self, id: u32) -> Result<(), Error> {
        if id == 0 {
            return Err(format_err!("Do not try to delete node 0"));
        }
        if self.tombstone_nodes.remove(&id) {
            return Ok(());
        }
        if let Some(node) = self.nodes.remove(&id) {
            // Tombstone all descendents. An orphan descendent may reappear improperly if a new
            // node is created with a recycled ID.
            for child in node.children.clone().iter() {
                self.make_tombstone_node(*child)?;
            }
            for property in node.properties.clone().iter() {
                self.make_tombstone_property(*property)?;
            }
            if let Some(parent) = self.nodes.get_mut(&node.parent) {
                if !parent.children.remove(&id) {
                    // Some of these can only happen in case of internal logic errors.
                    // I can't think of a way to test them; I think the errors are
                    // actually impossible. Should I leave them untested? Remove them
                    // from the code? Add a special test_cfg make_illegal_node()
                    // function just to test them?
                    bail!(
                        "Internal error! Parent {} didn't know about this child {}",
                        node.parent,
                        id
                    );
                }
            }
        } else {
            return Err(format_err!("Delete of nonexistent node {}", id));
        }
        Ok(())
    }

    fn make_tombstone_node(&mut self, id: u32) -> Result<(), Error> {
        if id == 0 {
            return Err(format_err!("Internal error! Do not try to delete node 0."));
        }
        if let Some(node) = self.nodes.remove(&id) {
            for child in node.children.clone().iter() {
                self.make_tombstone_node(*child)?;
            }
            for property in node.properties.clone().iter() {
                self.make_tombstone_property(*property)?;
            }
        } else {
            return Err(format_err!("Internal error! Tried to tombstone nonexistent node {}", id));
        }
        self.tombstone_nodes.insert(id);
        Ok(())
    }

    fn make_tombstone_property(&mut self, id: u32) -> Result<(), Error> {
        if let None = self.properties.remove(&id) {
            return Err(format_err!(
                "Internal error! Tried to tombstone nonexistent property {}",
                id
            ));
        }
        self.tombstone_properties.insert(id);
        Ok(())
    }

    fn add_property(
        &mut self,
        parent: u32,
        id: u32,
        name: &str,
        payload: Payload,
    ) -> Result<(), Error> {
        if let Some(node) = self.nodes.get_mut(&parent) {
            node.properties.insert(id);
        } else {
            return Err(format_err!("Parent {} of property {} not found", parent, id));
        }
        if self.tombstone_properties.contains(&id) {
            return Err(format_err!("Tried to create implicitly deleted property {}", id));
        }
        let property = Property { parent, id, name: name.into(), payload };
        if let Some(_) = self.properties.insert(id, property) {
            return Err(format_err!("Property insert called on existing id {}", id));
        }
        Ok(())
    }

    fn delete_property(&mut self, id: u32) -> Result<(), Error> {
        if self.tombstone_properties.remove(&id) {
            return Ok(());
        }
        if let Some(property) = self.properties.remove(&id) {
            if let Some(node) = self.nodes.get_mut(&property.parent) {
                if !node.properties.remove(&id) {
                    bail!(
                        "Internal error! Property {}'s parent {} didn't have it as child",
                        id,
                        property.parent
                    );
                }
            } else {
                bail!(
                    "Internal error! Property {}'s parent {} doesn't exist on delete",
                    id,
                    property.parent
                );
            }
        } else {
            return Err(format_err!("Delete of nonexistent property {}", id));
        }
        Ok(())
    }

    fn modify_number(&mut self, id: u32, value: &validate::Number, op: Op) -> Result<(), Error> {
        if let Some(property) = self.properties.get_mut(&id) {
            match (&property, value) {
                (Property { payload: Payload::Int(old), .. }, Number::IntT(value)) => {
                    property.payload = Payload::Int((op.int)(*old, *value));
                }
                (Property { payload: Payload::Uint(old), .. }, Number::UintT(value)) => {
                    property.payload = Payload::Uint((op.uint)(*old, *value));
                }
                (Property { payload: Payload::Double(old), .. }, Number::DoubleT(value)) => {
                    property.payload = Payload::Double((op.double)(*old, *value));
                }
                unexpected => {
                    return Err(format_err!("Bad types {:?} trying to set number", unexpected))
                }
            }
        } else {
            return Err(format_err!("Tried to {} number on nonexistent property {}", op.name, id));
        }
        Ok(())
    }

    fn check_index<T>(numbers: &Vec<T>, index: usize) -> Result<(), Error> {
        if index >= numbers.len() as usize {
            return Err(format_err!("Index {} too big for vector length {}", index, numbers.len()));
        }
        Ok(())
    }

    fn modify_array(
        &mut self,
        id: u32,
        index64: u64,
        value: &validate::Number,
        op: Op,
    ) -> Result<(), Error> {
        if let Some(mut property) = self.properties.get_mut(&id) {
            let index = index64 as usize;
            // Out of range index is a NOP, not an error.
            let number_len = match &property {
                Property { payload: Payload::IntArray(numbers, ArrayFormat::Default), .. } => {
                    numbers.len()
                }
                Property { payload: Payload::UintArray(numbers, ArrayFormat::Default), .. } => {
                    numbers.len()
                }
                Property {
                    payload: Payload::DoubleArray(numbers, ArrayFormat::Default), ..
                } => numbers.len(),
                unexpected => {
                    return Err(format_err!("Bad types {:?} trying to set number", unexpected))
                }
            };
            if index >= number_len {
                return Ok(());
            }
            match (&mut property, value) {
                (Property { payload: Payload::IntArray(numbers, _), .. }, Number::IntT(value)) => {
                    Self::check_index(numbers, index)?;
                    numbers[index] = (op.int)(numbers[index], *value);
                }
                (
                    Property { payload: Payload::UintArray(numbers, _), .. },
                    Number::UintT(value),
                ) => {
                    Self::check_index(numbers, index)?;
                    numbers[index] = (op.uint)(numbers[index], *value);
                }
                (
                    Property { payload: Payload::DoubleArray(numbers, _), .. },
                    Number::DoubleT(value),
                ) => {
                    Self::check_index(numbers, index)?;
                    numbers[index] = (op.double)(numbers[index], *value);
                }
                unexpected => {
                    return Err(format_err!("Type mismatch {:?} trying to set number", unexpected))
                }
            }
        } else {
            return Err(format_err!("Tried to {} number on nonexistent property {}", op.name, id));
        }
        Ok(())
    }

    fn set_string(&mut self, id: u32, value: &String) -> Result<(), Error> {
        if let Some(property) = self.properties.get_mut(&id) {
            match &property {
                Property { payload: Payload::String(_), .. } => {
                    property.payload = Payload::String(value.to_owned())
                }
                unexpected => {
                    return Err(format_err!("Bad type {:?} trying to set string", unexpected))
                }
            }
        } else {
            return Err(format_err!("Tried to set string on nonexistent property {}", id));
        }
        Ok(())
    }

    fn set_bytes(&mut self, id: u32, value: &Vec<u8>) -> Result<(), Error> {
        if let Some(property) = self.properties.get_mut(&id) {
            match &property {
                Property { payload: Payload::Bytes(_), .. } => {
                    property.payload = Payload::Bytes(value.to_owned())
                }
                unexpected => {
                    return Err(format_err!("Bad type {:?} trying to set bytes", unexpected))
                }
            }
        } else {
            return Err(format_err!("Tried to set bytes on nonexistent property {}", id));
        }
        Ok(())
    }

    fn set_bool(&mut self, id: u32, value: bool) -> Result<(), Error> {
        if let Some(property) = self.properties.get_mut(&id) {
            match &property {
                Property { payload: Payload::Bool(_), .. } => {
                    property.payload = Payload::Bool(value)
                }
                unexpected => {
                    return Err(format_err!("Bad type {:?} trying to set bool", unexpected))
                }
            }
        } else {
            return Err(format_err!("Tried to set bool on nonexistent property {}", id));
        }
        Ok(())
    }

    fn add_lazy_node(
        &mut self,
        parent: u32,
        id: u32,
        name: &str,
        disposition: &validate::LinkDisposition,
        actions: &Vec<validate::Action>,
    ) -> Result<(), Error> {
        let mut parsed_data = Data::new();
        parsed_data.apply_multiple(&actions)?;
        self.add_property(
            parent,
            id,
            name,
            Payload::Link {
                disposition: match disposition {
                    validate::LinkDisposition::Child => LinkNodeDisposition::Child,
                    validate::LinkDisposition::Inline => LinkNodeDisposition::Inline,
                },
                parsed_data,
            },
        )?;
        Ok(())
    }

    // ***** Here are the functions to compare two Data (by converting to a
    // ***** fully informative string).

    /// Make a clone of this Data with all properties replaced with their generic version.
    fn clone_generic(&self) -> Self {
        let mut clone = self.clone();

        let mut to_remove = vec![];
        let mut names = HashSet::new();

        clone.properties = clone
            .properties
            .into_iter()
            .filter_map(|(id, mut v)| {
                // We do not support duplicate property names within a single node in our JSON
                // output.
                // Delete one of the nodes from the tree.
                //
                // Note: This can cause errors if the children do not have the same properties.
                if !names.insert((v.parent, v.name.clone())) {
                    to_remove.push((v.parent, id));
                    None
                } else {
                    v.payload = v.payload.to_generic();
                    Some((id, v))
                }
            })
            .collect::<HashMap<u32, Property>>();

        // Clean up removed properties.
        for (parent, id) in to_remove {
            if clone.nodes.contains_key(&parent) {
                clone.nodes.get_mut(&parent).unwrap().properties.remove(&id);
            }
        }

        clone
    }

    /// Compare this data with data that should be equivalent but was parsed from JSON.
    ///
    /// This method tweaks some types to deal with JSON representation of the data, which is not as
    /// precise as the Inspect format itself.
    pub fn compare_to_json(&self, other: &Data, diff_type: DiffType) -> Result<(), Error> {
        self.clone_generic().compare(other, diff_type)
    }

    /// Compares two in-memory Inspect trees, returning Ok(()) if they have the
    /// same data and an Err<> if they are different. The string in the Err<>
    /// may be very large.
    pub fn compare(&self, other: &Data, diff_type: DiffType) -> Result<(), Error> {
        let self_string = self.to_string();
        let other_string = other.to_string();

        let difference::Changeset { diffs, distance, .. } =
            difference::Changeset::new(&self_string, &other_string, "\n");

        if distance == 0 {
            Ok(())
        } else {
            let diff_lines = diffs
                .into_iter()
                .flat_map(|diff| {
                    let (prefix, val) = match diff {
                        // extra space so that all ':'s in output are aligned
                        difference::Difference::Same(val) => (" same", val),
                        difference::Difference::Add(val) => ("other", val),
                        difference::Difference::Rem(val) => ("local", val),
                    };
                    val.split("\n")
                        .map(|line| format!("{}: {:?}", prefix, line))
                        .collect::<Vec<_>>()
                })
                .collect::<Vec<_>>();

            match diff_type {
                DiffType::Full => Err(format_err!(
                    "Trees differ:\n-- LOCAL --\n{}\n-- OTHER --\n{}",
                    self_string,
                    other_string
                )),
                DiffType::Diff => {
                    Err(format_err!("Trees differ:\n-- DIFF --\n{}", diff_lines.join("\n")))
                }
                DiffType::Both => Err(format_err!(
                    "Trees differ:\n-- LOCAL --\n{}\n-- OTHER --\n{}\n-- DIFF --\n{}",
                    self_string,
                    other_string,
                    diff_lines.join("\n")
                )),
            }
        }
    }

    /// Generates a string fully representing the Inspect data.
    pub fn to_string(&self) -> String {
        self.to_string_internal(false)
    }

    /// This creates a new Data. Note that the standard "root" node of the VMO API
    /// corresponds to the index-0 node added here.
    pub fn new() -> Data {
        let mut ret = Data {
            nodes: HashMap::new(),
            properties: HashMap::new(),
            tombstone_nodes: HashSet::new(),
            tombstone_properties: HashSet::new(),
        };
        ret.nodes.insert(
            0,
            Node {
                name: ROOT_NAME.into(),
                parent: 0,
                children: HashSet::new(),
                properties: HashSet::new(),
            },
        );
        ret
    }

    fn build(nodes: HashMap<u32, Node>, properties: HashMap<u32, Property>) -> Data {
        Data {
            nodes,
            properties,
            tombstone_nodes: HashSet::new(),
            tombstone_properties: HashSet::new(),
        }
    }

    fn to_string_internal(&self, hide_root: bool) -> String {
        if let Some(node) = self.nodes.get(&ROOT_ID) {
            node.to_string(&"".to_owned(), self, hide_root)
        } else {
            "No root node; internal error\n".to_owned()
        }
    }

    fn apply_multiple(&mut self, actions: &Vec<validate::Action>) -> Result<(), Error> {
        for action in actions {
            self.apply(&action)?;
        }
        Ok(())
    }

    /// Return true if this data is not just an empty root node.
    pub fn is_empty(&self) -> bool {
        if !self.nodes.contains_key(&0) {
            // No root
            return true;
        }

        // There are issues with displaying a tree that has no properties.
        // TODO(fxbug.dev/49861): Support empty trees in archive.
        if self.properties.len() == 0 {
            return true;
        }

        // Root has no properties and any children it may have are tombstoned.
        let root = &self.nodes[&0];
        return root.children.is_subset(&self.tombstone_nodes) && root.properties.len() == 0;
    }
}

// There's no enum in fuchsia_inspect::format::block which contains only
// values that are valid for an ArrayType.
#[derive(Debug, PartialEq, Eq, FromPrimitive, ToPrimitive)]
enum ArrayType {
    Int = 4,
    Uint = 5,
    Double = 6,
}

impl From<NodeHierarchy> for Data {
    fn from(hierarchy: NodeHierarchy) -> Self {
        let mut nodes = HashMap::new();
        let mut properties = HashMap::new();

        nodes.insert(
            0u32,
            Node {
                name: hierarchy.name.clone(),
                parent: 0u32,
                children: HashSet::new(),
                properties: HashSet::new(),
            },
        );

        let mut queue = vec![(0u32, &hierarchy)];
        let mut next_id: u32 = 1;

        while let Some((id, value)) = queue.pop() {
            for ref node in value.children.iter() {
                let child_id = next_id;
                next_id += 1;
                nodes.insert(
                    child_id,
                    Node {
                        name: node.name.clone(),
                        parent: id,
                        children: HashSet::new(),
                        properties: HashSet::new(),
                    },
                );
                nodes.get_mut(&id).expect("parent must exist").children.insert(child_id);
                queue.push((child_id, node));
            }
            for property in value.properties.iter() {
                let prop_id = next_id;
                next_id += 1;

                let (name, payload) = match property.clone() {
                    iProperty::String(n, v) => (n, Payload::String(v).to_generic()),
                    iProperty::Bytes(n, v) => (n, Payload::Bytes(v).to_generic()),
                    iProperty::Int(n, v) => (n, Payload::Int(v).to_generic()),
                    iProperty::Uint(n, v) => (n, Payload::Uint(v).to_generic()),
                    iProperty::Double(n, v) => (n, Payload::Double(v).to_generic()),
                    iProperty::Bool(n, v) => (n, Payload::Bool(v).to_generic()),
                    iProperty::IntArray(n, content) => (n, content.to_generic()),
                    iProperty::UintArray(n, content) => (n, content.to_generic()),
                    iProperty::DoubleArray(n, content) => (n, content.to_generic()),
                };

                properties.insert(prop_id, Property { name, id: prop_id, parent: id, payload });
                nodes.get_mut(&id).expect("parent must exist").properties.insert(prop_id);
            }
        }

        Data {
            nodes,
            properties,
            tombstone_nodes: HashSet::new(),
            tombstone_properties: HashSet::new(),
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::*,
        fidl_test_inspect_validate::{Number, NumberType, ROOT_ID},
        fuchsia_inspect::{
            format::block_type::BlockType,
            reader::{ArrayContent as iArrayContent, ArrayFormat},
        },
    };

    #[test]
    fn test_basic_data_strings() -> Result<(), Error> {
        let mut info = Data::new();
        assert_eq!(info.to_string(), "root ->");

        info.apply(&create_node!(parent: ROOT_ID, id: 1, name: "foo"))?;
        assert_eq!(info.to_string(), "root ->\n> foo ->");

        info.apply(&delete_node!( id: 1 ))?;
        assert_eq!(info.to_string(), "root ->");

        Ok(())
    }

    const EXPECTED_HIERARCHY: &'static str = r#"root ->
> double: GenericNumber("2.5")
> int: GenericNumber("-5")
> string: String("value")
> uint: GenericNumber("10")
> child ->
> > bytes: String("b64:AQI=")
> > grandchild ->
> > > double_a: GenericArray(["0.5", "1"])
> > > double_eh: GenericHistogram(["1", "2", "3"])
> > > double_lh: GenericHistogram(["1", "2", "3"])
> > > int_a: GenericArray(["-1", "-2"])
> > > int_eh: GenericHistogram(["1", "2", "3"])
> > > int_lh: GenericHistogram(["1", "2", "3"])
> > > uint_a: GenericArray(["1", "2"])
> > > uint_eh: GenericHistogram(["1", "2", "3"])
> > > uint_lh: GenericHistogram(["1", "2", "3"])"#;

    #[test]
    fn test_parse_hierarchy() -> Result<(), Error> {
        let hierarchy = NodeHierarchy {
            name: "root".to_string(),
            properties: vec![
                iProperty::String("string".to_string(), "value".to_string()),
                iProperty::Uint("uint".to_string(), 10u64),
                iProperty::Int("int".to_string(), -5i64),
                iProperty::Double("double".to_string(), 2.5f64),
            ],
            children: vec![NodeHierarchy {
                name: "child".to_string(),
                properties: vec![iProperty::Bytes("bytes".to_string(), vec![1u8, 2u8])],
                children: vec![NodeHierarchy {
                    name: "grandchild".to_string(),
                    properties: vec![
                        iProperty::UintArray(
                            "uint_a".to_string(),
                            iArrayContent::Values(vec![1, 2]),
                        ),
                        iProperty::IntArray(
                            "int_a".to_string(),
                            iArrayContent::Values(vec![-1i64, -2i64]),
                        ),
                        iProperty::DoubleArray(
                            "double_a".to_string(),
                            iArrayContent::Values(vec![0.5, 1.0]),
                        ),
                        iProperty::UintArray(
                            "uint_lh".to_string(),
                            iArrayContent::new(vec![1, 1, 1, 2, 3], ArrayFormat::LinearHistogram)
                                .unwrap(),
                        ),
                        iProperty::IntArray(
                            "int_lh".to_string(),
                            iArrayContent::new(
                                vec![-1i64, 1, 1, 2, 3],
                                ArrayFormat::LinearHistogram,
                            )
                            .unwrap(),
                        ),
                        iProperty::DoubleArray(
                            "double_lh".to_string(),
                            iArrayContent::new(
                                vec![0.5, 0.5, 1.0, 2.0, 3.0],
                                ArrayFormat::LinearHistogram,
                            )
                            .unwrap(),
                        ),
                        iProperty::UintArray(
                            "uint_eh".to_string(),
                            iArrayContent::new(
                                vec![1, 1, 2, 1, 2, 3],
                                ArrayFormat::ExponentialHistogram,
                            )
                            .unwrap(),
                        ),
                        iProperty::IntArray(
                            "int_eh".to_string(),
                            iArrayContent::new(
                                vec![-1i64, 1, 2, 1, 2, 3],
                                ArrayFormat::ExponentialHistogram,
                            )
                            .unwrap(),
                        ),
                        iProperty::DoubleArray(
                            "double_eh".to_string(),
                            iArrayContent::new(
                                vec![0.5, 0.5, 2.0, 1.0, 2.0, 3.0],
                                ArrayFormat::ExponentialHistogram,
                            )
                            .unwrap(),
                        ),
                    ],
                    children: vec![],
                    missing: vec![],
                }],
                missing: vec![],
            }],
            missing: vec![],
        };

        let data: Data = hierarchy.into();
        assert_eq!(EXPECTED_HIERARCHY, data.to_string());

        Ok(())
    }

    // Make sure every action correctly modifies the string representation of the data tree.
    #[test]
    fn test_creation_deletion() -> Result<(), Error> {
        let mut info = Data::new();
        assert!(!info.to_string().contains("child ->"));
        info.apply(&create_node!(parent: ROOT_ID, id: 1, name: "child"))?;
        assert!(info.to_string().contains("child ->"));

        info.apply(&create_node!(parent: 1, id: 2, name: "grandchild"))?;
        assert!(
            info.to_string().contains("grandchild ->") && info.to_string().contains("child ->")
        );

        info.apply(
            &create_numeric_property!(parent: ROOT_ID, id: 3, name: "int-42", value: Number::IntT(-42)),
        )?;

        assert!(info.to_string().contains("int-42: Int(-42)")); // Make sure it can hold negative #
        info.apply(&create_string_property!(parent: 1, id: 4, name: "stringfoo", value: "foo"))?;
        assert_eq!(
            info.to_string(),
            "root ->\n> int-42: Int(-42)\n> child ->\
             \n> > stringfoo: String(\"foo\")\n> > grandchild ->"
        );

        info.apply(&create_numeric_property!(parent: ROOT_ID, id: 5, name: "uint", value: Number::UintT(1024)))?;
        assert!(info.to_string().contains("uint: Uint(1024)"));

        info.apply(&create_numeric_property!(parent: ROOT_ID, id: 6, name: "frac", value: Number::DoubleT(0.5)))?;
        assert!(info.to_string().contains("frac: Double(0.5)"));

        info.apply(
            &create_bytes_property!(parent: ROOT_ID, id: 7, name: "bytes", value: vec!(1u8, 2u8)),
        )?;
        assert!(info.to_string().contains("bytes: Bytes([1, 2])"));

        info.apply(&create_array_property!(parent: ROOT_ID, id: 8, name: "i_ntarr", slots: 1, type: NumberType::Int))?;
        assert!(info.to_string().contains("i_ntarr: IntArray([0], Default)"));

        info.apply(&create_array_property!(parent: ROOT_ID, id: 9, name: "u_intarr", slots: 2, type: NumberType::Uint))?;
        assert!(info.to_string().contains("u_intarr: UintArray([0, 0], Default)"));

        info.apply(&create_array_property!(parent: ROOT_ID, id: 10, name: "dblarr", slots: 3, type: NumberType::Double))?;
        assert!(info.to_string().contains("dblarr: DoubleArray([0.0, 0.0, 0.0], Default)"));

        info.apply(&create_linear_histogram!(parent: ROOT_ID, id: 11, name: "ILhist", floor: 12,
            step_size: 3, buckets: 2, type: IntT))?;
        assert!(info
            .to_string()
            .contains("ILhist: IntArray([12, 3, 0, 0, 0, 0], LinearHistogram)"));

        info.apply(&create_linear_histogram!(parent: ROOT_ID, id: 12, name: "ULhist", floor: 34,
            step_size: 5, buckets: 2, type: UintT))?;
        assert!(info
            .to_string()
            .contains("ULhist: UintArray([34, 5, 0, 0, 0, 0], LinearHistogram)"));

        info.apply(
            &create_linear_histogram!(parent: ROOT_ID, id: 13, name: "DLhist", floor: 56.0,
            step_size: 7.0, buckets: 2, type: DoubleT),
        )?;
        assert!(info
            .to_string()
            .contains("DLhist: DoubleArray([56.0, 7.0, 0.0, 0.0, 0.0, 0.0], LinearHistogram)"));

        info.apply(&create_exponential_histogram!(parent: ROOT_ID, id: 14, name: "IEhist",
            floor: 12, initial_step: 3, step_multiplier: 5, buckets: 2, type: IntT))?;
        assert!(info
            .to_string()
            .contains("IEhist: IntArray([12, 3, 5, 0, 0, 0, 0], ExponentialHistogram)"));

        info.apply(&create_exponential_histogram!(parent: ROOT_ID, id: 15, name: "UEhist",
            floor: 34, initial_step: 9, step_multiplier: 6, buckets: 2, type: UintT))?;
        assert!(info
            .to_string()
            .contains("UEhist: UintArray([34, 9, 6, 0, 0, 0, 0], ExponentialHistogram)"));

        info.apply(&create_exponential_histogram!(parent: ROOT_ID, id: 16, name: "DEhist",
            floor: 56.0, initial_step: 27.0, step_multiplier: 7.0, buckets: 2, type: DoubleT))?;
        assert!(info.to_string().contains(
            "DEhist: DoubleArray([56.0, 27.0, 7.0, 0.0, 0.0, 0.0, 0.0], ExponentialHistogram)"
        ));

        info.apply(&create_bool_property!(parent: ROOT_ID, id: 17, name: "bool", value: true))?;
        assert!(info.to_string().contains("bool: Bool(true)"));

        info.apply(&delete_property!(id: 3))?;
        assert!(!info.to_string().contains("int-42") && info.to_string().contains("stringfoo"));
        info.apply(&delete_property!(id: 4))?;
        assert!(!info.to_string().contains("stringfoo"));
        info.apply(&delete_property!(id: 5))?;
        assert!(!info.to_string().contains("uint"));
        info.apply(&delete_property!(id: 6))?;
        assert!(!info.to_string().contains("frac"));
        info.apply(&delete_property!(id: 7))?;
        assert!(!info.to_string().contains("bytes"));
        info.apply(&delete_property!(id: 8))?;
        assert!(!info.to_string().contains("i_ntarr"));
        info.apply(&delete_property!(id: 9))?;
        assert!(!info.to_string().contains("u_intarr"));
        info.apply(&delete_property!(id: 10))?;
        assert!(!info.to_string().contains("dblarr"));
        info.apply(&delete_property!(id: 11))?;
        assert!(!info.to_string().contains("ILhist"));
        info.apply(&delete_property!(id: 12))?;
        assert!(!info.to_string().contains("ULhist"));
        info.apply(&delete_property!(id: 13))?;
        assert!(!info.to_string().contains("DLhist"));
        info.apply(&delete_property!(id: 14))?;
        assert!(!info.to_string().contains("IEhist"));
        info.apply(&delete_property!(id: 15))?;
        assert!(!info.to_string().contains("UEhist"));
        info.apply(&delete_property!(id: 16))?;
        assert!(!info.to_string().contains("DEhist"));
        info.apply(&delete_property!(id: 17))?;
        assert!(!info.to_string().contains("bool"));
        info.apply(&delete_node!(id:2))?;
        assert!(!info.to_string().contains("grandchild") && info.to_string().contains("child"));
        info.apply(&delete_node!( id: 1 ))?;
        assert_eq!(info.to_string(), "root ->");
        Ok(())
    }

    #[test]
    fn test_basic_int_ops() -> Result<(), Error> {
        let mut info = Data::new();
        info.apply(&create_numeric_property!(parent: ROOT_ID, id: 3, name: "value",
                                     value: Number::IntT(-42)))?;
        assert!(info.apply(&add_number!(id: 3, value: Number::IntT(3))).is_ok());
        assert!(info.to_string().contains("value: Int(-39)"));
        assert!(info.apply(&add_number!(id: 3, value: Number::UintT(3))).is_err());
        assert!(info.apply(&add_number!(id: 3, value: Number::DoubleT(3.0))).is_err());
        assert!(info.to_string().contains("value: Int(-39)"));
        assert!(info.apply(&subtract_number!(id: 3, value: Number::IntT(5))).is_ok());
        assert!(info.to_string().contains("value: Int(-44)"));
        assert!(info.apply(&subtract_number!(id: 3, value: Number::UintT(5))).is_err());
        assert!(info.apply(&subtract_number!(id: 3, value: Number::DoubleT(5.0))).is_err());
        assert!(info.to_string().contains("value: Int(-44)"));
        assert!(info.apply(&set_number!(id: 3, value: Number::IntT(22))).is_ok());
        assert!(info.to_string().contains("value: Int(22)"));
        assert!(info.apply(&set_number!(id: 3, value: Number::UintT(23))).is_err());
        assert!(info.apply(&set_number!(id: 3, value: Number::DoubleT(24.0))).is_err());
        assert!(info.to_string().contains("value: Int(22)"));
        Ok(())
    }

    #[test]
    fn test_array_int_ops() -> Result<(), Error> {
        let mut info = Data::new();
        info.apply(&create_array_property!(parent: ROOT_ID, id: 3, name: "value", slots: 3,
                                           type: NumberType::Int))?;
        assert!(info.apply(&array_add!(id: 3, index: 1, value: Number::IntT(3))).is_ok());
        assert!(info.to_string().contains("value: IntArray([0, 3, 0], Default)"));
        assert!(info.apply(&array_add!(id: 3, index: 1, value: Number::UintT(3))).is_err());
        assert!(info.apply(&array_add!(id: 3, index: 1, value: Number::DoubleT(3.0))).is_err());
        assert!(info.to_string().contains("value: IntArray([0, 3, 0], Default)"));
        assert!(info.apply(&array_subtract!(id: 3, index: 2, value: Number::IntT(5))).is_ok());
        assert!(info.to_string().contains("value: IntArray([0, 3, -5], Default)"));
        assert!(info.apply(&array_subtract!(id: 3, index: 2, value: Number::UintT(5))).is_err());
        assert!(info
            .apply(&array_subtract!(id: 3, index: 2,
                                            value: Number::DoubleT(5.0)))
            .is_err());
        assert!(info.to_string().contains("value: IntArray([0, 3, -5], Default)"));
        assert!(info.apply(&array_set!(id: 3, index: 1, value: Number::IntT(22))).is_ok());
        assert!(info.to_string().contains("value: IntArray([0, 22, -5], Default)"));
        assert!(info.apply(&array_set!(id: 3, index: 1, value: Number::UintT(23))).is_err());
        assert!(info.apply(&array_set!(id: 3, index: 1, value: Number::DoubleT(24.0))).is_err());
        assert!(info.to_string().contains("value: IntArray([0, 22, -5], Default)"));
        Ok(())
    }

    #[test]
    fn test_linear_int_ops() -> Result<(), Error> {
        let mut info = Data::new();
        info.apply(&create_linear_histogram!(parent: ROOT_ID, id: 3, name: "value",
                    floor: 4, step_size: 2, buckets: 2, type: IntT))?;
        assert!(info.to_string().contains("value: IntArray([4, 2, 0, 0, 0, 0], LinearHistogram)"));
        assert!(info.apply(&insert!(id: 3, value: Number::IntT(4))).is_ok());
        assert!(info.to_string().contains("value: IntArray([4, 2, 0, 1, 0, 0], LinearHistogram)"));
        assert!(info.apply(&insert!(id: 3, value: Number::IntT(5))).is_ok());
        assert!(info.to_string().contains("value: IntArray([4, 2, 0, 2, 0, 0], LinearHistogram)"));
        assert!(info.apply(&insert!(id: 3, value: Number::IntT(6))).is_ok());
        assert!(info.to_string().contains("value: IntArray([4, 2, 0, 2, 1, 0], LinearHistogram)"));
        assert!(info.apply(&insert!(id: 3, value: Number::IntT(8))).is_ok());
        assert!(info.to_string().contains("value: IntArray([4, 2, 0, 2, 1, 1], LinearHistogram)"));
        assert!(info.apply(&insert!(id: 3, value: Number::IntT(std::i64::MAX))).is_ok());
        assert!(info.to_string().contains("value: IntArray([4, 2, 0, 2, 1, 2], LinearHistogram)"));
        assert!(info.apply(&insert!(id: 3, value: Number::IntT(0))).is_ok());
        assert!(info.to_string().contains("value: IntArray([4, 2, 1, 2, 1, 2], LinearHistogram)"));
        assert!(info.apply(&insert!(id: 3, value: Number::IntT(std::i64::MIN))).is_ok());
        assert!(info.to_string().contains("value: IntArray([4, 2, 2, 2, 1, 2], LinearHistogram)"));
        assert!(info.apply(&insert!(id: 3, value: Number::UintT(0))).is_err());
        assert!(info.apply(&insert!(id: 3, value: Number::DoubleT(0.0))).is_err());
        assert!(info.apply(&array_set!(id: 3, index: 1, value: Number::IntT(222))).is_err());
        assert!(info.to_string().contains("value: IntArray([4, 2, 2, 2, 1, 2], LinearHistogram)"));
        assert!(info.apply(&insert_multiple!(id: 3, value: Number::IntT(7), count: 4)).is_ok());
        assert!(info.to_string().contains("value: IntArray([4, 2, 2, 2, 5, 2], LinearHistogram)"));
        Ok(())
    }

    #[test]
    fn test_exponential_int_ops() -> Result<(), Error> {
        let mut info = Data::new();
        // Bucket boundaries are 5, 7, 13
        info.apply(&create_exponential_histogram!(parent: ROOT_ID, id: 3, name: "value",
                    floor: 5, initial_step: 2,
                    step_multiplier: 4, buckets: 2, type: IntT))?;
        assert!(info
            .to_string()
            .contains("value: IntArray([5, 2, 4, 0, 0, 0, 0], ExponentialHistogram)"));
        assert!(info.apply(&insert!(id: 3, value: Number::IntT(5))).is_ok());
        assert!(info
            .to_string()
            .contains("value: IntArray([5, 2, 4, 0, 1, 0, 0], ExponentialHistogram)"));
        assert!(info.apply(&insert!(id: 3, value: Number::IntT(6))).is_ok());
        assert!(info
            .to_string()
            .contains("value: IntArray([5, 2, 4, 0, 2, 0, 0], ExponentialHistogram)"));
        assert!(info.apply(&insert!(id: 3, value: Number::IntT(7))).is_ok());
        assert!(info
            .to_string()
            .contains("value: IntArray([5, 2, 4, 0, 2, 1, 0], ExponentialHistogram)"));
        assert!(info.apply(&insert!(id: 3, value: Number::IntT(13))).is_ok());
        assert!(info
            .to_string()
            .contains("value: IntArray([5, 2, 4, 0, 2, 1, 1], ExponentialHistogram)"));
        assert!(info.apply(&insert!(id: 3, value: Number::IntT(std::i64::MAX))).is_ok());
        assert!(info
            .to_string()
            .contains("value: IntArray([5, 2, 4, 0, 2, 1, 2], ExponentialHistogram)"));
        assert!(info.apply(&insert!(id: 3, value: Number::IntT(0))).is_ok());
        assert!(info
            .to_string()
            .contains("value: IntArray([5, 2, 4, 1, 2, 1, 2], ExponentialHistogram)"));
        assert!(info.apply(&insert!(id: 3, value: Number::IntT(std::i64::MIN))).is_ok());
        assert!(info
            .to_string()
            .contains("value: IntArray([5, 2, 4, 2, 2, 1, 2], ExponentialHistogram)"));
        assert!(info.apply(&insert!(id: 3, value: Number::UintT(0))).is_err());
        assert!(info.apply(&insert!(id: 3, value: Number::DoubleT(0.0))).is_err());
        assert!(info.apply(&array_set!(id: 3, index: 1, value: Number::IntT(222))).is_err());
        assert!(info
            .to_string()
            .contains("value: IntArray([5, 2, 4, 2, 2, 1, 2], ExponentialHistogram)"));
        assert!(info.apply(&insert_multiple!(id: 3, value: Number::IntT(12), count: 4)).is_ok());
        assert!(info
            .to_string()
            .contains("value: IntArray([5, 2, 4, 2, 2, 5, 2], ExponentialHistogram)"));
        Ok(())
    }

    #[test]
    fn test_array_out_of_bounds_nop() -> Result<(), Error> {
        // Accesses to indexes beyond the array are legal and should have no effect on the data.
        let mut info = Data::new();
        info.apply(&create_array_property!(parent: ROOT_ID, id: 3, name: "value", slots: 3,
                                           type: NumberType::Int))?;
        assert!(info.apply(&array_add!(id: 3, index: 1, value: Number::IntT(3))).is_ok());
        assert!(info.to_string().contains("value: IntArray([0, 3, 0], Default)"));
        assert!(info.apply(&array_add!(id: 3, index: 3, value: Number::IntT(3))).is_ok());
        assert!(info.apply(&array_add!(id: 3, index: 6, value: Number::IntT(3))).is_ok());
        assert!(info.apply(&array_add!(id: 3, index: 12345, value: Number::IntT(3))).is_ok());
        assert!(info.to_string().contains("value: IntArray([0, 3, 0], Default)"));
        Ok(())
    }

    #[test]
    fn test_basic_uint_ops() -> Result<(), Error> {
        let mut info = Data::new();
        info.apply(&create_numeric_property!(parent: ROOT_ID, id: 3, name: "value",
                                     value: Number::UintT(42)))?;
        assert!(info.apply(&add_number!(id: 3, value: Number::UintT(3))).is_ok());
        assert!(info.to_string().contains("value: Uint(45)"));
        assert!(info.apply(&add_number!(id: 3, value: Number::IntT(3))).is_err());
        assert!(info.apply(&add_number!(id: 3, value: Number::DoubleT(3.0))).is_err());
        assert!(info.to_string().contains("value: Uint(45)"));
        assert!(info.apply(&subtract_number!(id: 3, value: Number::UintT(5))).is_ok());
        assert!(info.to_string().contains("value: Uint(40)"));
        assert!(info.apply(&subtract_number!(id: 3, value: Number::IntT(5))).is_err());
        assert!(info.apply(&subtract_number!(id: 3, value: Number::DoubleT(5.0))).is_err());
        assert!(info.to_string().contains("value: Uint(40)"));
        assert!(info.apply(&set_number!(id: 3, value: Number::UintT(22))).is_ok());
        assert!(info.to_string().contains("value: Uint(22)"));
        assert!(info.apply(&set_number!(id: 3, value: Number::IntT(23))).is_err());
        assert!(info.apply(&set_number!(id: 3, value: Number::DoubleT(24.0))).is_err());
        assert!(info.to_string().contains("value: Uint(22)"));
        Ok(())
    }

    #[test]
    fn test_array_uint_ops() -> Result<(), Error> {
        let mut info = Data::new();
        info.apply(&create_array_property!(parent: ROOT_ID, id: 3, name: "value", slots: 3,
                                     type: NumberType::Uint))?;
        assert!(info.apply(&array_add!(id: 3, index: 1, value: Number::UintT(3))).is_ok());
        assert!(info.to_string().contains("value: UintArray([0, 3, 0], Default)"));
        assert!(info.apply(&array_add!(id: 3, index: 1, value: Number::IntT(3))).is_err());
        assert!(info.apply(&array_add!(id: 3, index: 1, value: Number::DoubleT(3.0))).is_err());
        assert!(info.to_string().contains("value: UintArray([0, 3, 0], Default)"));
        assert!(info.apply(&array_set!(id: 3, index: 1, value: Number::UintT(22))).is_ok());
        assert!(info.to_string().contains("value: UintArray([0, 22, 0], Default)"));
        assert!(info.apply(&array_set!(id: 3, index: 1, value: Number::IntT(23))).is_err());
        assert!(info.apply(&array_set!(id: 3, index: 1, value: Number::DoubleT(24.0))).is_err());
        assert!(info.to_string().contains("value: UintArray([0, 22, 0], Default)"));
        assert!(info.apply(&array_subtract!(id: 3, index: 1, value: Number::UintT(5))).is_ok());
        assert!(info.to_string().contains("value: UintArray([0, 17, 0], Default)"));
        assert!(info.apply(&array_subtract!(id: 3, index: 1, value: Number::IntT(5))).is_err());
        assert!(info
            .apply(&array_subtract!(id: 3, index: 1, value: Number::DoubleT(5.0)))
            .is_err());
        assert!(info.to_string().contains("value: UintArray([0, 17, 0], Default)"));
        Ok(())
    }

    #[test]
    fn test_linear_uint_ops() -> Result<(), Error> {
        let mut info = Data::new();
        info.apply(&create_linear_histogram!(parent: ROOT_ID, id: 3, name: "value",
                    floor: 4, step_size: 2, buckets: 2, type: UintT))?;
        assert!(info.to_string().contains("value: UintArray([4, 2, 0, 0, 0, 0], LinearHistogram)"));
        assert!(info.apply(&insert!(id: 3, value: Number::UintT(4))).is_ok());
        assert!(info.to_string().contains("value: UintArray([4, 2, 0, 1, 0, 0], LinearHistogram)"));
        assert!(info.apply(&insert!(id: 3, value: Number::UintT(5))).is_ok());
        assert!(info.to_string().contains("value: UintArray([4, 2, 0, 2, 0, 0], LinearHistogram)"));
        assert!(info.apply(&insert!(id: 3, value: Number::UintT(6))).is_ok());
        assert!(info.to_string().contains("value: UintArray([4, 2, 0, 2, 1, 0], LinearHistogram)"));
        assert!(info.apply(&insert!(id: 3, value: Number::UintT(8))).is_ok());
        assert!(info.to_string().contains("value: UintArray([4, 2, 0, 2, 1, 1], LinearHistogram)"));
        assert!(info.apply(&insert!(id: 3, value: Number::UintT(std::u64::MAX))).is_ok());
        assert!(info.to_string().contains("value: UintArray([4, 2, 0, 2, 1, 2], LinearHistogram)"));
        assert!(info.apply(&insert!(id: 3, value: Number::UintT(0))).is_ok());
        assert!(info.to_string().contains("value: UintArray([4, 2, 1, 2, 1, 2], LinearHistogram)"));
        assert!(info.apply(&insert!(id: 3, value: Number::IntT(0))).is_err());
        assert!(info.apply(&insert!(id: 3, value: Number::DoubleT(0.0))).is_err());
        assert!(info.apply(&array_set!(id: 3, index: 1, value: Number::UintT(222))).is_err());
        assert!(info.to_string().contains("value: UintArray([4, 2, 1, 2, 1, 2], LinearHistogram)"));
        assert!(info.apply(&insert_multiple!(id: 3, value: Number::UintT(7), count: 4)).is_ok());
        assert!(info.to_string().contains("value: UintArray([4, 2, 1, 2, 5, 2], LinearHistogram)"));
        Ok(())
    }

    #[test]
    fn test_exponential_uint_ops() -> Result<(), Error> {
        let mut info = Data::new();
        // Bucket boundaries are 5, 7, 13
        info.apply(&create_exponential_histogram!(parent: ROOT_ID, id: 3, name: "value",
                    floor: 5, initial_step: 2,
                    step_multiplier: 4, buckets: 2, type: UintT))?;
        assert!(info
            .to_string()
            .contains("value: UintArray([5, 2, 4, 0, 0, 0, 0], ExponentialHistogram)"));
        assert!(info.apply(&insert!(id: 3, value: Number::UintT(5))).is_ok());
        assert!(info
            .to_string()
            .contains("value: UintArray([5, 2, 4, 0, 1, 0, 0], ExponentialHistogram)"));
        assert!(info.apply(&insert!(id: 3, value: Number::UintT(6))).is_ok());
        assert!(info
            .to_string()
            .contains("value: UintArray([5, 2, 4, 0, 2, 0, 0], ExponentialHistogram)"));
        assert!(info.apply(&insert!(id: 3, value: Number::UintT(7))).is_ok());
        assert!(info
            .to_string()
            .contains("value: UintArray([5, 2, 4, 0, 2, 1, 0], ExponentialHistogram)"));
        assert!(info.apply(&insert!(id: 3, value: Number::UintT(13))).is_ok());
        assert!(info
            .to_string()
            .contains("value: UintArray([5, 2, 4, 0, 2, 1, 1], ExponentialHistogram)"));
        assert!(info.apply(&insert!(id: 3, value: Number::UintT(std::u64::MAX))).is_ok());
        assert!(info
            .to_string()
            .contains("value: UintArray([5, 2, 4, 0, 2, 1, 2], ExponentialHistogram)"));
        assert!(info.apply(&insert!(id: 3, value: Number::UintT(0))).is_ok());
        assert!(info
            .to_string()
            .contains("value: UintArray([5, 2, 4, 1, 2, 1, 2], ExponentialHistogram)"));
        assert!(info.apply(&insert!(id: 3, value: Number::IntT(0))).is_err());
        assert!(info.apply(&insert!(id: 3, value: Number::DoubleT(0.0))).is_err());
        assert!(info.apply(&array_set!(id: 3, index: 1, value: Number::UintT(222))).is_err());
        assert!(info
            .to_string()
            .contains("value: UintArray([5, 2, 4, 1, 2, 1, 2], ExponentialHistogram)"));
        assert!(info.apply(&insert_multiple!(id: 3, value: Number::UintT(12), count: 4)).is_ok());
        assert!(info
            .to_string()
            .contains("value: UintArray([5, 2, 4, 1, 2, 5, 2], ExponentialHistogram)"));
        Ok(())
    }

    #[test]
    fn test_basic_double_ops() -> Result<(), Error> {
        let mut info = Data::new();
        info.apply(&create_numeric_property!(parent: ROOT_ID, id: 3, name: "value",
                                     value: Number::DoubleT(42.0)))?;
        assert!(info.apply(&add_number!(id: 3, value: Number::DoubleT(3.0))).is_ok());
        assert!(info.to_string().contains("value: Double(45.0)"));
        assert!(info.apply(&add_number!(id: 3, value: Number::IntT(3))).is_err());
        assert!(info.apply(&add_number!(id: 3, value: Number::UintT(3))).is_err());
        assert!(info.to_string().contains("value: Double(45.0)"));
        assert!(info.apply(&subtract_number!(id: 3, value: Number::DoubleT(5.0))).is_ok());
        assert!(info.to_string().contains("value: Double(40.0)"));
        assert!(info.apply(&subtract_number!(id: 3, value: Number::UintT(5))).is_err());
        assert!(info.apply(&subtract_number!(id: 3, value: Number::UintT(5))).is_err());
        assert!(info.to_string().contains("value: Double(40.0)"));
        assert!(info.apply(&set_number!(id: 3, value: Number::DoubleT(22.0))).is_ok());
        assert!(info.to_string().contains("value: Double(22.0)"));
        assert!(info.apply(&set_number!(id: 3, value: Number::UintT(23))).is_err());
        assert!(info.apply(&set_number!(id: 3, value: Number::UintT(24))).is_err());
        assert!(info.to_string().contains("value: Double(22.0)"));
        Ok(())
    }

    #[test]
    fn test_array_double_ops() -> Result<(), Error> {
        let mut info = Data::new();
        info.apply(&create_array_property!(parent: ROOT_ID, id: 3, name: "value", slots: 3,
                                     type: NumberType::Double))?;
        assert!(info.apply(&array_add!(id: 3, index: 1, value: Number::DoubleT(3.0))).is_ok());
        assert!(info.to_string().contains("value: DoubleArray([0.0, 3.0, 0.0], Default)"));
        assert!(info.apply(&array_add!(id: 3, index: 1, value: Number::IntT(3))).is_err());
        assert!(info.apply(&array_add!(id: 3, index: 1, value: Number::UintT(3))).is_err());
        assert!(info.to_string().contains("value: DoubleArray([0.0, 3.0, 0.0], Default)"));
        assert!(info.apply(&array_subtract!(id: 3, index: 2, value: Number::DoubleT(5.0))).is_ok());
        assert!(info.to_string().contains("value: DoubleArray([0.0, 3.0, -5.0], Default)"));
        assert!(info.apply(&array_subtract!(id: 3, index: 2, value: Number::IntT(5))).is_err());
        assert!(info.apply(&array_subtract!(id: 3, index: 2, value: Number::UintT(5))).is_err());
        assert!(info.to_string().contains("value: DoubleArray([0.0, 3.0, -5.0], Default)"));
        assert!(info.apply(&array_set!(id: 3, index: 1, value: Number::DoubleT(22.0))).is_ok());
        assert!(info.to_string().contains("value: DoubleArray([0.0, 22.0, -5.0], Default)"));
        assert!(info.apply(&array_set!(id: 3, index: 1, value: Number::IntT(23))).is_err());
        assert!(info.apply(&array_set!(id: 3, index: 1, value: Number::IntT(24))).is_err());
        assert!(info.to_string().contains("value: DoubleArray([0.0, 22.0, -5.0], Default)"));
        Ok(())
    }

    #[test]
    fn test_linear_double_ops() -> Result<(), Error> {
        let mut info = Data::new();
        info.apply(&create_linear_histogram!(parent: ROOT_ID, id: 3, name: "value",
                    floor: 4.0, step_size: 0.5, buckets: 2, type: DoubleT))?;
        assert!(info
            .to_string()
            .contains("value: DoubleArray([4.0, 0.5, 0.0, 0.0, 0.0, 0.0], LinearHistogram)"));
        assert!(info.apply(&insert!(id: 3, value: Number::DoubleT(4.0))).is_ok());
        assert!(info
            .to_string()
            .contains("value: DoubleArray([4.0, 0.5, 0.0, 1.0, 0.0, 0.0], LinearHistogram)"));
        assert!(info.apply(&insert!(id: 3, value: Number::DoubleT(4.25))).is_ok());
        assert!(info
            .to_string()
            .contains("value: DoubleArray([4.0, 0.5, 0.0, 2.0, 0.0, 0.0], LinearHistogram)"));
        assert!(info.apply(&insert!(id: 3, value: Number::DoubleT(4.75))).is_ok());
        assert!(info
            .to_string()
            .contains("value: DoubleArray([4.0, 0.5, 0.0, 2.0, 1.0, 0.0], LinearHistogram)"));
        assert!(info.apply(&insert!(id: 3, value: Number::DoubleT(5.1))).is_ok());
        assert!(info
            .to_string()
            .contains("value: DoubleArray([4.0, 0.5, 0.0, 2.0, 1.0, 1.0], LinearHistogram)"));
        assert!(info.apply(&insert!(id: 3, value: Number::DoubleT(std::f64::MAX))).is_ok());
        assert!(info
            .to_string()
            .contains("value: DoubleArray([4.0, 0.5, 0.0, 2.0, 1.0, 2.0], LinearHistogram)"));
        assert!(info
            .apply(&insert!(id: 3, value: Number::DoubleT(std::f64::MIN_POSITIVE)))
            .is_ok());
        assert!(info
            .to_string()
            .contains("value: DoubleArray([4.0, 0.5, 1.0, 2.0, 1.0, 2.0], LinearHistogram)"));
        assert!(info.apply(&insert!(id: 3, value: Number::DoubleT(std::f64::MIN))).is_ok());
        assert!(info
            .to_string()
            .contains("value: DoubleArray([4.0, 0.5, 2.0, 2.0, 1.0, 2.0], LinearHistogram)"));
        assert!(info.apply(&insert!(id: 3, value: Number::DoubleT(0.0))).is_ok());
        assert!(info
            .to_string()
            .contains("value: DoubleArray([4.0, 0.5, 3.0, 2.0, 1.0, 2.0], LinearHistogram)"));
        assert!(info.apply(&insert!(id: 3, value: Number::IntT(0))).is_err());
        assert!(info.apply(&insert!(id: 3, value: Number::UintT(0))).is_err());
        assert!(info.apply(&array_set!(id: 3, index: 1, value: Number::DoubleT(222.0))).is_err());
        assert!(info
            .to_string()
            .contains("value: DoubleArray([4.0, 0.5, 3.0, 2.0, 1.0, 2.0], LinearHistogram)"));
        assert!(info
            .apply(&insert_multiple!(id: 3, value: Number::DoubleT(4.5), count: 4))
            .is_ok());
        assert!(info
            .to_string()
            .contains("value: DoubleArray([4.0, 0.5, 3.0, 2.0, 5.0, 2.0], LinearHistogram)"));
        Ok(())
    }

    #[test]
    fn test_exponential_double_ops() -> Result<(), Error> {
        let mut info = Data::new();
        // Bucket boundaries are 5, 7, 13, 37
        info.apply(&create_exponential_histogram!(parent: ROOT_ID, id: 3, name: "value",
                    floor: 5.0, initial_step: 2.0,
                    step_multiplier: 4.0, buckets: 3, type: DoubleT))?;
        assert!(info.to_string().contains(
            "value: DoubleArray([5.0, 2.0, 4.0, 0.0, 0.0, 0.0, 0.0, 0.0], ExponentialHistogram)"
        ));
        assert!(info.apply(&insert!(id: 3, value: Number::DoubleT(5.0))).is_ok());
        assert!(info.to_string().contains(
            "value: DoubleArray([5.0, 2.0, 4.0, 0.0, 1.0, 0.0, 0.0, 0.0], ExponentialHistogram)"
        ));
        assert!(info.apply(&insert!(id: 3, value: Number::DoubleT(6.9))).is_ok());
        assert!(info.to_string().contains(
            "value: DoubleArray([5.0, 2.0, 4.0, 0.0, 2.0, 0.0, 0.0, 0.0], ExponentialHistogram)"
        ));
        assert!(info.apply(&insert!(id: 3, value: Number::DoubleT(7.1))).is_ok());
        assert!(info.to_string().contains(
            "value: DoubleArray([5.0, 2.0, 4.0, 0.0, 2.0, 1.0, 0.0, 0.0], ExponentialHistogram)"
        ));
        assert!(info
            .apply(&insert_multiple!(id: 3, value: Number::DoubleT(12.9), count: 4))
            .is_ok());
        assert!(info.to_string().contains(
            "value: DoubleArray([5.0, 2.0, 4.0, 0.0, 2.0, 5.0, 0.0, 0.0], ExponentialHistogram)"
        ));
        assert!(info.apply(&insert!(id: 3, value: Number::DoubleT(13.1))).is_ok());
        assert!(info.to_string().contains(
            "value: DoubleArray([5.0, 2.0, 4.0, 0.0, 2.0, 5.0, 1.0, 0.0], ExponentialHistogram)"
        ));
        assert!(info.apply(&insert!(id: 3, value: Number::DoubleT(36.9))).is_ok());
        assert!(info.to_string().contains(
            "value: DoubleArray([5.0, 2.0, 4.0, 0.0, 2.0, 5.0, 2.0, 0.0], ExponentialHistogram)"
        ));
        assert!(info.apply(&insert!(id: 3, value: Number::DoubleT(37.1))).is_ok());
        assert!(info.to_string().contains(
            "value: DoubleArray([5.0, 2.0, 4.0, 0.0, 2.0, 5.0, 2.0, 1.0], ExponentialHistogram)"
        ));
        assert!(info.apply(&insert!(id: 3, value: Number::DoubleT(std::f64::MAX))).is_ok());
        assert!(info.to_string().contains(
            "value: DoubleArray([5.0, 2.0, 4.0, 0.0, 2.0, 5.0, 2.0, 2.0], ExponentialHistogram)"
        ));
        assert!(info
            .apply(&insert!(id: 3, value: Number::DoubleT(std::f64::MIN_POSITIVE)))
            .is_ok());
        assert!(info.to_string().contains(
            "value: DoubleArray([5.0, 2.0, 4.0, 1.0, 2.0, 5.0, 2.0, 2.0], ExponentialHistogram)"
        ));
        assert!(info.apply(&insert!(id: 3, value: Number::DoubleT(std::f64::MIN))).is_ok());
        assert!(info.to_string().contains(
            "value: DoubleArray([5.0, 2.0, 4.0, 2.0, 2.0, 5.0, 2.0, 2.0], ExponentialHistogram)"
        ));
        assert!(info.apply(&insert!(id: 3, value: Number::DoubleT(0.0))).is_ok());
        assert!(info.to_string().contains(
            "value: DoubleArray([5.0, 2.0, 4.0, 3.0, 2.0, 5.0, 2.0, 2.0], ExponentialHistogram)"
        ));
        assert!(info.apply(&insert!(id: 3, value: Number::IntT(0))).is_err());
        assert!(info.apply(&insert!(id: 3, value: Number::UintT(0))).is_err());
        assert!(info.apply(&array_set!(id: 3, index: 1, value: Number::DoubleT(222.0))).is_err());
        assert!(info.to_string().contains(
            "value: DoubleArray([5.0, 2.0, 4.0, 3.0, 2.0, 5.0, 2.0, 2.0], ExponentialHistogram)"
        ));
        Ok(())
    }

    #[test]
    fn test_basic_vector_ops() -> Result<(), Error> {
        let mut info = Data::new();
        info.apply(&create_string_property!(parent: ROOT_ID, id: 3, name: "value",
                                     value: "foo"))?;
        assert!(info.to_string().contains("value: String(\"foo\")"));
        assert!(info.apply(&set_string!(id: 3, value: "bar")).is_ok());
        assert!(info.to_string().contains("value: String(\"bar\")"));
        assert!(info.apply(&set_bytes!(id: 3, value: vec!(3u8))).is_err());
        assert!(info.to_string().contains("value: String(\"bar\")"));
        info.apply(&create_bytes_property!(parent: ROOT_ID, id: 4, name: "bvalue",
                                     value: vec!(1u8, 2u8)))?;
        assert!(info.to_string().contains("bvalue: Bytes([1, 2])"));
        assert!(info.apply(&set_bytes!(id: 4, value: vec!(3u8, 4u8))).is_ok());
        assert!(info.to_string().contains("bvalue: Bytes([3, 4])"));
        assert!(info.apply(&set_string!(id: 4, value: "baz")).is_err());
        assert!(info.to_string().contains("bvalue: Bytes([3, 4])"));
        Ok(())
    }

    #[test]
    fn test_basic_lazy_node_ops() -> Result<(), Error> {
        let mut info = Data::new();
        info.apply_lazy(&create_lazy_node!(parent: ROOT_ID, id: 1, name: "child", disposition: validate::LinkDisposition::Child, actions: vec![create_bytes_property!(parent: ROOT_ID, id: 1, name: "child_bytes",value: vec!(3u8, 4u8))]))?;
        info.apply_lazy(&create_lazy_node!(parent: ROOT_ID, id: 2, name: "inline", disposition: validate::LinkDisposition::Inline, actions: vec![create_bytes_property!(parent: ROOT_ID, id: 1, name: "inline_bytes",value: vec!(3u8, 4u8))]))?;

        // Outputs 'Inline' and 'Child' dispositions differently.
        assert_eq!(
            info.to_string(),
            "root ->\n> inline_bytes: Bytes([3, 4])\n> child ->\n> > child_bytes: Bytes([3, 4])"
        );

        info.apply_lazy(&delete_lazy_node!(id: 1))?;
        // Outputs only 'Inline' lazy node since 'Child' lazy node was deleted
        assert_eq!(info.to_string(), "root ->\n> inline_bytes: Bytes([3, 4])");

        Ok(())
    }

    #[test]
    fn test_illegal_node_actions() -> Result<(), Error> {
        let mut info = Data::new();
        // Parent must exist
        assert!(info.apply(&create_node!(parent: 42, id: 1, name: "child")).is_err());
        // Can't reuse node IDs
        info = Data::new();
        info.apply(&create_node!(parent: ROOT_ID, id: 1, name: "child"))?;
        assert!(info.apply(&create_node!(parent: ROOT_ID, id: 1, name: "another_child")).is_err());
        // Can't delete root
        info = Data::new();
        assert!(info.apply(&delete_node!(id: ROOT_ID)).is_err());
        // Can't delete nonexistent node
        info = Data::new();
        assert!(info.apply(&delete_node!(id: 333)).is_err());
        Ok(())
    }

    #[test]
    fn test_illegal_property_actions() -> Result<(), Error> {
        let mut info = Data::new();
        // Parent must exist
        assert!(info
            .apply(
                &create_numeric_property!(parent: 42, id: 1, name: "answer", value: Number::IntT(42))
            )
            .is_err());
        // Can't reuse property IDs
        info = Data::new();
        info.apply(&create_numeric_property!(parent: ROOT_ID, id: 1, name: "answer", value: Number::IntT(42)))?;
        assert!(info
            .apply(&create_numeric_property!(parent: ROOT_ID, id: 1, name: "another_answer", value: Number::IntT(7)))
            .is_err());
        // Can't delete nonexistent property
        info = Data::new();
        assert!(info.apply(&delete_property!(id: 1)).is_err());
        // Can't do basic-int on array or histogram, or any vice versa
        info = Data::new();
        info.apply(&create_numeric_property!(parent: ROOT_ID, id: 3, name: "value",
                                     value: Number::IntT(42)))?;
        info.apply(&create_array_property!(parent: ROOT_ID, id: 4, name: "array", slots: 2,
                                     type: NumberType::Int))?;
        info.apply(&create_linear_histogram!(parent: ROOT_ID, id: 5, name: "lin",
                                floor: 5, step_size: 2,
                                buckets: 2, type: IntT))?;
        info.apply(&create_exponential_histogram!(parent: ROOT_ID, id: 6, name: "exp",
                                floor: 5, initial_step: 2,
                                step_multiplier: 2, buckets: 2, type: IntT))?;
        assert!(info.apply(&set_number!(id: 3, value: Number::IntT(5))).is_ok());
        assert!(info.apply(&array_set!(id: 4, index: 0, value: Number::IntT(5))).is_ok());
        assert!(info.apply(&insert!(id: 5, value: Number::IntT(2))).is_ok());
        assert!(info.apply(&insert!(id: 6, value: Number::IntT(2))).is_ok());
        assert!(info.apply(&insert_multiple!(id: 5, value: Number::IntT(2), count: 3)).is_ok());
        assert!(info.apply(&insert_multiple!(id: 6, value: Number::IntT(2), count: 3)).is_ok());
        assert!(info.apply(&set_number!(id: 4, value: Number::IntT(5))).is_err());
        assert!(info.apply(&set_number!(id: 5, value: Number::IntT(5))).is_err());
        assert!(info.apply(&set_number!(id: 6, value: Number::IntT(5))).is_err());
        assert!(info.apply(&array_set!(id: 3, index: 0, value: Number::IntT(5))).is_err());
        assert!(info.apply(&array_set!(id: 5, index: 0, value: Number::IntT(5))).is_err());
        assert!(info.apply(&array_set!(id: 6, index: 0, value: Number::IntT(5))).is_err());
        assert!(info.apply(&insert!(id: 3, value: Number::IntT(2))).is_err());
        assert!(info.apply(&insert!(id: 4, value: Number::IntT(2))).is_err());
        assert!(info.apply(&insert_multiple!(id: 3, value: Number::IntT(2), count: 3)).is_err());
        assert!(info.apply(&insert_multiple!(id: 4, value: Number::IntT(2), count: 3)).is_err());
        Ok(())
    }

    #[test]
    fn test_enum_values() {
        assert_eq!(BlockType::IntValue.to_isize().unwrap(), ArrayType::Int.to_isize().unwrap());
        assert_eq!(BlockType::UintValue.to_isize().unwrap(), ArrayType::Uint.to_isize().unwrap());
        assert_eq!(
            BlockType::DoubleValue.to_isize().unwrap(),
            ArrayType::Double.to_isize().unwrap()
        );
    }

    #[test]
    fn test_create_node_checks() {
        let mut data = Data::new();
        assert!(data.apply(&create_node!(parent: 0, id: 1, name: "first")).is_ok());
        assert!(data.apply(&create_node!(parent: 0, id: 2, name: "second")).is_ok());
        assert!(data.apply(&create_node!(parent: 1, id: 3, name: "child")).is_ok());
        assert!(data.apply(&create_node!(parent: 0, id: 2, name: "double")).is_err());
        let mut data = Data::new();
        assert!(data.apply(&create_node!(parent: 1, id: 2, name: "orphan")).is_err());
    }

    #[test]
    fn test_delete_node_checks() {
        let mut data = Data::new();
        assert!(data.apply(&delete_node!(id: 0)).is_err());
        let mut data = Data::new();
        data.apply(&create_node!(parent: 0, id: 1, name: "first")).ok();
        assert!(data.apply(&delete_node!(id: 1)).is_ok());
        assert!(data.apply(&delete_node!(id: 1)).is_err());
    }

    #[test]
    // Make sure tombstoning works correctly (tracking implicitly deleted descendants).
    fn test_node_tombstoning() {
        // Can delete, but not double-delete, a tombstoned node.
        let mut data = Data::new();
        assert!(data.apply(&create_node!(parent: 0, id: 1, name: "first")).is_ok());
        assert!(data
            .apply(&create_numeric_property!(parent: 1, id: 2,
            name: "answer", value: Number::IntT(42)))
            .is_ok());
        assert!(data.apply(&delete_node!(id: 1)).is_ok());
        assert!(data.apply(&delete_property!(id: 2)).is_ok());
        assert!(data.apply(&delete_property!(id: 2)).is_err());
        // Can tombstone, then delete, then create.
        let mut data = Data::new();
        assert!(data.apply(&create_node!(parent: 0, id: 1, name: "first")).is_ok());
        assert!(data
            .apply(&create_numeric_property!(parent: 1, id: 2,
            name: "answer", value: Number::IntT(42)))
            .is_ok());
        assert!(data.apply(&delete_node!(id: 1)).is_ok());
        assert!(data.apply(&delete_property!(id: 2)).is_ok());
        assert!(data
            .apply(&create_numeric_property!(parent: 0, id: 2,
            name: "root_answer", value: Number::IntT(42)))
            .is_ok());
        // Cannot tombstone, then create.
        let mut data = Data::new();
        assert!(data.apply(&create_node!(parent: 0, id: 1, name: "first")).is_ok());
        assert!(data
            .apply(&create_numeric_property!(parent: 1, id: 2,
            name: "answer", value: Number::IntT(42)))
            .is_ok());
        assert!(data.apply(&delete_node!(id: 1)).is_ok());
        assert!(data
            .apply(&create_numeric_property!(parent: 0, id: 2,
            name: "root_answer", value: Number::IntT(42)))
            .is_err());
    }

    #[test]
    fn test_property_tombstoning() {
        // Make sure tombstoning works correctly (tracking implicitly deleted descendants).
        // Can delete, but not double-delete, a tombstoned property.
        let mut data = Data::new();
        assert!(data.apply(&create_node!(parent: 0, id: 1, name: "first")).is_ok());
        assert!(data.apply(&create_node!(parent: 1, id: 2, name: "second")).is_ok());
        assert!(data.apply(&delete_node!(id: 1)).is_ok());
        assert!(data.apply(&delete_node!(id: 2)).is_ok());
        assert!(data.apply(&delete_node!(id: 2)).is_err());
        // Can tombstone, then delete, then create.
        let mut data = Data::new();
        assert!(data.apply(&create_node!(parent: 0, id: 1, name: "first")).is_ok());
        assert!(data.apply(&create_node!(parent: 1, id: 2, name: "second")).is_ok());
        assert!(data.apply(&delete_node!(id: 1)).is_ok());
        assert!(data.apply(&delete_node!(id: 2)).is_ok());
        assert!(data.apply(&create_node!(parent: 0, id: 2, name: "new_root_second")).is_ok());
        // Cannot tombstone, then create.
        let mut data = Data::new();
        assert!(data.apply(&create_node!(parent: 0, id: 1, name: "first")).is_ok());
        assert!(data.apply(&create_node!(parent: 1, id: 2, name: "second")).is_ok());
        assert!(data.apply(&delete_node!(id: 1)).is_ok());
        assert!(data.apply(&create_node!(parent: 0, id: 2, name: "new_root_second")).is_err());
    }

    const DIFF_STRING: &'static str = r#"-- DIFF --
 same: "root ->"
 same: "> node ->"
local: "> > prop1: String(\"foo\")"
other: "> > prop1: String(\"bar\")""#;

    const FULL_STRING: &'static str = r#"-- LOCAL --
root ->
> node ->
> > prop1: String("foo")
-- OTHER --
root ->
> node ->
> > prop1: String("bar")"#;

    #[test]
    fn diff_modes_work() -> Result<(), Error> {
        let mut local = Data::new();
        let mut remote = Data::new();
        local.apply(&create_node!(parent: 0, id: 1, name: "node"))?;
        local.apply(&create_string_property!(parent: 1, id: 2, name: "prop1", value: "foo"))?;
        remote.apply(&create_node!(parent: 0, id: 1, name: "node"))?;
        remote.apply(&create_string_property!(parent: 1, id: 2, name: "prop1", value: "bar"))?;
        match local.compare(&mut remote, DiffType::Diff) {
            Err(error) => {
                let error_string = format!("{:?}", error);
                assert_eq!("Trees differ:\n".to_string() + DIFF_STRING, error_string);
            }
            _ => return Err(format_err!("Didn't get failure")),
        }
        match local.compare(&mut remote, DiffType::Full) {
            Err(error) => {
                let error_string = format!("{:?}", error);
                assert_eq!("Trees differ:\n".to_string() + FULL_STRING, error_string);
            }
            _ => return Err(format_err!("Didn't get failure")),
        }
        match local.compare(&mut remote, DiffType::Both) {
            Err(error) => {
                let error_string = format!("{:?}", error);
                assert_eq!(
                    vec!["Trees differ:", FULL_STRING, DIFF_STRING].join("\n"),
                    error_string
                );
            }
            _ => return Err(format_err!("Didn't get failure")),
        }
        Ok(())
    }
}
