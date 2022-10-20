// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Diagnostics hierarchy
//!
//! This library provides a tree strcture used to store diagnostics data such as inspect and logs,
//! as well as utilities for reading from it, serializing and deserializing it and testing it.

use {
    base64::display::Base64Display,
    fidl_fuchsia_diagnostics::{Selector, StringSelector, TreeSelector},
    num_derive::{FromPrimitive, ToPrimitive},
    num_traits::bounds::Bounded,
    regex::RegexSet,
    selectors::{self, ValidateExt},
    serde::Deserialize,
    std::{
        borrow::Borrow,
        cmp::Ordering,
        convert::{TryFrom, TryInto},
        fmt::{Display, Formatter, Result as FmtResult},
        ops::{Add, AddAssign, MulAssign},
        sync::Arc,
    },
    thiserror::Error,
};

pub mod macros;
pub mod serialization;
pub mod testing;
pub mod trie;

/// Extra slots for a linear histogram: 2 parameter slots (floor, step size) and
/// 2 overflow slots.
pub const LINEAR_HISTOGRAM_EXTRA_SLOTS: usize = 4;

/// Extra slots for an exponential histogram: 3 parameter slots (floor, initial
/// step and step multiplier) and 2 overflow slots.
pub const EXPONENTIAL_HISTOGRAM_EXTRA_SLOTS: usize = 5;

// TODO(fxbug.dev/43873): move LinkNodeDisposition and ArrayFormat to fuchsia-inspect-format
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

/// A hierarchy of nodes representing structured data, such as Inspect or
/// structured log data.
///
/// Each hierarchy consists of properties, and a map of named child hierarchies.
#[derive(Clone, Debug, PartialEq)]
pub struct DiagnosticsHierarchy<Key = String> {
    /// The name of this node.
    pub name: String,

    /// The properties for the node.
    pub properties: Vec<Property<Key>>,

    /// The children of this node.
    pub children: Vec<DiagnosticsHierarchy<Key>>,

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

    /// There was a timeout while reading.
    Timeout,
}

/// Compares the names of two properties or nodes. If both are unsigned integers, then it compares
/// their numerical value.
fn name_partial_cmp(a: &str, b: &str) -> Ordering {
    match (a.parse::<u64>(), b.parse::<u64>()) {
        (Ok(n), Ok(m)) => n.partial_cmp(&m).unwrap(),
        _ => a.partial_cmp(b).unwrap(),
    }
}

impl<Key> DiagnosticsHierarchy<Key>
where
    Key: AsRef<str>,
{
    /// Sorts the properties and children of the diagnostics hierarchy by name.
    pub fn sort(&mut self) {
        self.properties.sort_by(|p1, p2| name_partial_cmp(p1.name(), p2.name()));
        self.children.sort_by(|c1, c2| name_partial_cmp(&c1.name, &c2.name));
        for child in self.children.iter_mut() {
            child.sort();
        }
    }

    /// Creates a new empty diagnostics hierarchy with the root node named "root".
    pub fn new_root() -> Self {
        DiagnosticsHierarchy::new("root", vec![], vec![])
    }

    /// Creates a new diagnostics hierarchy with the given `name` for the root and the given
    /// `properties` and `children` under that root.
    pub fn new(
        name: impl Into<String>,
        properties: Vec<Property<Key>>,
        children: Vec<DiagnosticsHierarchy<Key>>,
    ) -> Self {
        Self { name: name.into(), properties, children, missing: vec![] }
    }

    /// Either returns an existing child of `self` with name `name` or creates
    /// a new child with name `name`.
    pub fn get_or_add_child_mut<T>(&mut self, name: T) -> &mut DiagnosticsHierarchy<Key>
    where
        T: AsRef<str>,
    {
        // We have to use indices to iterate here because the borrow checker cannot
        // deduce that there are no borrowed values in the else-branch.
        // TODO(fxbug.dev/4601): We could make this cleaner by changing the DiagnosticsHierarchy
        // children to hashmaps.
        match (0..self.children.len()).find(|&i| self.children[i].name == name.as_ref()) {
            Some(matching_index) => &mut self.children[matching_index],
            None => {
                self.children.push(DiagnosticsHierarchy::new(name.as_ref(), vec![], vec![]));
                self.children
                    .last_mut()
                    .expect("We just added an entry so we cannot get None here.")
            }
        }
    }

    /// Add a child to this DiagnosticsHierarchy.
    ///
    /// Note: It is possible to create multiple children with the same name using this method, but
    /// readers may not support such a case.
    pub fn add_child(&mut self, insert: DiagnosticsHierarchy<Key>) {
        self.children.push(insert);
    }

    /// Creates and returns a new Node whose location in a hierarchy
    /// rooted at `self` is defined by node_path.
    ///
    /// Requires: that node_path is not empty.
    /// Requires: that node_path begin with the key fragment equal to the name of the node
    ///           that add is being called on.
    ///
    /// NOTE: Inspect VMOs may allow multiple nodes of the same name. In this case,
    ///        the first node found is returned.
    pub fn get_or_add_node<T>(&mut self, node_path: &[T]) -> &mut DiagnosticsHierarchy<Key>
    where
        T: AsRef<str>,
    {
        assert!(!node_path.is_empty());
        let mut iter = node_path.iter();
        let first_path_string = iter.next().unwrap().as_ref();
        // It is an invariant that the node path start with the key fragment equal to the
        // name of the node that get_or_add_node is called on.
        assert_eq!(first_path_string, &self.name);
        let mut curr_node = self;
        for node_path_entry in iter {
            curr_node = curr_node.get_or_add_child_mut(node_path_entry);
        }
        curr_node
    }

    /// Inserts a new Property into this hierarchy.
    pub fn add_property(&mut self, property: Property<Key>) {
        self.properties.push(property);
    }

    /// Inserts a new Property into a Node whose location in a hierarchy
    /// rooted at `self` is defined by node_path.
    ///
    /// Requires: that node_path is not empty.
    /// Requires: that node_path begin with the key fragment equal to the name of the node
    ///           that add is being called on.
    ///
    /// NOTE: Inspect VMOs may allow multiple nodes of the same name. In this case,
    ///       the property is added to the first node found.
    pub fn add_property_at_path<T>(&mut self, node_path: &[T], property: Property<Key>)
    where
        T: AsRef<str>,
    {
        self.get_or_add_node(node_path).properties.push(property);
    }

    /// Provides an iterator over the diagnostics hierarchy returning properties in pre-order.
    pub fn property_iter(&self) -> DiagnosticsHierarchyIterator<'_, Key> {
        DiagnosticsHierarchyIterator::new(&self)
    }

    /// Adds a value that couldn't be read. This can happen when loading a lazy child.
    pub fn add_missing(&mut self, reason: MissingValueReason, name: String) {
        self.missing.push(MissingValue { reason, name });
    }
    /// Returns the property of the given |name| if one exists.
    pub fn get_property(&self, name: &str) -> Option<&Property<Key>> {
        self.properties.iter().find(|prop| prop.name() == name)
    }

    /// Returns the child of the given |name| if one exists.
    pub fn get_child(&self, name: &str) -> Option<&DiagnosticsHierarchy<Key>> {
        self.children.iter().find(|node| node.name == name)
    }

    /// Returns the child of the given |path| if one exists.
    pub fn get_child_by_path(&self, path: &[&str]) -> Option<&DiagnosticsHierarchy<Key>> {
        let mut result = Some(self);
        for name in path {
            result = result.and_then(|node| node.get_child(name));
        }
        result
    }

    /// Returns the property of the given |name| if one exists.
    pub fn get_property_by_path(&self, path: &[&str]) -> Option<&Property<Key>> {
        let node = self.get_child_by_path(&path[..path.len() - 1]);
        node.and_then(|node| node.get_property(path[path.len() - 1]))
    }
}

