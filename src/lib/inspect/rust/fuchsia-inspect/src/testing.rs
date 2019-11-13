// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::{
    reader::{NodeHierarchy, Property},
    Inspector,
};

use failure::{bail, Error};
use std::borrow::Cow;
use std::collections::HashSet;

/// Macro to simplify tree matching in tests. The first argument is the actual tree and can be
/// a NodeHierarchy or an Inspector. The second argument is an matcher/assertion expression to
/// compare against the tree.
///
/// Each leaf value must be a type that implements `PropertyAssertion`.
///
/// Example:
/// ```
/// // Actual tree
/// let node_hierarchy = NodeHierarchy {
///     name: "key".to_string(),
///     properties: vec![
///         Property::String("sub".to_string(), "sub_value".to_string()),
///         Property::String("sub2".to_string(), "sub2_value".to_string()),
///     ],
///     children: vec![
///        NodeHierarchy {
///            name: "child1".to_string(),
///            properties: vec![
///                Property::Int("child1_sub".to_string(), 10i64),
///            ],
///            children: vec![],
///        },
///        NodeHierarchy {
///            name: "child2".to_string(),
///            properties: vec![
///                Property::Uint("child2_sub".to_string(), 20u64),
///            ],
///            children: vec![],
///        },
///    ],
/// };
///
/// assert_inspect_tree!(node_hierarchy, key: {
///     sub: AnyProperty,   // only verify that `sub` is a property of `key`
///     sub2: "sub2_value",
///     child1: {
///         child1_sub: 10i64,
///     },
///     child2: {
///         child2_sub: 20u64,
///     },
/// });
/// ```
///
/// In order to do partial match on a tree, use the `contains` keyword:
/// ```
/// assert_inspect_tree!(node_hierarchy, key: contains {
///     sub: "sub_value",
///     child1: contains {},
/// });
/// ```
///
/// The first argument can be an Inspector, in which case the whole tree is read from the VMO and
/// matched against:
/// ```
/// let inspector = Inspector::new().unwrap();
/// assert_inspect_tree!(inspector, root: {});
/// ```
#[macro_export]
macro_rules! assert_inspect_tree {
    (@build $tree_assertion:expr,) => {};

    // Exact match of tree
    (@build $tree_assertion:expr, var $key:ident: { $($sub:tt)* }) => {{
        #[allow(unused_mut)]
        let mut child_tree_assertion = TreeAssertion::new($key, true);
        assert_inspect_tree!(@build child_tree_assertion, $($sub)*);
        $tree_assertion.add_child_assertion(child_tree_assertion);
    }};
    (@build $tree_assertion:expr, var $key:ident: { $($sub:tt)* }, $($rest:tt)*) => {{
        assert_inspect_tree!(@build $tree_assertion, var $key: { $($sub)* });
        assert_inspect_tree!(@build $tree_assertion, $($rest)*);
    }};

    // Partial match of tree
    (@build $tree_assertion:expr, var $key:ident: contains { $($sub:tt)* }) => {{
        #[allow(unused_mut)]
        let mut child_tree_assertion = TreeAssertion::new($key, false);
        assert_inspect_tree!(@build child_tree_assertion, $($sub)*);
        $tree_assertion.add_child_assertion(child_tree_assertion);
    }};
    (@build $tree_assertion:expr, var $key:ident: contains { $($sub:tt)* }, $($rest:tt)*) => {{
        assert_inspect_tree!(@build $tree_assertion, var $key: contains { $($sub)* });
        assert_inspect_tree!(@build $tree_assertion, $($rest)*);
    }};

    // Matching properties of a tree
    (@build $tree_assertion:expr, var $key:ident: $assertion:expr) => {{
        $tree_assertion.add_property_assertion($key, Box::new($assertion))
    }};
    (@build $tree_assertion:expr, var $key:ident: $assertion:expr, $($rest:tt)*) => {{
        assert_inspect_tree!(@build $tree_assertion, var $key: $assertion);
        assert_inspect_tree!(@build $tree_assertion, $($rest)*);
    }};

    // Key identifier format
    (@build $tree_assertion:expr, $key:ident: $($rest:tt)+) => {{
        let key = stringify!($key);
        assert_inspect_tree!(@build $tree_assertion, var key: $($rest)+);
    }};
    // Allows string literal for key
    (@build $tree_assertion:expr, $key:tt: $($rest:tt)+) => {{
        let key: &'static str = $key;
        assert_inspect_tree!(@build $tree_assertion, var key: $($rest)+);
    }};

    // Entry points
    ($node_hierarchy:expr, var $key:ident: { $($sub:tt)* }) => {{
        use $crate::testing::{NodeHierarchyGetter, TreeAssertion};
        #[allow(unused_mut)]
        let mut tree_assertion = TreeAssertion::new($key, true);
        assert_inspect_tree!(@build tree_assertion, $($sub)*);
        if let Err(e) = tree_assertion.run($node_hierarchy.get_node_hierarchy().as_ref()) {
            panic!("tree assertion fails: {}", e);
        }
    }};
    ($node_hierarchy:expr, var $key:ident: contains { $($sub:tt)* }) => {{
        use $crate::testing::{NodeHierarchyGetter, TreeAssertion};
        #[allow(unused_mut)]
        let mut tree_assertion = TreeAssertion::new($key, false);
        assert_inspect_tree!(@build tree_assertion, $($sub)*);
        if let Err(e) = tree_assertion.run($node_hierarchy.get_node_hierarchy().as_ref()) {
            panic!("tree assertion fails: {}", e);
        }
    }};
    ($node_hierarchy:expr, $key:ident: $($rest:tt)+) => {{
        let key = stringify!($key);
        assert_inspect_tree!($node_hierarchy, var key: $($rest)+);
    }};
    ($node_hierarchy:expr, $key:tt: $($rest:tt)+) => {{
        let key: &'static str = $key;
        assert_inspect_tree!($node_hierarchy, var key: $($rest)+);
    }};
}

