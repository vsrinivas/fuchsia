// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::trie::*,
    anyhow::Error,
    core::marker::PhantomData,
    fidl_fuchsia_diagnostics::Selector,
    num_derive::{FromPrimitive, ToPrimitive},
    num_traits::bounds::Bounded,
    regex::{Regex, RegexSet},
    std::{
        collections::HashMap,
        convert::TryFrom,
        ops::{Add, AddAssign, MulAssign},
        sync::Arc,
    },
};

pub mod trie;

// TODO(fxb/43873): move LinkNodeDisposition and ArrayFormat to fuchsia-inspect-format
/// Disposition of a Link value.
#[derive(Clone, Debug, PartialEq, Eq, FromPrimitive, ToPrimitive)]
pub enum LinkNodeDisposition {
    Child = 0,
    Inline = 1,
}

/// Format in which the array will be read.
#[derive(Clone, Debug, PartialEq, Eq, FromPrimitive, ToPrimitive)]
pub enum ArrayFormat {
    /// Regular array, it stores N values in N slots.
    Default = 0,

    /// The array is a linear histogram with N buckets and N+4 slots, which are:
    /// - param_floor_value
    /// - param_step_size
    /// - underflow_bucket
    /// - ...N buckets...
    /// - overflow_bucket
    LinearHistogram = 1,

    /// The array is an exponential histogram with N buckets and N+5 slots, which are:
    /// - param_floor_value
    /// - param_initial_step
    /// - param_step_multiplier
    /// - underflow_bucket
    /// - ...N buckets...
    /// - overflow_bucket
    ExponentialHistogram = 2,
}

/// A hierarchy of Inspect Nodes.
///
/// Each hierarchy consists of properties, and a map of named child hierarchies.
#[derive(Clone, Debug, PartialEq)]
pub struct NodeHierarchy {
    /// The name of this node.
    pub name: String,

    /// The properties for the node.
    pub properties: Vec<Property>,

    /// The children of this node.
    pub children: Vec<NodeHierarchy>,

    /// Values that were impossible to load.
    pub missing: Vec<MissingValue>,
}

/// A value that couldn't be loaded in the hierarchy and the reason.
#[derive(Clone, Debug, PartialEq)]
pub struct MissingValue {
    /// Specific reason why the value couldn't be loaded.
    pub reason: MissingValueReason,

    /// The name of the value.
    pub name: String,
}

/// Reasons why the value couldn't be loaded.
#[derive(Clone, Debug, PartialEq)]
pub enum MissingValueReason {
    /// A referenced hierarchy in the link was not found.
    LinkNotFound,

    /// A linked hierarchy couldn't be parsed.
    LinkParseFailure,

    /// A linked hierarchy was invalid.
    LinkInvalid,

    /// There was no attempt to read the link.
    LinkNeverExpanded,
}

impl NodeHierarchy {
    pub fn new_root() -> Self {
        NodeHierarchy::new("root", vec![], vec![])
    }

    pub fn new(
        name: impl Into<String>,
        properties: Vec<Property>,
        children: Vec<NodeHierarchy>,
    ) -> Self {
        Self { name: name.into(), properties, children, missing: vec![] }
    }

    /// Sorts the properties and children of the node hierarchy by name.
    pub fn sort(&mut self) {
        if self.properties.iter().all(|p| p.name().parse::<u64>().is_ok()) {
            self.properties.sort_by(|p1, p2| {
                let p1_value = p1.name().parse::<u64>().unwrap();
                let p2_value = p2.name().parse::<u64>().unwrap();
                p1_value.partial_cmp(&p2_value).unwrap()
            });
        } else {
            self.properties.sort_by(|p1, p2| p1.name().partial_cmp(p2.name()).unwrap());
        }
        self.children.sort_by(|c1, c2| c1.name.partial_cmp(&c2.name).unwrap());
        for child in self.children.iter_mut() {
            child.sort();
        }
    }