macro_rules! property_type_getters {
    ($([$variant:ident, $fn_name:ident, $type:ty]),*) => {
        paste::item! {
          impl<Key> Property<Key> {
              $(
                  #[doc = "Returns the " $variant " value or `None` if the property isn't of that type"]
                  pub fn $fn_name(&self) -> Option<&$type> {
                      match self {
                          Property::$variant(_, value) => Some(value),
                          _ => None,
                      }
                  }
              )*
          }
        }
    }
}

property_type_getters!(
    [String, string, str],
    [Bytes, bytes, [u8]],
    [Int, int, i64],
    [Uint, uint, u64],
    [Double, double, f64],
    [Bool, boolean, bool],
    [DoubleArray, double_array, ArrayContent<f64>],
    [IntArray, int_array, ArrayContent<i64>],
    [UintArray, uint_array, ArrayContent<u64>],
    [StringList, string_list, Vec<String>]
);

struct WorkStackEntry<'a, Key> {
    node: &'a DiagnosticsHierarchy<Key>,
    key: Vec<&'a str>,
}

pub struct DiagnosticsHierarchyIterator<'a, Key> {
    work_stack: Vec<WorkStackEntry<'a, Key>>,
    current_key: Vec<&'a str>,
    current_node: Option<&'a DiagnosticsHierarchy<Key>>,
    current_property_index: usize,
}

impl<'a, Key> DiagnosticsHierarchyIterator<'a, Key> {
    /// Creates a new iterator for the given `hierarchy`.
    fn new(hierarchy: &'a DiagnosticsHierarchy<Key>) -> Self {
        DiagnosticsHierarchyIterator {
            work_stack: vec![WorkStackEntry { node: hierarchy, key: vec![&hierarchy.name] }],
            current_key: vec![],
            current_node: None,
            current_property_index: 0,
        }
    }
}

impl<'a, Key> Iterator for DiagnosticsHierarchyIterator<'a, Key> {
    /// Each item is a path to the node holding the resulting property.
    /// If a node has no properties, a `None` will be returned for it.
    /// If a node has properties a `Some` will be returned for each property and no `None` will be
    /// returned.
    type Item = (Vec<&'a str>, Option<&'a Property<Key>>);

    fn next(&mut self) -> Option<Self::Item> {
        loop {
            let node = match self.current_node {
                // If we are going through a node properties, that node will be set here.
                Some(node) => node,
                None => {
                    // If we don't have a node we are currently working with, then go to the next
                    // node in our stack.
                    let WorkStackEntry { node, key } = match self.work_stack.pop() {
                        None => return None,
                        Some(entry) => entry,
                    };

                    // Push to the stack all children of the new node.
                    for child in node.children.iter() {
                        let mut child_key = key.clone();
                        child_key.push(&child.name);
                        self.work_stack.push(WorkStackEntry { node: child, key: child_key })
                    }

                    // If this node doesn't have any properties, we still want to return that it
                    // exists, so we return with a property=None.
                    if node.properties.is_empty() {
                        return Some((key.clone(), None));
                    }

                    self.current_property_index = 0;
                    self.current_key = key;

                    node
                }
            };

            // We were already done with this node. Try the next item in our stack.
            if self.current_property_index == node.properties.len() {
                self.current_node = None;
                continue;
            }

            // Return the current property and advance our index to the next property we want to
            // explore.
            let property = &node.properties[self.current_property_index];
            self.current_property_index += 1;
            self.current_node = Some(node);

            return Some((self.current_key.clone(), Some(property)));
        }
    }
}

/// A lazy node in a hierarchy.
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
///
/// Key is the type of the property's name and is typically a string. In cases where
/// there are well known, common property names, an alternative may be used to
/// reduce copies of the name.
#[derive(Debug, PartialEq, Clone)]
pub enum Property<Key = String> {
    /// The value is a string.
    String(Key, String),

    /// The value is a bytes vector.
    Bytes(Key, Vec<u8>),

    /// The value is an integer.
    Int(Key, i64),

    /// The value is an unsigned integer.
    Uint(Key, u64),

    /// The value is a double.
    Double(Key, f64),

    /// The value is a boolean.
    Bool(Key, bool),

    /// The value is a double array.
    DoubleArray(Key, ArrayContent<f64>),

    /// The value is an integer array.
    IntArray(Key, ArrayContent<i64>),

    /// The value is an unsigned integer array.
    UintArray(Key, ArrayContent<u64>),

    /// The value is a list of strings.
    StringList(Key, Vec<String>),
}

impl<K> Property<K> {
    /// Returns the key of a property
    pub fn key(&self) -> &K {
        match self {
            Property::String(k, _) => k,
            Property::Bytes(k, _) => k,
            Property::Int(k, _) => k,
            Property::Uint(k, _) => k,
            Property::Double(k, _) => k,
            Property::Bool(k, _) => k,
            Property::DoubleArray(k, _) => k,
            Property::IntArray(k, _) => k,
            Property::UintArray(k, _) => k,
            Property::StringList(k, _) => k,
        }
    }

    /// Returns a string indicating which variant of property this is, useful for printing
    /// debug values.
    pub fn discriminant_name(&self) -> &'static str {
        match self {
            Property::String(_, _) => "String",
            Property::Bytes(_, _) => "Bytes",
            Property::Int(_, _) => "Int",
            Property::IntArray(_, _) => "IntArray",
            Property::Uint(_, _) => "Uint",
            Property::UintArray(_, _) => "UintArray",
            Property::Double(_, _) => "Double",
            Property::DoubleArray(_, _) => "DoubleArray",
            Property::Bool(_, _) => "Bool",
            Property::StringList(_, _) => "StringList",
        }
    }
}