#[allow(missing_docs)]
pub trait NodeHierarchyGetter {
    fn get_node_hierarchy(&self) -> Cow<NodeHierarchy>;
}

impl NodeHierarchyGetter for NodeHierarchy {
    fn get_node_hierarchy(&self) -> Cow<NodeHierarchy> {
        Cow::Borrowed(self)
    }
}

impl NodeHierarchyGetter for Inspector {
    fn get_node_hierarchy(&self) -> Cow<NodeHierarchy> {
        use std::convert::TryFrom;
        Cow::Owned(NodeHierarchy::try_from(self).unwrap())
    }
}

macro_rules! eq_or_bail {
    ($expected:expr, $actual:expr) => {{
        if $expected != $actual {
            bail!("\n Expected: {:?}\n      Got: {:?}", $expected, $actual);
        }
    }};
    ($expected:expr, $actual:expr, $($args:tt)+) => {{
        if $expected != $actual {
            bail!("{}:\n Expected: {:?}\n      Got: {:?}", format!($($args)+), $expected, $actual);
        }
    }}
}

/// Struct for matching against an Inspect tree (NodeHierarchy).
pub struct TreeAssertion {
    /// Expected name of the node being compared against
    name: String,
    /// Friendly name that includes path from ancestors. Mainly used to indicate which node fails
    /// in error message
    path: String,
    /// Expected property names and assertions to match the actual properties against
    properties: Vec<(String, Box<dyn PropertyAssertion>)>,
    /// Assertions to match against child trees
    children: Vec<TreeAssertion>,
    /// Whether all properties and children of the tree should be checked
    exact_match: bool,
}

impl TreeAssertion {
    /// Create a new `TreeAssertion`. The |name| argument is the expected name of the tree to be
    /// compared against. Set |exact_match| to true to specify that all properties and children of
    /// the tree should be checked. To perform partial matching of the tree, set it to false.
    pub fn new(name: &str, exact_match: bool) -> Self {
        Self {
            name: name.to_string(),
            path: name.to_string(),
            properties: vec![],
            children: vec![],
            exact_match,
        }
    }

    #[allow(missing_docs)]
    pub fn add_property_assertion(&mut self, key: &str, assertion: Box<dyn PropertyAssertion>) {
        self.properties.push((key.to_string(), assertion));
    }