    /// Either returns an existing child of `self` with name `name` or creates
    /// a new child with name `name`.
    pub fn get_or_add_child_mut(
        &mut self,
        name: impl Into<String> + Copy,
    ) -> Option<&mut NodeHierarchy> {
        // We have to use indices to iterate here because the borrow checker cannot
        // deduce that there are no borrowed values in the else-branch.
        // TODO(4601): We could make this cleaner by changing the NodeHierarchy
        // children to hashmaps.
        match (0..self.children.len()).find(|&i| self.children[i].name == name.into()) {
            Some(matching_index) => Some(&mut self.children[matching_index]),
            None => {
                self.children.push(NodeHierarchy::new(name.into(), vec![], vec![]));
                Some(
                    self.children
                        .last_mut()
                        .expect("We just added an entry so we cannot get None here."),
                )
            }
        }
    }

    /// Inserts a new Property into a Node whose location in a hierarchy
    /// rooted at `self` is defined by node_path.
    ///
    /// Requires: a non-empty node_path vector.
    /// Requires: that the first entry in node_path is the name of the
    /// `self` node on which the method is called.
    ///
    /// NOTE: Inspect VMOs may allow multiple nodes of the same name. In this case,
    ///       the property is added to the first node found.
    pub fn add(&mut self, node_path: Vec<impl Into<String> + Copy>, property: Property) {
        assert!(!node_path.is_empty(), "Property insertion requires a valid node-path.");
        let mut curr_node_option: Option<&mut NodeHierarchy> = None;

        for node_path_entry in node_path {
            match curr_node_option {
                Some(curr_node) => {
                    curr_node_option = curr_node.get_or_add_child_mut(node_path_entry);
                }
                None => {
                    // This is the first node_path_entry we've seen, it is an invariant
                    // of this inserter that we call add() at the root node of the
                    // provided node_path.
                    assert_eq!(self.name, node_path_entry.into());
                    curr_node_option = Some(self);
                }
            }
        }

        curr_node_option
            .expect(
                "curr_node cannot be none, since a valid node-path is a requirement of insertion.",
            )
            .properties
            .push(property);
    }

    /// Provides an iterator over the node hierarchy returning properties in pre-order.
    pub fn property_iter(&self) -> impl Iterator<Item = (Vec<&String>, &Property)> {
        TrieIterableType { iterator: NodeHierarchyIterator::new(&self), _marker: PhantomData }
    }

    /// Adds a value that couldn't be read. This can happen when loading a lazy child.
    pub fn add_missing(&mut self, reason: MissingValueReason, name: String) {
        self.missing.push(MissingValue { reason, name });
    }
}

impl TrieIterableNode<String, Property> for NodeHierarchy {
    fn get_children(&self) -> HashMap<&String, &Self> {
        self.children
            .iter()
            .map(|node_hierarchy| (&node_hierarchy.name, node_hierarchy))
            .collect::<HashMap<&String, &Self>>()
    }

    fn get_values(&self) -> &[Property] {
        return &self.properties;
    }
}

struct NodeHierarchyIterator<'a> {
    root: &'a NodeHierarchy,
    iterator_initialized: bool,
    work_stack: Vec<TrieIterableWorkEvent<'a, String, NodeHierarchy>>,
    curr_key: Vec<&'a String>,
    curr_node: Option<&'a NodeHierarchy>,
    curr_val_index: usize,
}

impl<'a> NodeHierarchyIterator<'a> {
    pub fn new(root: &'a NodeHierarchy) -> Self {
        NodeHierarchyIterator {
            root,
            iterator_initialized: false,
            work_stack: Vec::new(),
            curr_key: Vec::new(),
            curr_node: None,
            curr_val_index: 0,
        }
    }
}