impl<K> Display for Property<K>
where
    K: AsRef<str>,
{
    fn fmt(&self, f: &mut Formatter<'_>) -> FmtResult {
        macro_rules! pair {
            ($fmt:literal, $val:expr) => {
                write!(f, "{}={}", self.key().as_ref(), format_args!($fmt, $val))
            };
        }
        match self {
            Property::String(_, v) => pair!("{}", v),
            Property::Bytes(_, v) => {
                pair!("b64:{}", Base64Display::with_config(v, base64::STANDARD))
            }
            Property::Int(_, v) => pair!("{}", v),
            Property::Uint(_, v) => pair!("{}", v),
            Property::Double(_, v) => pair!("{}", v),
            Property::Bool(_, v) => pair!("{}", v),
            Property::DoubleArray(_, v) => pair!("{:?}", v),
            Property::IntArray(_, v) => pair!("{:?}", v),
            Property::UintArray(_, v) => pair!("{:?}", v),
            Property::StringList(_, v) => pair!("{:?}", v),
        }
    }
}

/// Errors that can happen in this library.
#[derive(Debug, Error)]
pub enum Error {
    #[error(
        "Missing elements for {histogram_type:?} histogram. Expected {expected}, got {actual}"
    )]
    MissingHistogramElements { histogram_type: ArrayFormat, expected: usize, actual: usize },

    #[error("TreeSelector only supports property and subtree selection.")]
    InvalidTreeSelector,

    #[error("Invalid regex")]
    Regex(#[source] regex::Error),

    #[error(transparent)]
    Selectors(#[from] selectors::Error),
}

impl Error {
    fn missing_histogram_elements(
        histogram_type: ArrayFormat,
        actual: usize,
        expected: usize,
    ) -> Self {
        Self::MissingHistogramElements { histogram_type, actual, expected }
    }
}

/// A bucket of a histogram property.
#[derive(Debug, Deserialize, PartialEq, Clone)]
pub struct Bucket<T> {
    /// The floor of the bucket range.
    pub floor: T,

    /// The ceiling of the bucket range.
    #[serde(alias = "upper_bound")]
    pub ceiling: T,

    /// The number of items in this bucket.
    pub count: T,
}

impl<T> Bucket<T> {
    fn new(floor: T, ceiling: T, count: T) -> Self {
        Self { floor, ceiling, count }
    }
}

/// Represents the content of a DiagnosticsHierarchy array property: a regular array or a
/// linear/exponential histogram.
#[derive(Debug, PartialEq, Clone)]
pub enum ArrayContent<T> {
    /// The contents of an array.
    Values(Vec<T>),

    /// The contents of a histogram.
    Buckets(Vec<Bucket<T>>),
}

impl<T: Add<Output = T> + AddAssign + Copy + MulAssign + Bounded> ArrayContent<T> {
    /// Creates a new ArrayContent parsing the `values` based on the given `format`.
    pub fn new(values: Vec<T>, format: ArrayFormat) -> Result<Self, Error> {
        match format {
            ArrayFormat::Default => Ok(Self::Values(values)),
            ArrayFormat::LinearHistogram => {
                let buckets = Self::buckets_for_linear_hist(values)?;
                Ok(Self::Buckets(buckets))
            }
            ArrayFormat::ExponentialHistogram => {
                let buckets = Self::buckets_for_exp_hist(values)?;
                Ok(Self::Buckets(buckets))
            }
        }
    }

    fn buckets_for_linear_hist(values: Vec<T>) -> Result<Vec<Bucket<T>>, Error> {
        // Check that the minimum required values are available:
        // floor, stepsize, underflow, bucket 0, overflow
        if values.len() < 5 {
            return Err(Error::missing_histogram_elements(
                ArrayFormat::LinearHistogram,
                values.len(),
                5,
            ));
        }
        let mut floor = values[0];
        let step_size = values[1];

        let mut result = Vec::new();
        result.push(Bucket::new(T::min_value(), floor, values[2]));
        for i in 3..values.len() - 1 {
            result.push(Bucket::new(floor, floor + step_size, values[i]));
            floor += step_size;
        }
        result.push(Bucket::new(floor, T::max_value(), values[values.len() - 1]));
        Ok(result)
    }

    fn buckets_for_exp_hist(values: Vec<T>) -> Result<Vec<Bucket<T>>, Error> {
        // Check that the minimum required values are available:
        // floor, initial step, step multiplier, underflow, bucket 0, overflow
        if values.len() < 6 {
            return Err(Error::missing_histogram_elements(
                ArrayFormat::ExponentialHistogram,
                values.len(),
                6,
            ));
        }
        let floor = values[0];
        let initial_step = values[1];
        let step_multiplier = values[2];

        let mut result = vec![Bucket::new(T::min_value(), floor, values[3])];

        let mut offset = initial_step;
        let mut current_floor = floor;
        for i in 4..values.len() - 1 {
            let ceiling = floor + offset;
            result.push(Bucket::new(current_floor, ceiling, values[i]));
            offset *= step_multiplier;
            current_floor = ceiling;
        }

        result.push(Bucket::new(current_floor, T::max_value(), values[values.len() - 1]));
        Ok(result)
    }
}

impl<Key> Property<Key>
where
    Key: AsRef<str>,
{
    /// Returns the key of a property.
    pub fn name(&self) -> &str {
        match self {
            Property::String(name, _)
            | Property::Bytes(name, _)
            | Property::Int(name, _)
            | Property::IntArray(name, _)
            | Property::Uint(name, _)
            | Property::UintArray(name, _)
            | Property::Double(name, _)
            | Property::Bool(name, _)
            | Property::DoubleArray(name, _)
            | Property::StringList(name, _) => name.as_ref(),
        }
    }
}

/// Wrapper for the tools needed to filter a single DiagnosticsHierarchy based on selectors
/// known to be applicable to it.
///
/// `component_node_selector` is a RegexSet of all path
///     selectors on a hierarchy.
///
/// `node_property_selectors` is a vector of Regexs that match single named properties
///     on a DiagnosticsHierarchy. NOTE: Their order is aligned with the vector of Regexes that created
///     the component_node_selector RegexSet, since each property selector is associated with
///     a particular node path selector.
#[derive(Clone)]
pub struct InspectHierarchyMatcher {
    /// RegexSet encoding all the node path selectors for
    /// inspect hierarchies under this component's out directory.
    pub component_node_selector: RegexSet,
    /// Vector of strings encoding regexes corresponding to the node path selectors
    /// in the regex set.
    /// Note: Order of regex strings matters here, this vector must be aligned
    /// with the vector used to construct component_node_selector since
    /// conponent_node_selector.matches() returns a vector of ints used to
    /// find all the relevant property selectors corresponding to the matching
    /// node selectors.
    pub node_property_selectors: Vec<String>,
}

impl<T: Borrow<Selector>> TryFrom<Vec<T>> for InspectHierarchyMatcher {
    type Error = Error;

    fn try_from(selectors: Vec<T>) -> Result<Self, Self::Error> {
        selectors[..].try_into()
    }
}

impl<T: Borrow<Selector>> TryFrom<&[T]> for InspectHierarchyMatcher {
    type Error = Error;

    fn try_from(selectors: &[T]) -> Result<Self, Self::Error> {
        let (node_path_regexes, property_regexes): (Vec<_>, Vec<_>) = selectors
            .iter()
            .map(|selector| {
                let component_selector = selector.borrow();
                component_selector.validate().map_err(|e| Error::Selectors(e.into()))?;

                // Unwrapping is safe here since we validate the selector above.
                match component_selector.tree_selector.as_ref().unwrap() {
                    TreeSelector::SubtreeSelector(subtree_selector) => {
                        Ok((
                            selectors::convert_path_selector_to_regex(
                                &subtree_selector.node_path,
                                /*is_subtree_selector=*/ true,
                            )?,
                            selectors::convert_property_selector_to_regex(
                                &StringSelector::StringPattern("*".to_string()),
                            )?,
                        ))
                    }
                    TreeSelector::PropertySelector(property_selector) => {
                        Ok((
                            selectors::convert_path_selector_to_regex(
                                &property_selector.node_path,
                                /*is_subtree_selector=*/ false,
                            )?,
                            selectors::convert_property_selector_to_regex(
                                &property_selector.target_properties,
                            )?,
                        ))
                    }
                    _ => Err(Error::InvalidTreeSelector),
                }
            })
            .collect::<Result<Vec<(String, String)>, Error>>()?
            .into_iter()
            .unzip();

        let node_path_regex_set = RegexSet::new(&node_path_regexes).map_err(Error::Regex)?;

        Ok(InspectHierarchyMatcher {
            component_node_selector: node_path_regex_set,
            node_property_selectors: property_regexes,
        })
    }
}

/// PropertyEntry is a container of Properties, and their locations in the hierarchy.
///
/// eg: `{property_node_path: "root/a/b/c", property: Property::Int("foo", -4)}`
///     is a `PropertyEntry` specifying an integer property named Foo
///     found at node c.
#[derive(Debug, PartialEq)]
pub struct PropertyEntry<Key = String> {
    /// A forward-slash (`/`) delimited string of node names from the root node of a hierarchy
    /// to the node holding the Property.
    /// eg: `root/a/b/c` is a property_node_path specifying that `property`
    ///     can found at node c, under node b, which is under node a, which is under root.
    pub property_node_path: String,

    /// A clone of the property found in the diagnostics hierarchy at the node specified by
    /// `property_node_path`.
    pub property: Property<Key>,
}

/// Applies a single selector to a `DiagnosticsHierarchy`, returning a vector of tuples for every
/// property in the hierarchy matched by the selector.
// TODO(fxbug.dev/47015): Benchmark performance issues with full-filters for selection.
pub fn select_from_hierarchy<Key>(
    root_node: DiagnosticsHierarchy<Key>,
    selector: Selector,
) -> Result<Vec<PropertyEntry<Key>>, Error>
where
    Key: AsRef<str> + Clone,
{
    let single_selector_hierarchy_matcher = vec![Arc::new(selector)].try_into()?;

    // TODO(fxbug.dev/47015): Extraction doesn't require a full tree filter. Instead, the hierarchy
    // should be traversed like a state machine, and all matching nodes should search for
    // their properties.
    let filtered_hierarchy = filter_hierarchy(root_node, &single_selector_hierarchy_matcher)?;

    match filtered_hierarchy {
        Some(new_root) => Ok(new_root
            .property_iter()
            .filter_map(|(node_path, property_opt)| {
                let formatted_node_path = node_path
                    .iter()
                    .map(|s| selectors::sanitize_string_for_selectors(s))
                    .collect::<Vec<String>>()
                    .join("/");

                property_opt.map(|property| PropertyEntry {
                    property_node_path: formatted_node_path,
                    property: property.clone(),
                })
            })
            .collect::<Vec<PropertyEntry<Key>>>()),
        None => Ok(Vec::new()),
    }
}

/// Filters a diagnostics hierarchy using a set of path selectors and their associated property
/// selectors.
///
/// - If the return type is Ok(Some()) that implies that the filter encountered no errors AND
///    a meaningful tree remained at the end.
/// - If the return type is Ok(None) that implies that the filter encountered no errors AND
///    the tree was filtered to be empty at the end.
/// - If the return type is Error that implies the filter encountered errors.
pub fn filter_hierarchy<Key>(
    root_node: DiagnosticsHierarchy<Key>,
    hierarchy_matcher: &InspectHierarchyMatcher,
) -> Result<Option<DiagnosticsHierarchy<Key>>, Error>
where
    Key: AsRef<str> + Clone,
{
    let mut nodes_added = 0;

    let mut new_root = DiagnosticsHierarchy::new(root_node.name.clone(), vec![], vec![]);

    let mut working_node: &mut DiagnosticsHierarchy<Key> = &mut new_root;
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

        // TODO(fxbug.dev/44926): If any of the selectors in the current set are a
        // subtree selector, we dont have to compile new property selector sets
        // until we iterate out of the subtree.
        let property_regex_set: &RegexSet = match &working_node_path {
            Some(working_path) if *working_path == formatted_node_path => {
                working_property_regex_set.as_ref().unwrap()
            }
            _ => {
                // Either we never created a property Regex or we've iterated
                // to a new trie node. Either way, we need to find the relevant
                // selectors for the current node and create a new property
                // regex set.
                let property_regex_strings = hierarchy_matcher
                    .component_node_selector
                    .matches(&formatted_node_path)
                    .into_iter()
                    .map(|property_index| {
                        &hierarchy_matcher.node_property_selectors[property_index]
                    })
                    .collect::<Vec<&String>>();

                let property_regex_set =
                    RegexSet::new(property_regex_strings).map_err(Error::Regex)?;

                if property_regex_set.len() > 0 {
                    working_node = new_root.get_or_add_node(&node_path);
                    nodes_added = nodes_added + 1;
                }

                working_node_path = Some(formatted_node_path);
                working_property_regex_set = Some(property_regex_set);

                working_property_regex_set.as_ref().unwrap()
            }
        };

        if property_regex_set.len() == 0 {
            continue;
        }

        match property {
            Some(property) => {
                if property_regex_set
                    .is_match(&selectors::sanitize_string_for_selectors(property.name()))
                {
                    // TODO(fxbug.dev/4601): We can keep track of the prefix string identifying
                    // the "curr_node" and only insert from root if our iteration has
                    // brought us to a new node higher up the hierarchy. Right now, we
                    // insert from root for every new property.
                    working_node.properties.push(property.clone());
                }
            }
            None => {
                continue;
            }
        }
    }

    if nodes_added > 0 {
        Ok(Some(new_root))
    } else {
        Ok(None)
    }
}