    #[allow(missing_docs)]
    pub fn add_child_assertion(&mut self, mut assertion: TreeAssertion) {
        assertion.path = format!("{}.{}", self.path, assertion.name);
        self.children.push(assertion);
    }

    /// Check whether |actual| tree satisfies criteria defined by `TreeAssertion`. Return `Ok` if
    /// assertion passes and `Error` if assertion fails.
    pub fn run(&self, actual: &NodeHierarchy) -> Result<(), Error> {
        eq_or_bail!(self.name, actual.name, "node `{}` - expected node name != actual", self.path);

        if self.exact_match {
            let properties_names = self.properties.iter().map(|p| p.0.clone());
            let children_names = self.children.iter().map(|c| c.name.clone());
            let keys: HashSet<String> = properties_names.chain(children_names).collect();

            let actual_props = actual.properties.iter().map(|p| p.name().to_string());
            let actual_children = actual.children.iter().map(|c| c.name.clone());
            let actual_keys: HashSet<String> = actual_props.chain(actual_children).collect();
            eq_or_bail!(keys, actual_keys, "node `{}` - expected keys != actual", self.path);
        }

        for (name, assertion) in self.properties.iter() {
            match actual.properties.iter().find(|p| p.name() == name) {
                Some(property) => {
                    if let Err(e) = assertion.run(property) {
                        bail!(
                            "node `{}` - assertion fails for property `{}`. Reason: {}",
                            self.path,
                            name,
                            e
                        );
                    }
                }
                None => bail!("node `{}` - no property named `{}`", self.path, name),
            }
        }
        for assertion in self.children.iter() {
            match actual.children.iter().find(|c| c.name == assertion.name) {
                Some(child) => assertion.run(&child)?,
                None => bail!("node `{}` - no child named `{}`", self.path, assertion.name),
            }
        }
        Ok(())
    }
}

#[allow(missing_docs)]
pub trait PropertyAssertion {
    /// Check whether |actual| property satisfies criteria. Return `Ok` if assertion passes and
    /// `Error` if assertion fails.
    fn run(&self, actual: &Property) -> Result<(), Error>;
}

macro_rules! impl_property_assertion {
    ($prop_variant:ident, $($ty:ty),+) => {
        $(
            impl PropertyAssertion for $ty {
                fn run(&self, actual: &Property) -> Result<(), Error> {
                    if let Property::$prop_variant(_key, value, ..) = actual {
                        eq_or_bail!(self, value);
                    } else {
                        bail!("expected {}, found {}", stringify!($prop_variant), property_type_name(actual));
                    }
                    Ok(())
                }
            }
        )+
    }
}

macro_rules! impl_array_property_assertion {
    ($prop_variant:ident, $($ty:ty),+) => {
        $(
            impl PropertyAssertion for $ty {
                fn run(&self, actual: &Property) -> Result<(), Error> {
                    if let Property::$prop_variant(_key, value, ..) = actual {
                        eq_or_bail!(self, &value.values);
                    } else {
                        bail!("expected {}, found {}", stringify!($prop_variant), property_type_name(actual));
                    }
                    Ok(())
                }
            }
        )+
    }
}

fn property_type_name(property: &Property) -> &str {
    match property {
        Property::String(_, _) => "String",
        Property::Bytes(_, _) => "Bytes",
        Property::Int(_, _) => "Int",
        Property::IntArray(_, _) => "IntArray",
        Property::Uint(_, _) => "Uint",
        Property::UintArray(_, _) => "UintArray",
        Property::Double(_, _) => "Double",
        Property::DoubleArray(_, _) => "DoubleArray",
    }
}

impl_property_assertion!(String, &str, String);
impl_property_assertion!(Bytes, Vec<u8>);
impl_property_assertion!(Uint, u64);
impl_property_assertion!(Int, i64);
impl_property_assertion!(Double, f64);
impl_array_property_assertion!(DoubleArray, Vec<f64>);
impl_array_property_assertion!(IntArray, Vec<i64>);
impl_array_property_assertion!(UintArray, Vec<u64>);

/// A PropertyAssertion that always passes
pub struct AnyProperty;