impl<'a> TrieIterable<'a, String, Property> for NodeHierarchyIterator<'a> {
    type Node = NodeHierarchy;

    fn is_initialized(&self) -> bool {
        self.iterator_initialized
    }

    fn initialize(&mut self) {
        self.iterator_initialized = true;
        self.add_work_event(TrieIterableWorkEvent {
            key_state: TrieIterableKeyState::PopKeyFragment,
            potential_child: None,
        });

        self.curr_node = Some(self.root);
        self.curr_key.push(&self.root.name);
        for (key_fragment, child_node) in self.root.get_children().iter() {
            self.add_work_event(TrieIterableWorkEvent {
                key_state: TrieIterableKeyState::AddKeyFragment(key_fragment),
                potential_child: Some(child_node),
            });
        }
    }

    fn add_work_event(&mut self, work_event: TrieIterableWorkEvent<'a, String, Self::Node>) {
        self.work_stack.push(work_event);
    }

    fn expect_work_event(&mut self) -> TrieIterableWorkEvent<'a, String, Self::Node> {
        self.work_stack
            .pop()
            .expect("Should never attempt to retrieve an event from an empty work stack,")
    }
    fn expect_curr_node(&self) -> &'a Self::Node {
        self.curr_node.expect("We should never be trying to retrieve an unset working node.")
    }
    fn set_curr_node(&mut self, new_node: &'a Self::Node) {
        self.curr_val_index = 0;
        self.curr_node = Some(new_node);
    }
    fn is_curr_node_fully_processed(&self) -> bool {
        let curr_node = self
            .curr_node
            .expect("We should always have a working node when checking progress on that node.");
        curr_node.get_values().is_empty() || curr_node.get_values().len() <= self.curr_val_index
    }
    fn is_work_stack_empty(&self) -> bool {
        self.work_stack.is_empty()
    }
    fn pop_curr_key_fragment(&mut self) {
        self.curr_key.pop();
    }
    fn extend_curr_key(&mut self, new_fragment: &'a String) {
        self.curr_key.push(new_fragment);
    }
    fn expect_next_value(&mut self) -> (Vec<&'a String>, &'a Property) {
        self.curr_val_index = self.curr_val_index + 1;
        (
            self.curr_key.clone(),
            &self
                .curr_node
                .expect("Should never be retrieving a node value without a working node.")
                .get_values()[self.curr_val_index - 1],
        )
    }
}

#[derive(Debug, PartialEq, Clone)]
pub struct LinkValue {
    /// The name of the link.
    pub name: String,

    /// The content of the link.
    pub content: String,

    /// The disposition of the link in the hierarchy when evaluated.
    pub disposition: LinkNodeDisposition,
}

/// A named property. Each of the fields consists of (name, value).
#[derive(Debug, PartialEq, Clone)]
pub enum Property {
    /// The value is a string.
    String(String, String),

    /// The value is a bytes vector.
    Bytes(String, Vec<u8>),

    /// The value is an integer.
    Int(String, i64),

    /// The value is an unsigned integer.
    Uint(String, u64),

    /// The value is a double.
    Double(String, f64),

    /// The value is a double array.
    DoubleArray(String, ArrayValue<f64>),

    /// The value is an integer array.
    IntArray(String, ArrayValue<i64>),

    /// The value is an unsigned integer array.
    UintArray(String, ArrayValue<u64>),
}

#[allow(missing_docs)]
#[derive(Debug, PartialEq, Clone)]
pub struct ArrayValue<T> {
    pub format: ArrayFormat,
    pub values: Vec<T>,
}

#[allow(missing_docs)]
#[derive(Debug, PartialEq)]
pub struct ArrayBucket<T> {
    pub floor: T,
    pub upper: T,
    pub count: T,
}

impl<T> ArrayBucket<T> {
    fn new(floor: T, upper: T, count: T) -> Self {
        Self { floor, upper, count }
    }
}

impl<T: Add<Output = T> + AddAssign + Copy + MulAssign + Bounded> ArrayValue<T> {
    #[allow(missing_docs)]
    pub fn new(values: Vec<T>, format: ArrayFormat) -> Self {
        Self { format, values }
    }