/// The parameters of an exponential histogram.
#[derive(Clone)]
pub struct ExponentialHistogramParams<T: Clone> {
    /// The floor of the exponential histogram.
    pub floor: T,

    /// The initial step of the exponential histogram.
    pub initial_step: T,

    /// The step multiplier of the exponential histogram.
    pub step_multiplier: T,

    /// The number of buckets that the exponential histogram can have. This doesn't include the
    /// overflow and underflow buckets.
    pub buckets: usize,
}

/// The parameters of a linear histogram.
#[derive(Clone)]
pub struct LinearHistogramParams<T: Clone> {
    /// The floor of the linear histogram.
    pub floor: T,

    /// The step size of the linear histogram.
    pub step_size: T,

    /// The number of buckets that the linear histogram can have. This doesn't include the overflow
    /// and underflow buckets.
    pub buckets: usize,
}

#[cfg(test)]
mod tests {
    use {super::*, assert_matches::assert_matches, selectors::VerboseError};

    fn validate_hierarchy_iteration(
        mut results_vec: Vec<(Vec<String>, Option<Property>)>,
        test_hierarchy: DiagnosticsHierarchy,
    ) {
        let expected_num_entries = results_vec.len();
        let mut num_entries = 0;
        for (key, val) in test_hierarchy.property_iter() {
            num_entries = num_entries + 1;
            let (expected_key, expected_property) = results_vec.pop().unwrap();
            assert_eq!(
                key.iter().map(|s| *s).collect::<Vec<&str>>().join("/"),
                expected_key.iter().map(|s| s.as_str()).collect::<Vec<&str>>().join("/")
            );

            assert_eq!(val, expected_property.as_ref());
        }

        assert_eq!(num_entries, expected_num_entries);
    }