impl PropertyAssertion for AnyProperty {
    fn run(&self, _actual: &Property) -> Result<(), Error> {
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::reader::{ArrayFormat, ArrayValue},
    };

    #[test]
    fn test_exact_match_simple() {
        let node_hierarchy = simple_tree();
        assert_inspect_tree!(node_hierarchy, key: {
            sub: "sub_value",
            sub2: "sub2_value",
        });
    }

    #[test]
    fn test_exact_match_complex() {
        let node_hierarchy = complex_tree();
        assert_inspect_tree!(node_hierarchy, key: {
            sub: "sub_value",
            sub2: "sub2_value",
            child1: {
                child1_sub: 10i64,
            },
            child2: {
                child2_sub: 20u64,
            },
        });
    }

    #[test]
    #[should_panic]
    fn test_exact_match_mismatched_property_name() {
        let node_hierarchy = simple_tree();
        assert_inspect_tree!(node_hierarchy, key: {
            sub: "sub_value",
            sub3: "sub2_value",
        });
    }

    #[test]
    #[should_panic]
    fn test_exact_match_mismatched_child_name() {
        let node_hierarchy = complex_tree();
        assert_inspect_tree!(node_hierarchy, key: {
            sub: "sub_value",
            sub2: "sub2_value",
            child1: {
                child1_sub: 10i64,
            },
            child3: {
                child2_sub: 20u64,
            },
        });
    }

    #[test]
    #[should_panic]
    fn test_exact_match_mismatched_property_name_in_child() {
        let node_hierarchy = complex_tree();
        assert_inspect_tree!(node_hierarchy, key: {
            sub: "sub_value",
            sub2: "sub2_value",
            child1: {
                child2_sub: 10i64,
            },
            child2: {
                child2_sub: 20u64,
            },
        });
    }

    #[test]
    #[should_panic]
    fn test_exact_match_mismatched_property_value() {
        let node_hierarchy = simple_tree();
        assert_inspect_tree!(node_hierarchy, key: {
            sub: "sub2_value",
            sub2: "sub2_value",
        });
    }

    #[test]
    #[should_panic]
    fn test_exact_match_missing_property() {
        let node_hierarchy = simple_tree();
        assert_inspect_tree!(node_hierarchy, key: {
            sub: "sub_value",
        });
    }

    #[test]
    #[should_panic]
    fn test_exact_match_missing_child() {
        let node_hierarchy = complex_tree();
        assert_inspect_tree!(node_hierarchy, key: {
            sub: "sub_value",
            sub2: "sub2_value",
            child1: {
                child1_sub: 10i64,
            },
        });
    }

    #[test]
    fn test_partial_match_success() {
        let node_hierarchy = complex_tree();

        // only verify the top tree name
        assert_inspect_tree!(node_hierarchy, key: contains {});

        // verify parts of the tree
        assert_inspect_tree!(node_hierarchy, key: contains {
            sub: "sub_value",
            child1: contains {},
        });
    }

    #[test]
    #[should_panic]
    fn test_partial_match_nonexistent_property() {
        let node_hierarchy = simple_tree();
        assert_inspect_tree!(node_hierarchy, key: contains {
            sub3: AnyProperty,
        });
    }

    #[test]
    fn test_ignore_property_value() {
        let node_hierarchy = simple_tree();
        assert_inspect_tree!(node_hierarchy, key: {
            sub: AnyProperty,
            sub2: "sub2_value",
        });
    }

    #[test]
    #[should_panic]
    fn test_ignore_property_value_property_name_is_still_checked() {
        let node_hierarchy = simple_tree();
        assert_inspect_tree!(node_hierarchy, key: {
            sub1: AnyProperty,
            sub2: "sub2_value",
        })
    }

    #[test]
    fn test_expr_key_syntax() {
        let node_hierarchy = NodeHierarchy::new(
            "key",
            vec![Property::String("@time".to_string(), "1.000".to_string())],
            vec![],
        );
        assert_inspect_tree!(node_hierarchy, key: {
            "@time": "1.000"
        });
    }