    #[allow(missing_docs)]
    pub fn buckets(&self) -> Option<Vec<ArrayBucket<T>>> {
        match self.format {
            ArrayFormat::Default => None,
            ArrayFormat::LinearHistogram => self.buckets_for_linear_hist(),
            ArrayFormat::ExponentialHistogram => self.buckets_for_exp_hist(),
        }
    }

    fn buckets_for_linear_hist(&self) -> Option<Vec<ArrayBucket<T>>> {
        // Check that the minimum required values are available:
        // floor, stepsize, underflow, bucket 0, underflow
        if self.values.len() < 5 {
            return None;
        }
        let mut floor = self.values[0];
        let step_size = self.values[1];

        let mut result = Vec::new();
        result.push(ArrayBucket::new(T::min_value(), floor, self.values[2]));
        for i in 3..self.values.len() - 1 {
            result.push(ArrayBucket::new(floor, floor + step_size, self.values[i]));
            floor += step_size;
        }
        result.push(ArrayBucket::new(floor, T::max_value(), self.values[self.values.len() - 1]));
        Some(result)
    }

    fn buckets_for_exp_hist(&self) -> Option<Vec<ArrayBucket<T>>> {
        // Check that the minimum required values are available:
        // floor, initial step, step multiplier, underflow, bucket 0, underflow
        if self.values.len() < 6 {
            return None;
        }
        let floor = self.values[0];
        let initial_step = self.values[1];
        let step_multiplier = self.values[2];

        let mut result = vec![ArrayBucket::new(T::min_value(), floor, self.values[3])];

        let mut offset = initial_step;
        let mut current_floor = floor;
        for i in 4..self.values.len() - 1 {
            let upper = floor + offset;
            result.push(ArrayBucket::new(current_floor, upper, self.values[i]));
            offset *= step_multiplier;
            current_floor = upper;
        }

        result.push(ArrayBucket::new(
            current_floor,
            T::max_value(),
            self.values[self.values.len() - 1],
        ));
        Some(result)
    }
}

impl Property {
    #[allow(missing_docs)]
    pub fn name(&self) -> &str {
        match self {
            Property::String(name, _)
            | Property::Bytes(name, _)
            | Property::Int(name, _)
            | Property::IntArray(name, _)
            | Property::Uint(name, _)
            | Property::UintArray(name, _)
            | Property::Double(name, _)
            | Property::DoubleArray(name, _) => &name,
        }
    }
}

/// Wrapper for the tools needed to filter a single NodeHierarchy based on selectors
/// known to be applicable to it.
///
/// `component_node_selector` is a RegexSet of all path
///     selectors on a hierarchy.
///
/// `node_property_selectors` is a vector of Regexs that match single named properties
///     on a NodeHierarchy. NOTE: Their order is aligned with the vector of Regexes that created
///     the component_node_selector RegexSet, since each property selector is associated with
///     a particular node path selector.
#[derive(Clone)]
pub struct InspectHierarchyMatcher {
    /// RegexSet encoding all the node path selectors for
    /// inspect hierarchies under this component's out directory.
    pub component_node_selector: RegexSet,
    /// Vector of Regexes corresponding to the node path selectors
    /// in the regex set.
    /// Note: Order of Regexes matters here, this vector must be aligned
    /// with the vector used to construct component_node_selector since
    /// conponent_node_selector.matches() returns a vector of ints used to
    /// find all the relevant property selectors corresponding to the matching
    /// node selectors.
    pub node_property_selectors: Vec<Regex>,
}

impl TryFrom<&Vec<Arc<Selector>>> for InspectHierarchyMatcher {
    type Error = anyhow::Error;