    #[fuchsia::test]
    fn test_diagnostics_hierarchy_iteration() {
        let double_array_data = vec![-1.2, 2.3, 3.4, 4.5, -5.6];
        let chars = ['a', 'b', 'c', 'd', 'e', 'f', 'g'];
        let string_data = chars.iter().cycle().take(6000).collect::<String>();
        let bytes_data = (0u8..=9u8).cycle().take(5000).collect::<Vec<u8>>();

        let test_hierarchy = DiagnosticsHierarchy::new(
            "root".to_string(),
            vec![
                Property::Int("int-root".to_string(), 3),
                Property::DoubleArray(
                    "property-double-array".to_string(),
                    ArrayContent::Values(double_array_data.clone()),
                ),
            ],
            vec![DiagnosticsHierarchy::new(
                "child-1".to_string(),
                vec![
                    Property::Uint("property-uint".to_string(), 10),
                    Property::Double("property-double".to_string(), -3.4),
                    Property::String("property-string".to_string(), string_data.clone()),
                    Property::IntArray(
                        "property-int-array".to_string(),
                        ArrayContent::new(vec![1, 2, 1, 1, 1, 1, 1], ArrayFormat::LinearHistogram)
                            .unwrap(),
                    ),
                ],
                vec![DiagnosticsHierarchy::new(
                    "child-1-1".to_string(),
                    vec![
                        Property::Int("property-int".to_string(), -9),
                        Property::Bytes("property-bytes".to_string(), bytes_data.clone()),
                        Property::UintArray(
                            "property-uint-array".to_string(),
                            ArrayContent::new(
                                vec![1, 1, 2, 0, 1, 1, 2, 0, 0],
                                ArrayFormat::ExponentialHistogram,
                            )
                            .unwrap(),
                        ),
                    ],
                    vec![],
                )],
            )],
        );

        let results_vec = vec![
            (
                vec!["root".to_string(), "child-1".to_string(), "child-1-1".to_string()],
                Some(Property::UintArray(
                    "property-uint-array".to_string(),
                    ArrayContent::new(
                        vec![1, 1, 2, 0, 1, 1, 2, 0, 0],
                        ArrayFormat::ExponentialHistogram,
                    )
                    .unwrap(),
                )),
            ),
            (
                vec!["root".to_string(), "child-1".to_string(), "child-1-1".to_string()],
                Some(Property::Bytes("property-bytes".to_string(), bytes_data)),
            ),
            (
                vec!["root".to_string(), "child-1".to_string(), "child-1-1".to_string()],
                Some(Property::Int("property-int".to_string(), -9)),
            ),
            (
                vec!["root".to_string(), "child-1".to_string()],
                Some(Property::IntArray(
                    "property-int-array".to_string(),
                    ArrayContent::new(vec![1, 2, 1, 1, 1, 1, 1], ArrayFormat::LinearHistogram)
                        .unwrap(),
                )),
            ),
            (
                vec!["root".to_string(), "child-1".to_string()],
                Some(Property::String("property-string".to_string(), string_data)),
            ),
            (
                vec!["root".to_string(), "child-1".to_string()],
                Some(Property::Double("property-double".to_string(), -3.4)),
            ),
            (
                vec!["root".to_string(), "child-1".to_string()],
                Some(Property::Uint("property-uint".to_string(), 10)),
            ),
            (
                vec!["root".to_string()],
                Some(Property::DoubleArray(
                    "property-double-array".to_string(),
                    ArrayContent::Values(double_array_data),
                )),
            ),
            (vec!["root".to_string()], Some(Property::Int("int-root".to_string(), 3))),
        ];

        validate_hierarchy_iteration(results_vec, test_hierarchy);
    }

    #[fuchsia::test]
    fn test_getters() {
        let a_prop = Property::Int("a".to_string(), 1);
        let b_prop = Property::Uint("b".to_string(), 2);
        let child2 = DiagnosticsHierarchy::new("child2".to_string(), vec![], vec![]);
        let child = DiagnosticsHierarchy::new(
            "child".to_string(),
            vec![b_prop.clone()],
            vec![child2.clone()],
        );
        let hierarchy = DiagnosticsHierarchy::new(
            "root".to_string(),
            vec![a_prop.clone()],
            vec![child.clone()],
        );
        assert_matches!(hierarchy.get_child("child"), Some(node) if *node == child);
        assert_matches!(hierarchy.get_child_by_path(&vec!["child", "child2"]),
                        Some(node) if *node == child2);
        assert_matches!(hierarchy.get_property("a"), Some(prop) if *prop == a_prop);
        assert_matches!(hierarchy.get_property_by_path(&vec!["child", "b"]),
                        Some(prop) if *prop == b_prop);
    }