    #[test]
    fn test_var_key_syntax() {
        let node_hierarchy = NodeHierarchy::new(
            "key",
            vec![Property::String("@time".to_string(), "1.000".to_string())],
            vec![],
        );
        let time_key = "@time";
        assert_inspect_tree!(node_hierarchy, key: {
            var time_key: "1.000"
        });
    }

    #[test]
    fn test_arrays() {
        let node_hierarchy = NodeHierarchy::new(
            "key",
            vec![
                Property::UintArray(
                    "@uints".to_string(),
                    ArrayValue::new(vec![1, 2, 3], ArrayFormat::Default),
                ),
                Property::IntArray(
                    "@ints".to_string(),
                    ArrayValue::new(vec![-2, -4, 0], ArrayFormat::Default),
                ),
                Property::DoubleArray(
                    "@doubles".to_string(),
                    ArrayValue::new(vec![1.3, 2.5, -3.6], ArrayFormat::Default),
                ),
            ],
            vec![],
        );
        assert_inspect_tree!(node_hierarchy, key: {
            "@uints": vec![1u64, 2, 3],
            "@ints": vec![-2i64, -4, 0],
            "@doubles": vec![1.3, 2.5, -3.6]
        });
    }

    #[test]
    fn test_histograms() {
        let node_hierarchy = NodeHierarchy::new(
            "key",
            vec![
                Property::UintArray(
                    "@linear-uints".to_string(),
                    ArrayValue::new(vec![1, 2, 3, 4, 5], ArrayFormat::LinearHistogram),
                ),
                Property::IntArray(
                    "@linear-ints".to_string(),
                    ArrayValue::new(vec![6, 7, 8, 9], ArrayFormat::LinearHistogram),
                ),
                Property::DoubleArray(
                    "@linear-doubles".to_string(),
                    ArrayValue::new(vec![1.0, 2.0, 4.0, 5.0], ArrayFormat::LinearHistogram),
                ),
                Property::UintArray(
                    "@exp-uints".to_string(),
                    ArrayValue::new(vec![2, 4, 6, 8, 10], ArrayFormat::ExponentialHistogram),
                ),
                Property::IntArray(
                    "@exp-ints".to_string(),
                    ArrayValue::new(vec![1, 3, 5, 7, 9], ArrayFormat::ExponentialHistogram),
                ),
                Property::DoubleArray(
                    "@exp-doubles".to_string(),
                    ArrayValue::new(
                        vec![1.0, 2.0, 3.0, 4.0, 5.0],
                        ArrayFormat::ExponentialHistogram,
                    ),
                ),
            ],
            vec![],
        );
        assert_inspect_tree!(node_hierarchy, key: {
            "@linear-uints": vec![1u64, 2, 3, 4, 5],
            "@linear-ints": vec![6i64, 7, 8, 9],
            "@linear-doubles": vec![1.0, 2.0, 4.0, 5.0],
            "@exp-uints": vec![2u64, 4, 6, 8, 10],
            "@exp-ints": vec![1i64, 3, 5, 7, 9],
            "@exp-doubles": vec![1.0, 2.0, 3.0, 4.0, 5.0]
        });
    }

    #[test]
    fn test_matching_with_inspector() {
        let inspector = Inspector::new();
        assert_inspect_tree!(inspector, root: {});
    }

    fn simple_tree() -> NodeHierarchy {
        NodeHierarchy::new(
            "key",
            vec![
                Property::String("sub".to_string(), "sub_value".to_string()),
                Property::String("sub2".to_string(), "sub2_value".to_string()),
            ],
            vec![],
        )
    }

    fn complex_tree() -> NodeHierarchy {
        NodeHierarchy::new(
            "key",
            vec![
                Property::String("sub".to_string(), "sub_value".to_string()),
                Property::String("sub2".to_string(), "sub2_value".to_string()),
            ],
            vec![
                NodeHierarchy::new(
                    "child1",
                    vec![Property::Int("child1_sub".to_string(), 10i64)],
                    vec![],
                ),
                NodeHierarchy::new(
                    "child2",
                    vec![Property::Uint("child2_sub".to_string(), 20u64)],
                    vec![],
                ),
            ],
        )
    }
}