    fn try_from(selectors: &Vec<Arc<Selector>>) -> Result<Self, Error> {
        let node_path_regexes = selectors
            .iter()
            .map(|selector| match &selector.tree_selector.node_path {
                Some(node_path) => selectors::convert_path_selector_to_regex(node_path),
                None => unreachable!("Selectors are required to specify a node path."),
            })
            .collect::<Result<Vec<Regex>, Error>>()?;

        let node_path_regex_set = RegexSet::new(
            &node_path_regexes
                .iter()
                .map(|selector_regex| selector_regex.as_str())
                .collect::<Vec<&str>>(),
        )?;

        let property_regexes = selectors
            .iter()
            .map(|selector| match &selector.tree_selector.target_properties {
                Some(target_property) => {
                    selectors::convert_property_selector_to_regex(target_property)
                }
                None => unreachable!("Selectors are required to specify a node path."),
            })
            .collect::<Result<Vec<Regex>, Error>>()?;

        Ok(InspectHierarchyMatcher {
            component_node_selector: node_path_regex_set,
            node_property_selectors: property_regexes,
        })
    }
}

// Filters a node hierarchy using a set of path selectors and their asscoaited property
// selectors.
//
// - If the return type is Ok(Some()) that implies that the filter encountered no errors AND
//    a meaningful tree remained at the end.
// - If the return type is Ok(None) that implies that the filter encountered no errors AND
//    the tree was filtered to be empty at the end.
// - If the return type is Error that implies the filter encountered errors.
pub fn filter_inspect_snapshot(
    root_node: NodeHierarchy,
    hierarchy_matcher: &InspectHierarchyMatcher,
) -> Result<Option<NodeHierarchy>, Error> {
    let mut properties_selected = 0;

    let mut new_root = NodeHierarchy::new(root_node.name.clone(), vec![], vec![]);

    let mut working_node_path: Option<String> = None;
    let mut working_property_regex_set: Option<RegexSet> = None;

    for (node_path, property) in root_node.property_iter() {
        let mut formatted_node_path = node_path
            .iter()
            .map(|s| selectors::sanitize_string_for_selectors(s))
            .collect::<Vec<String>>()
            .join("/");
        // We must append a "/" because the absolute monikers end in slash and
        // hierarchy node paths don't, but we want to reuse the regex logic.
        formatted_node_path.push('/');

        let property_regex_set: &RegexSet = match &working_node_path {
            Some(working_path) if *working_path == formatted_node_path => {
                working_property_regex_set.as_ref().unwrap()
            }
            _ => {
                let property_regex_strings = hierarchy_matcher
                    .component_node_selector
                    .matches(&formatted_node_path)
                    .into_iter()
                    .map(|property_index| {
                        let property_selector: &Regex =
                            &hierarchy_matcher.node_property_selectors[property_index];
                        property_selector.as_str()
                    })
                    .collect::<Vec<&str>>();

                let property_regex_set = RegexSet::new(property_regex_strings)?;
                working_node_path = Some(formatted_node_path);
                working_property_regex_set = Some(property_regex_set);
                working_property_regex_set.as_ref().unwrap()
            }
        };

        if property_regex_set.is_match(property.name()) {
            // TODO(4601): We can keep track of the prefix string identifying
            // the "curr_node" and only insert from root if our iteration has
            // brought us to a new node higher up the hierarchy. Right now, we
            // insert from root for every new property.
            new_root.add(node_path, property.clone());
            properties_selected = properties_selected + 1;
        }
    }

    if properties_selected > 0 {
        Ok(Some(new_root))
    } else {
        Ok(None)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use selectors;
    use std::convert::TryInto;

    #[test]
    fn test_node_hierarchy_iteration() {
        let double_array_data = vec![-1.2, 2.3, 3.4, 4.5, -5.6];
        let chars = ['a', 'b', 'c', 'd', 'e', 'f', 'g'];
        let string_data = chars.iter().cycle().take(6000).collect::<String>();
        let bytes_data = (0u8..=9u8).cycle().take(5000).collect::<Vec<u8>>();

        let test_hierarchy = NodeHierarchy::new(
            "root".to_string(),
            vec![
                Property::Int("int-root".to_string(), 3),
                Property::DoubleArray(
                    "property-double-array".to_string(),
                    ArrayValue::new(double_array_data.clone(), ArrayFormat::Default),
                ),
            ],
            vec![NodeHierarchy::new(
                "child-1".to_string(),
                vec![
                    Property::Uint("property-uint".to_string(), 10),
                    Property::Double("property-double".to_string(), -3.4),
                    Property::String("property-string".to_string(), string_data.clone()),
                    Property::IntArray(
                        "property-int-array".to_string(),
                        ArrayValue::new(vec![1, 2, 1, 1, 1, 1, 1], ArrayFormat::LinearHistogram),
                    ),
                ],
                vec![NodeHierarchy::new(
                    "child-1-1".to_string(),
                    vec![
                        Property::Int("property-int".to_string(), -9),
                        Property::Bytes("property-bytes".to_string(), bytes_data.clone()),
                        Property::UintArray(
                            "property-uint-array".to_string(),
                            ArrayValue::new(
                                vec![1, 1, 2, 0, 1, 1, 2, 0, 0],
                                ArrayFormat::ExponentialHistogram,
                            ),
                        ),
                    ],
                    vec![],
                )],
            )],
        );

        let mut results_vec = vec![
            (
                vec!["root".to_string(), "child-1".to_string(), "child-1-1".to_string()],
                Property::UintArray(
                    "property-uint-array".to_string(),
                    ArrayValue::new(
                        vec![1, 1, 2, 0, 1, 1, 2, 0, 0],
                        ArrayFormat::ExponentialHistogram,
                    ),
                ),
            ),
            (
                vec!["root".to_string(), "child-1".to_string(), "child-1-1".to_string()],
                Property::Bytes("property-bytes".to_string(), bytes_data),
            ),
            (
                vec!["root".to_string(), "child-1".to_string(), "child-1-1".to_string()],
                Property::Int("property-int".to_string(), -9),
            ),
            (
                vec!["root".to_string(), "child-1".to_string()],
                Property::IntArray(
                    "property-int-array".to_string(),
                    ArrayValue::new(vec![1, 2, 1, 1, 1, 1, 1], ArrayFormat::LinearHistogram),
                ),
            ),
            (
                vec!["root".to_string(), "child-1".to_string()],
                Property::String("property-string".to_string(), string_data),
            ),
            (
                vec!["root".to_string(), "child-1".to_string()],
                Property::Double("property-double".to_string(), -3.4),
            ),
            (
                vec!["root".to_string(), "child-1".to_string()],
                Property::Uint("property-uint".to_string(), 10),
            ),
            (
                vec!["root".to_string()],
                Property::DoubleArray(
                    "property-double-array".to_string(),
                    ArrayValue::new(double_array_data, ArrayFormat::Default),
                ),
            ),
            (vec!["root".to_string()], Property::Int("int-root".to_string(), 3)),
        ];

        let expected_num_entries = results_vec.len();
        let mut num_entries = 0;
        for (key, val) in test_hierarchy.property_iter() {
            num_entries = num_entries + 1;
            let (expected_key, expected_property) = results_vec.pop().unwrap();
            assert_eq!(
                key.iter().map(|s| s.as_str()).collect::<Vec<&str>>().join("/"),
                expected_key.iter().map(|s| s.as_str()).collect::<Vec<&str>>().join("/")
            );

            assert_eq!(*val, expected_property);
        }

        assert_eq!(num_entries, expected_num_entries);
    }

    #[test]
    fn array_value() {
        let values = vec![1, 2, 5, 7, 9, 11, 13];
        let array = ArrayValue::<u64>::new(values.clone(), ArrayFormat::Default);
        assert!(array.buckets().is_none());
    }

    #[test]
    fn linear_histogram_array_value() {
        let values = vec![1, 2, 5, 7, 9, 11, 13];
        let int_array = ArrayValue::<i64>::new(values.clone(), ArrayFormat::LinearHistogram);
        assert_eq!(int_array.values, values);
        let buckets = int_array.buckets().unwrap();
        assert_eq!(buckets.len(), 5);
        assert_eq!(buckets[0], ArrayBucket { floor: std::i64::MIN, upper: 1, count: 5 });
        assert_eq!(buckets[1], ArrayBucket { floor: 1, upper: 3, count: 7 });
        assert_eq!(buckets[2], ArrayBucket { floor: 3, upper: 5, count: 9 });
        assert_eq!(buckets[3], ArrayBucket { floor: 5, upper: 7, count: 11 });
        assert_eq!(buckets[4], ArrayBucket { floor: 7, upper: std::i64::MAX, count: 13 });
    }

    #[test]
    fn exponential_histogram_array_value() {
        let values = vec![1.0, 2.0, 5.0, 7.0, 9.0, 11.0, 15.0];
        let array = ArrayValue::<f64>::new(values.clone(), ArrayFormat::ExponentialHistogram);
        assert_eq!(array.values, values);
        let buckets = array.buckets().unwrap();
        assert_eq!(buckets.len(), 4);
        assert_eq!(buckets[0], ArrayBucket { floor: std::f64::MIN, upper: 1.0, count: 7.0 });
        assert_eq!(buckets[1], ArrayBucket { floor: 1.0, upper: 3.0, count: 9.0 });
        assert_eq!(buckets[2], ArrayBucket { floor: 3.0, upper: 11.0, count: 11.0 });
        assert_eq!(buckets[3], ArrayBucket { floor: 11.0, upper: std::f64::MAX, count: 15.0 });
    }

    #[test]
    fn add_to_hierarchy() {
        let mut hierarchy = NodeHierarchy::new_root();
        let prop_1 = Property::String("x".to_string(), "foo".to_string());
        let path_1 = vec!["root", "one"];
        let prop_2 = Property::Uint("c".to_string(), 3);
        let path_2 = vec!["root", "two"];
        let prop_2_prime = Property::Int("z".to_string(), -4);
        hierarchy.add(path_1, prop_1.clone());
        hierarchy.add(path_2.clone(), prop_2.clone());
        hierarchy.add(path_2, prop_2_prime.clone());

        assert_eq!(
            hierarchy,
            NodeHierarchy {
                name: "root".to_string(),
                children: vec![
                    NodeHierarchy {
                        name: "one".to_string(),
                        properties: vec![prop_1],
                        children: vec![],
                        missing: vec![],
                    },
                    NodeHierarchy {
                        name: "two".to_string(),
                        properties: vec![prop_2, prop_2_prime],
                        children: vec![],
                        missing: vec![],
                    }
                ],
                properties: vec![],
                missing: vec![],
            }
        );
    }

    #[test]
    #[should_panic]
    // Empty paths are meaningless on insertion and break the method invariant.
    fn no_empty_paths_allowed() {
        let mut hierarchy = NodeHierarchy::new_root();
        let prop_1 = Property::String("x".to_string(), "foo".to_string());
        let path_1: Vec<&String> = vec![];
        hierarchy.add(path_1, prop_1);
    }

    #[test]
    #[should_panic]
    // Paths provided to add must begin at the node we're calling
    // add() on.
    fn path_must_start_at_self() {
        let mut hierarchy = NodeHierarchy::new_root();
        let prop_1 = Property::String("x".to_string(), "foo".to_string());
        let path_1 = vec!["not_root", "a"];
        hierarchy.add(path_1, prop_1);
    }

    #[test]
    fn sort_hierarchy() {
        let mut hierarchy = NodeHierarchy::new(
            "root",
            vec![
                Property::String("x".to_string(), "foo".to_string()),
                Property::Uint("c".to_string(), 3),
                Property::Int("z".to_string(), -4),
            ],
            vec![
                NodeHierarchy::new(
                    "foo",
                    vec![
                        Property::Int("11".to_string(), -4),
                        Property::Bytes("123".to_string(), "foo".bytes().into_iter().collect()),
                        Property::Double("0".to_string(), 8.1),
                    ],
                    vec![],
                ),
                NodeHierarchy::new("bar", vec![], vec![]),
            ],
        );

        hierarchy.sort();

        let sorted_hierarchy = NodeHierarchy::new(
            "root",
            vec![
                Property::Uint("c".to_string(), 3),
                Property::String("x".to_string(), "foo".to_string()),
                Property::Int("z".to_string(), -4),
            ],
            vec![
                NodeHierarchy::new("bar", vec![], vec![]),
                NodeHierarchy::new(
                    "foo",
                    vec![
                        Property::Double("0".to_string(), 8.1),
                        Property::Int("11".to_string(), -4),
                        Property::Bytes("123".to_string(), "foo".bytes().into_iter().collect()),
                    ],
                    vec![],
                ),
            ],
        );
        assert_eq!(sorted_hierarchy, hierarchy);
    }

    #[test]
    fn exponential_histogram_buckets() {
        let values = vec![0, 2, 4, 0, 1, 2, 3, 4, 5];
        let buckets = ArrayValue::new(values, ArrayFormat::ExponentialHistogram).buckets().unwrap();
        assert_eq!(buckets.len(), 6);
        assert_eq!(buckets[0], ArrayBucket { floor: i64::min_value(), upper: 0, count: 0 });
        assert_eq!(buckets[1], ArrayBucket { floor: 0, upper: 2, count: 1 });
        assert_eq!(buckets[2], ArrayBucket { floor: 2, upper: 8, count: 2 });
        assert_eq!(buckets[3], ArrayBucket { floor: 8, upper: 32, count: 3 });
        assert_eq!(buckets[4], ArrayBucket { floor: 32, upper: 128, count: 4 });
        assert_eq!(buckets[5], ArrayBucket { floor: 128, upper: i64::max_value(), count: 5 });
    }

    #[test]
    fn test_filter_hierarchy() {
        let hierarchy = NodeHierarchy::new(
            "root",
            vec![
                Property::String("x".to_string(), "foo".to_string()),
                Property::Uint("c".to_string(), 3),
                Property::Int("z".to_string(), -4),
            ],
            vec![
                NodeHierarchy::new(
                    "foo",
                    vec![
                        Property::Int("11".to_string(), -4),
                        Property::Bytes("123".to_string(), "foo".bytes().into_iter().collect()),
                        Property::Double("0".to_string(), 8.1),
                    ],
                    vec![],
                ),
                NodeHierarchy::new("bar", vec![], vec![]),
            ],
        );

        let test_selectors = vec!["*:root/foo:11", "*:root:z"];
        let parsed_test_selectors = test_selectors
            .into_iter()
            .map(|selector_string| {
                Arc::new(
                    selectors::parse_selector(selector_string)
                        .expect("All test selectors are valid and parsable."),
                )
            })
            .collect::<Vec<Arc<Selector>>>();
        let hierarchy_matcher: InspectHierarchyMatcher =
            (&parsed_test_selectors).try_into().unwrap();

        let mut filtered_hierarchy = filter_inspect_snapshot(hierarchy, &hierarchy_matcher)
            .expect("filtered hierarchy should succeed.")
            .expect("There should be an actual resulting hierarchy.");
        filtered_hierarchy.sort();

        assert_eq!(
            filtered_hierarchy,
            NodeHierarchy::new(
                "root",
                vec![Property::Int("z".to_string(), -4),],
                vec![
                    NodeHierarchy::new("foo", vec![Property::Int("11".to_string(), -4),], vec![],),
                ],
            )
        );
    }
}