    #[fuchsia::test]
    fn test_edge_case_hierarchy_iteration() {
        let root_only_with_one_property_hierarchy = DiagnosticsHierarchy::new(
            "root".to_string(),
            vec![Property::Int("property-int".to_string(), -9)],
            vec![],
        );

        let results_vec =
            vec![(vec!["root".to_string()], Some(Property::Int("property-int".to_string(), -9)))];

        validate_hierarchy_iteration(results_vec, root_only_with_one_property_hierarchy);

        let empty_hierarchy = DiagnosticsHierarchy::new("root".to_string(), vec![], vec![]);

        let results_vec = vec![(vec!["root".to_string()], None)];

        validate_hierarchy_iteration(results_vec, empty_hierarchy);

        let empty_root_populated_child = DiagnosticsHierarchy::new(
            "root",
            vec![],
            vec![DiagnosticsHierarchy::new(
                "foo",
                vec![Property::Int("11".to_string(), -4)],
                vec![],
            )],
        );

        let results_vec = vec![
            (
                vec!["root".to_string(), "foo".to_string()],
                Some(Property::Int("11".to_string(), -4)),
            ),
            (vec!["root".to_string()], None),
        ];

        validate_hierarchy_iteration(results_vec, empty_root_populated_child);

        let empty_root_empty_child = DiagnosticsHierarchy::new(
            "root",
            vec![],
            vec![DiagnosticsHierarchy::new("foo", vec![], vec![])],
        );

        let results_vec = vec![
            (vec!["root".to_string(), "foo".to_string()], None),
            (vec!["root".to_string()], None),
        ];

        validate_hierarchy_iteration(results_vec, empty_root_empty_child);
    }

    #[fuchsia::test]
    fn array_value() {
        let values = vec![1, 2, 5, 7, 9, 11, 13];
        let array = ArrayContent::<u64>::new(values.clone(), ArrayFormat::Default);
        assert_matches!(array, Ok(ArrayContent::Values(vals)) if vals == values);
    }

    #[fuchsia::test]
    fn linear_histogram_array_value() {
        let values = vec![1, 2, 5, 7, 9, 11, 13];
        let array = ArrayContent::<i64>::new(values, ArrayFormat::LinearHistogram);
        assert_matches!(array, Ok(ArrayContent::Buckets(buckets)) if buckets == vec![
            Bucket { floor: std::i64::MIN, ceiling: 1, count: 5 },
            Bucket { floor: 1, ceiling: 3, count: 7 },
            Bucket { floor: 3, ceiling: 5, count: 9 },
            Bucket { floor: 5, ceiling: 7, count: 11 },
            Bucket { floor: 7, ceiling: std::i64::MAX, count: 13 },
        ]);
    }

    #[fuchsia::test]
    fn exponential_histogram_array_value() {
        let values = vec![1.0, 2.0, 5.0, 7.0, 9.0, 11.0, 15.0];
        let array = ArrayContent::<f64>::new(values, ArrayFormat::ExponentialHistogram);
        assert_matches!(array, Ok(ArrayContent::Buckets(buckets)) if buckets == vec![
                Bucket { floor: std::f64::MIN, ceiling: 1.0, count: 7.0 },
                Bucket { floor: 1.0, ceiling: 3.0, count: 9.0 },
                Bucket { floor: 3.0, ceiling: 11.0, count: 11.0 },
                Bucket { floor: 11.0, ceiling: std::f64::MAX, count: 15.0 },
        ]);
    }

    #[fuchsia::test]
    fn deserialize_buckets() -> Result<(), serde_json::Error> {
        let json_string = r#"{
            "root": {
                "histogram": {
                  "buckets": [
                    {
                      "count": 4,
                      "floor": -9223372036854775808,
                      "ceiling": 0
                    },
                    {
                      "count": 1,
                      "floor": 0,
                      "upper_bound": 2
                    },
                    {
                      "count": 3,
                      "floor": 2,
                      "ceiling": 9223372036854775807
                    }
                  ]
                }
            }
          }"#;
        let json_bad_missing_field = r#"{
            "root": {
                "histogram": {
                  "buckets": [
                    {
                      "count": 4,
                      "floor": -9223372036854775808
                    },
                    {
                      "count": 1,
                      "floor": 0,
                      "upper_bound": 2
                    },
                    {
                      "count": 3,
                      "floor": 2,
                      "ceiling": 9223372036854775807
                    }
                  ]
                }
            }
          }"#;
        let json_bad_extra_field = r#"{
            "root": {
                "histogram": {
                  "buckets": [
                    {
                      "count": 4,
                      "floor": -9223372036854775808,
                      "ceiling": 0,
                      "upper_bound": 0
                    },
                    {
                      "count": 1,
                      "floor": 0,
                      "upper_bound": 2
                    },
                    {
                      "count": 3,
                      "floor": 2,
                      "ceiling": 9223372036854775807
                    }
                  ]
                }
            }
          }"#;

        let expected_hierarchy = DiagnosticsHierarchy::new(
            "root".to_string(),
            vec![Property::IntArray(
                "histogram".to_string(),
                ArrayContent::new(vec![0, 2, 4, 1, 3], ArrayFormat::LinearHistogram).unwrap(),
            )],
            vec![],
        );
        let parsed_hierarchy: DiagnosticsHierarchy =
            serde_json::from_value(serde_json::from_str(json_string)?)?;
        let missing_hierarchy: Result<DiagnosticsHierarchy, serde_json::Error> =
            serde_json::from_value(serde_json::from_str(json_bad_missing_field)?);
        let extra_hierarchy: Result<DiagnosticsHierarchy, serde_json::Error> =
            serde_json::from_value(serde_json::from_str(json_bad_extra_field)?);

        assert_eq!(expected_hierarchy, parsed_hierarchy);
        assert_matches!(missing_hierarchy, Err(_));
        assert_matches!(extra_hierarchy, Err(_));
        Ok(())
    }

    #[fuchsia::test]
    fn exponential_histogram_buckets() {
        let values = vec![0, 2, 4, 0, 1, 2, 3, 4, 5];
        let array = ArrayContent::new(values, ArrayFormat::ExponentialHistogram);
        assert_matches!(array, Ok(ArrayContent::Buckets(buckets)) if buckets == vec![
                Bucket { floor: i64::min_value(), ceiling: 0, count: 0 },
                Bucket { floor: 0, ceiling: 2, count: 1 },
                Bucket { floor: 2, ceiling: 8, count: 2 },
                Bucket { floor: 8, ceiling: 32, count: 3 },
                Bucket { floor: 32, ceiling: 128, count: 4 },
                Bucket { floor: 128, ceiling: i64::max_value(), count: 5 },
        ]);
    }

    #[fuchsia::test]
    fn add_to_hierarchy() {
        let mut hierarchy = DiagnosticsHierarchy::new_root();
        let prop_1 = Property::String("x".to_string(), "foo".to_string());
        let path_1 = vec!["root", "one"];
        let prop_2 = Property::Uint("c".to_string(), 3);
        let path_2 = vec!["root", "two"];
        let prop_2_prime = Property::Int("z".to_string(), -4);
        hierarchy.add_property_at_path(&path_1, prop_1.clone());
        hierarchy.add_property_at_path(&path_2.clone(), prop_2.clone());
        hierarchy.add_property_at_path(&path_2, prop_2_prime.clone());

        assert_eq!(
            hierarchy,
            DiagnosticsHierarchy {
                name: "root".to_string(),
                children: vec![
                    DiagnosticsHierarchy {
                        name: "one".to_string(),
                        properties: vec![prop_1],
                        children: vec![],
                        missing: vec![],
                    },
                    DiagnosticsHierarchy {
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

    #[fuchsia::test]
    fn string_lists() {
        let mut hierarchy = DiagnosticsHierarchy::new_root();
        let prop_1 =
            Property::StringList("x".to_string(), vec!["foo".to_string(), "bar".to_string()]);
        let path_1 = vec!["root", "one"];
        hierarchy.add_property_at_path(&path_1, prop_1.clone());

        assert_eq!(
            hierarchy,
            DiagnosticsHierarchy {
                name: "root".to_string(),
                children: vec![DiagnosticsHierarchy {
                    name: "one".to_string(),
                    properties: vec![prop_1],
                    children: vec![],
                    missing: vec![],
                },],
                properties: vec![],
                missing: vec![],
            }
        );
    }

    #[fuchsia::test]
    #[should_panic]
    // Empty paths are meaningless on insertion and break the method invariant.
    fn no_empty_paths_allowed() {
        let mut hierarchy = DiagnosticsHierarchy::<String>::new_root();
        let path_1: Vec<&String> = vec![];
        hierarchy.get_or_add_node(&path_1);
    }

    #[fuchsia::test]
    #[should_panic]
    // Paths provided to add must begin at the node we're calling
    // add() on.
    fn path_must_start_at_self() {
        let mut hierarchy = DiagnosticsHierarchy::<String>::new_root();
        let path_1 = vec!["not_root", "a"];
        hierarchy.get_or_add_node(&path_1);
    }

    #[fuchsia::test]
    fn sort_hierarchy() {
        let mut hierarchy = DiagnosticsHierarchy::new(
            "root",
            vec![
                Property::String("x".to_string(), "foo".to_string()),
                Property::Uint("c".to_string(), 3),
                Property::Int("z".to_string(), -4),
            ],
            vec![
                DiagnosticsHierarchy::new(
                    "foo",
                    vec![
                        Property::Int("11".to_string(), -4),
                        Property::Bytes("123".to_string(), "foo".bytes().into_iter().collect()),
                        Property::Double("0".to_string(), 8.1),
                    ],
                    vec![],
                ),
                DiagnosticsHierarchy::new("bar", vec![], vec![]),
            ],
        );

        hierarchy.sort();

        let sorted_hierarchy = DiagnosticsHierarchy::new(
            "root",
            vec![
                Property::Uint("c".to_string(), 3),
                Property::String("x".to_string(), "foo".to_string()),
                Property::Int("z".to_string(), -4),
            ],
            vec![
                DiagnosticsHierarchy::new("bar", vec![], vec![]),
                DiagnosticsHierarchy::new(
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

    fn parse_selectors_and_filter_hierarchy(
        hierarchy: DiagnosticsHierarchy,
        test_selectors: Vec<&str>,
    ) -> DiagnosticsHierarchy {
        let parsed_test_selectors = test_selectors
            .into_iter()
            .map(|selector_string| {
                Arc::new(
                    selectors::parse_selector::<VerboseError>(selector_string)
                        .expect("All test selectors are valid and parsable."),
                )
            })
            .collect::<Vec<Arc<Selector>>>();

        let hierarchy_matcher: InspectHierarchyMatcher = parsed_test_selectors.try_into().unwrap();

        let mut filtered_hierarchy = filter_hierarchy(hierarchy, &hierarchy_matcher)
            .expect("filtered hierarchy should succeed.")
            .expect("There should be an actual resulting hierarchy.");
        filtered_hierarchy.sort();
        filtered_hierarchy
    }

    fn parse_selector_and_select_from_hierarchy(
        hierarchy: DiagnosticsHierarchy,
        test_selector: &str,
    ) -> Vec<PropertyEntry> {
        let parsed_selector = selectors::parse_selector::<VerboseError>(test_selector)
            .expect("All test selectors are valid and parsable.");

        select_from_hierarchy(hierarchy, parsed_selector)
            .expect("Selecting from hierarchy should succeed.")
    }

    fn get_test_hierarchy() -> DiagnosticsHierarchy {
        DiagnosticsHierarchy::new(
            "root",
            vec![
                Property::String("x".to_string(), "foo".to_string()),
                Property::Uint("c".to_string(), 3),
                Property::Int("z".to_string(), -4),
            ],
            vec![
                DiagnosticsHierarchy::new(
                    "foo",
                    vec![
                        Property::Int("11".to_string(), -4),
                        Property::Bytes("123".to_string(), "foo".bytes().into_iter().collect()),
                        Property::Double("0".to_string(), 8.1),
                    ],
                    vec![DiagnosticsHierarchy::new(
                        "zed",
                        vec![Property::Int("13".to_string(), -4)],
                        vec![],
                    )],
                ),
                DiagnosticsHierarchy::new(
                    "bar",
                    vec![Property::Int("12".to_string(), -4)],
                    vec![DiagnosticsHierarchy::new(
                        "zed",
                        vec![Property::Int("13/:".to_string(), -4)],
                        vec![],
                    )],
                ),
            ],
        )
    }

    #[fuchsia::test]
    fn test_filter_hierarchy() {
        let test_selectors = vec!["*:root/foo:11", "*:root:z", r#"*:root/bar/zed:13\/\:"#];

        assert_eq!(
            parse_selectors_and_filter_hierarchy(get_test_hierarchy(), test_selectors),
            DiagnosticsHierarchy::new(
                "root",
                vec![Property::Int("z".to_string(), -4),],
                vec![
                    DiagnosticsHierarchy::new(
                        "bar",
                        vec![],
                        vec![DiagnosticsHierarchy::new(
                            "zed",
                            vec![Property::Int("13/:".to_string(), -4)],
                            vec![],
                        )],
                    ),
                    DiagnosticsHierarchy::new(
                        "foo",
                        vec![Property::Int("11".to_string(), -4),],
                        vec![],
                    )
                ],
            )
        );

        let test_selectors = vec!["*:root"];
        let mut sorted_expected = get_test_hierarchy();
        sorted_expected.sort();
        assert_eq!(
            parse_selectors_and_filter_hierarchy(get_test_hierarchy(), test_selectors),
            sorted_expected
        );
    }

    #[fuchsia::test]
    fn test_filter_includes_empty_node() {
        let test_selectors = vec!["*:root/foo:blorg"];

        assert_eq!(
            parse_selectors_and_filter_hierarchy(get_test_hierarchy(), test_selectors),
            DiagnosticsHierarchy::new(
                "root",
                vec![],
                vec![DiagnosticsHierarchy::new("foo", vec![], vec![],)],
            )
        );
    }

    #[fuchsia::test]
    fn test_subtree_selection_includes_empty_nodes() {
        let test_selectors = vec!["*:root"];
        let mut empty_hierarchy = DiagnosticsHierarchy::new(
            "root",
            vec![],
            vec![
                DiagnosticsHierarchy::new(
                    "foo",
                    vec![],
                    vec![DiagnosticsHierarchy::new("zed", vec![], vec![])],
                ),
                DiagnosticsHierarchy::new(
                    "bar",
                    vec![],
                    vec![DiagnosticsHierarchy::new("zed", vec![], vec![])],
                ),
            ],
        );

        empty_hierarchy.sort();

        assert_eq!(
            parse_selectors_and_filter_hierarchy(empty_hierarchy.clone(), test_selectors),
            empty_hierarchy.clone()
        );
    }

    #[fuchsia::test]
    fn test_empty_tree_filtering() {
        // Subtree selection on the empty tree should produce the empty tree.
        let mut empty_hierarchy = DiagnosticsHierarchy::new("root", vec![], vec![]);
        empty_hierarchy.sort();

        let subtree_selector = vec!["*:root"];
        assert_eq!(
            parse_selectors_and_filter_hierarchy(empty_hierarchy.clone(), subtree_selector),
            empty_hierarchy.clone()
        );

        // Selecting a property on the root, even if it doesn't exist, should produce the empty tree.
        let fake_property_selector = vec!["*:root:blorp"];
        assert_eq!(
            parse_selectors_and_filter_hierarchy(empty_hierarchy.clone(), fake_property_selector),
            empty_hierarchy.clone()
        );
    }

    #[fuchsia::test]
    fn test_select_from_hierarchy() {
        let test_cases = vec![
            (
                "*:root/foo:11",
                vec![PropertyEntry {
                    property_node_path: "root/foo".to_string(),
                    property: Property::Int("11".to_string(), -4),
                }],
            ),
            (
                "*:root/foo:*",
                vec![
                    PropertyEntry {
                        property_node_path: "root/foo".to_string(),
                        property: Property::Double("0".to_string(), 8.1),
                    },
                    PropertyEntry {
                        property_node_path: "root/foo".to_string(),
                        property: Property::Int("11".to_string(), -4),
                    },
                    PropertyEntry {
                        property_node_path: "root/foo".to_string(),
                        property: Property::Bytes(
                            "123".to_string(),
                            "foo".bytes().into_iter().collect(),
                        ),
                    },
                ],
            ),
            ("*:root/foo:nonexistant", vec![]),
            (
                "*:root/foo",
                vec![
                    PropertyEntry {
                        property_node_path: "root/foo".to_string(),
                        property: Property::Double("0".to_string(), 8.1),
                    },
                    PropertyEntry {
                        property_node_path: "root/foo".to_string(),
                        property: Property::Int("11".to_string(), -4),
                    },
                    PropertyEntry {
                        property_node_path: "root/foo".to_string(),
                        property: Property::Bytes(
                            "123".to_string(),
                            "foo".bytes().into_iter().collect(),
                        ),
                    },
                    PropertyEntry {
                        property_node_path: "root/foo/zed".to_string(),
                        property: Property::Int("13".to_string(), -4),
                    },
                ],
            ),
        ];

        for (test_selector, expected_vector) in test_cases {
            let mut property_entry_vec =
                parse_selector_and_select_from_hierarchy(get_test_hierarchy(), test_selector);

            property_entry_vec.sort_by(|p1, p2| {
                let p1_string = format!("{}/{}", p1.property_node_path, p1.property.name());
                let p2_string = format!("{}/{}", p2.property_node_path, p2.property.name());
                p1_string.cmp(&p2_string)
            });
            assert_eq!(property_entry_vec, expected_vector);
        }
    }

    #[fuchsia::test]
    fn sort_numerical_value() {
        let mut diagnostics_hierarchy = DiagnosticsHierarchy::new(
            "root",
            vec![
                Property::Double("2".to_string(), 2.3),
                Property::Int("0".to_string(), -4),
                Property::Uint("10".to_string(), 3),
                Property::String("1".to_string(), "test".to_string()),
            ],
            vec![
                DiagnosticsHierarchy::new("123", vec![], vec![]),
                DiagnosticsHierarchy::new("34", vec![], vec![]),
                DiagnosticsHierarchy::new("4", vec![], vec![]),
                DiagnosticsHierarchy::new("023", vec![], vec![]),
                DiagnosticsHierarchy::new("12", vec![], vec![]),
                DiagnosticsHierarchy::new("1", vec![], vec![]),
            ],
        );
        diagnostics_hierarchy.sort();
        assert_eq!(
            diagnostics_hierarchy,
            DiagnosticsHierarchy::new(
                "root",
                vec![
                    Property::Int("0".to_string(), -4),
                    Property::String("1".to_string(), "test".to_string()),
                    Property::Double("2".to_string(), 2.3),
                    Property::Uint("10".to_string(), 3),
                ],
                vec![
                    DiagnosticsHierarchy::new("1", vec![], vec![]),
                    DiagnosticsHierarchy::new("4", vec![], vec![]),
                    DiagnosticsHierarchy::new("12", vec![], vec![]),
                    DiagnosticsHierarchy::new("023", vec![], vec![]),
                    DiagnosticsHierarchy::new("34", vec![], vec![]),
                    DiagnosticsHierarchy::new("123", vec![], vec![]),
                ]
            )
        );
    }
}
