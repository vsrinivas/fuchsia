// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Module defining the Abstract Syntax Tree (AST) used by Serde.
//!
//! The following nodes are possible:
//! * `ContainerNode`: an abstract data type (struct or enum),
//! * `Node`: an unnamed value,
//! * `Named<Node>`: a node with a name,
//! * `VariantNode`: a variant in a enum,
//! * `Named<VariantNode>`: a variant in a enum, together with its name,
//! * `Variable<Node>`: a variable holding an initially unknown value node,
//! * `Variable<VariantNode>`: a variable holding an initially unknown variant node.

use {
    crate::errors::{Error, Result},
    anyhow::anyhow,
    serde::{Deserialize, Serialize},
    std::cell::{Ref, RefCell, RefMut},
    std::collections::{btree_map::Entry, BTreeMap},
    std::ops::DerefMut,
    std::rc::Rc,
};

/// The AST nodes that are possible for Rust types.
#[derive(Clone, Debug, Eq, PartialEq, Deserialize, Serialize)]
pub enum Node {
    /// A variable is used to track things we don't know yet to support type unification.
    Variable(#[serde(with = "not_implemented")] Variable<Node>),

    /// This is used to keep track of the names of types: structs, enums, etc.
    TypeName(String),

    Unit,
    Bool,
    I8,
    I16,
    I32,
    I64,
    I128,
    U8,
    U16,
    U32,
    U64,
    U128,
    F32,
    F64,
    Char,
    Str,
    Bytes,

    /// This represents an Option<T>.
    Option(Box<Node>),
    /// This represents things like Vec<T>
    Seq(Box<Node>),
    /// This represents things like HashMap<K, V>.
    Map {
        key: Box<Node>,
        value: Box<Node>,
    },
    /// This represents things like (i32, String).
    Tuple(Vec<Node>),
    /// This represents a value like (u16, u16, u16) as content: U16, size: 3.
    TupleArray {
        content: Box<Node>,
        size: usize,
    },
}

impl Node {
    pub fn unknown() -> Self {
        Self::Variable(Variable::new(None))
    }
}

impl Default for Node {
    fn default() -> Self {
        Self::unknown()
    }
}

/// Nodes that represent the algebraic data types.
#[derive(Clone, Debug, Eq, PartialEq, Deserialize, Serialize)]
pub enum ContainerNode {
    /// An empty struct, e.g. `struct A`.
    UnitStruct,
    /// A struct with a single unnamed parameter, e.g. `struct A(i32)`
    NewTypeStruct(Box<Node>),
    /// A struct with several unnamed parameters, e.g. `struct A(i32, String)`
    TupleStruct(Vec<Node>),
    /// A struct with named parameters, e.g. `struct A { a: i32 }`.
    Struct(Vec<Named<Node>>),
    /// An enum.
    /// Each variant has a unique index and name within the enum.
    /// The map is keyed on this unique index and the value has the name wrapped around the
    /// different types of nodes that are possible to be enum variants.
    Enum(BTreeMap<u32, Named<VariantNode>>),
}

/// A named value.
/// Used for named parameters or variants.
#[derive(Clone, Debug, Eq, PartialEq, Deserialize, Serialize)]
pub struct Named<T> {
    pub name: String,
    pub value: T,
}

/// A generic represention of a possibly unknown thing.
#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct Variable<T>(Rc<RefCell<Option<T>>>);

/// Possible types that can be variants of an enum.
/// These analogous to the similarly named ContainerNode variants.
#[derive(Clone, Debug, Eq, PartialEq, Deserialize, Serialize)]
pub enum VariantNode {
    Variable(#[serde(with = "not_implemented")] Variable<VariantNode>),
    Unit,
    NewType(Box<Node>),
    Tuple(Vec<Node>),
    Struct(Vec<Named<Node>>),
}

impl VariantNode {
    pub fn unknown() -> Self {
        Self::Variable(Variable::new(None))
    }
}

impl Default for VariantNode {
    fn default() -> Self {
        Self::unknown()
    }
}

impl<T> Variable<T> {
    pub(crate) fn new(content: Option<T>) -> Self {
        Self(Rc::new(RefCell::new(content)))
    }

    pub fn borrow(&self) -> Ref<'_, Option<T>> {
        self.0.as_ref().borrow()
    }

    pub fn borrow_mut(&self) -> RefMut<'_, Option<T>> {
        self.0.as_ref().borrow_mut()
    }
}

impl<T> Variable<T>
where
    T: Clone,
{
    fn into_inner(self) -> Option<T> {
        match Rc::try_unwrap(self.0) {
            Ok(cell) => cell.into_inner(),
            Err(rc) => rc.borrow().clone(),
        }
    }
}

mod not_implemented {
    pub fn serialize<T, S>(_: &T, _serializer: S) -> std::result::Result<S::Ok, S::Error>
    where
        S: serde::ser::Serializer,
    {
        use serde::ser::Error;
        Err(S::Error::custom("Cannot serialize variables"))
    }

    pub fn deserialize<'de, T, D>(_deserializer: D) -> std::result::Result<T, D::Error>
    where
        D: serde::de::Deserializer<'de>,
    {
        use serde::de::Error;
        Err(D::Error::custom("Cannot deserialize variables"))
    }
}

/// The shared set of functionality across different nodes in the AST.
pub trait NodeHolder {
    /// Depth first visit of the graph.
    /// Replace variables with known values before calling f.
    fn visit_mut(&mut self, f: &mut dyn FnMut(&mut Node) -> Result<()>) -> Result<()>;

    /// Term unification that updates variables and enum variants within self and other to match.
    fn unify(&mut self, other: Self) -> Result<()>;

    fn is_unknown(&self) -> bool;

    /// Remove variable wrappers and compress tuples into TupleArrays where possible. This will
    /// fail if any variables are still unknown.
    fn normalize(&mut self) -> Result<()> {
        self.visit_mut(&mut |node: &mut Node| {
            let normalized = match node {
                Node::Tuple(nodes) => {
                    let size = nodes.len();
                    if size <= 1 {
                        return Ok(());
                    }
                    let node0 = &nodes[0];
                    for node in nodes.iter().skip(1) {
                        if node != node0 {
                            return Ok(());
                        }
                    }
                    Node::TupleArray { content: Box::new(std::mem::take(&mut nodes[0])), size }
                }
                _ => {
                    return Ok(());
                }
            };
            *node = normalized;
            Ok(())
        })
    }

    /// Removes as many variable wrappers as possible. It is okay to call this if some variables
    /// are unknown.
    fn reduce(&mut self) {
        self.visit_mut(&mut |_| Ok(())).unwrap_or(())
    }
}

fn unification_error(node1: impl std::fmt::Debug, node2: impl std::fmt::Debug) -> Error {
    Error::Error(anyhow!("Failed to unify: {:?} with {:?}", node1, node2))
}

impl NodeHolder for VariantNode {
    fn visit_mut(&mut self, f: &mut dyn FnMut(&mut Node) -> Result<()>) -> Result<()> {
        match self {
            Self::Variable(variable) => {
                variable.visit_mut(f)?;
                *self = std::mem::take(variable).into_inner().expect("variable to be known");
            }
            Self::Unit => (),
            Self::NewType(node) => node.visit_mut(f)?,
            Self::Tuple(nodes) => {
                for node in nodes {
                    node.visit_mut(f)?;
                }
            }
            Self::Struct(named_nodes) => {
                for node in named_nodes {
                    node.visit_mut(f)?;
                }
            }
        }
        Ok(())
    }

    fn unify(&mut self, other: VariantNode) -> Result<()> {
        match (self, other) {
            (node1, Self::Variable(variable2)) => {
                if let Some(node2) = variable2.borrow_mut().deref_mut() {
                    node1.unify(std::mem::take(node2))?;
                }
                *variable2.borrow_mut() = Some(node1.clone());
            }
            (Self::Variable(variable1), node2) => {
                let var = match variable1.borrow_mut().deref_mut() {
                    value1 @ None => {
                        value1.replace(node2);
                        // value1 has type Option<Variable<VariantNode>> but we need to return an
                        // Option<VariantNode> this None below as actually different than the None
                        // above in value1
                        None
                    }
                    Some(node1) => {
                        node1.unify(node2)?;
                        match node1 {
                            Self::Variable(variable) => Some(variable.clone()),
                            _ => None,
                        }
                    }
                };
                if let Some(variable) = var {
                    *variable1 = variable;
                }
            }
            (Self::Unit, Self::Unit) => (),
            (Self::NewType(node1), Self::NewType(node2)) => {
                node1.as_mut().unify(*node2)?;
            }
            (Self::Tuple(nodes1), Self::Tuple(nodes2)) if nodes1.len() == nodes2.len() => {
                for (node1, node2) in nodes1.iter_mut().zip(nodes2.into_iter()) {
                    node1.unify(node2)?;
                }
            }
            (Self::Struct(named_nodes1), Self::Struct(named_nodes2))
                if named_nodes1.len() == named_nodes2.len() =>
            {
                for (node1, node2) in named_nodes1.iter_mut().zip(named_nodes2.into_iter()) {
                    node1.unify(node2)?;
                }
            }
            (node1, node2) => {
                return Err(unification_error(node1, node2));
            }
        }
        Ok(())
    }

    fn is_unknown(&self) -> bool {
        match self {
            Self::Variable(v) => v.is_unknown(),
            _ => false,
        }
    }
}

impl<T> NodeHolder for Named<T>
where
    T: NodeHolder + std::fmt::Debug,
{
    fn visit_mut(&mut self, f: &mut dyn FnMut(&mut Node) -> Result<()>) -> Result<()> {
        self.value.visit_mut(f)
    }

    fn unify(&mut self, other: Named<T>) -> Result<()> {
        if self.name != other.name {
            return Err(unification_error(self, other));
        }
        self.value.unify(other.value)
    }

    fn is_unknown(&self) -> bool {
        false
    }
}

impl<T> NodeHolder for Variable<T>
where
    T: NodeHolder + std::fmt::Debug + Clone,
{
    fn visit_mut(&mut self, f: &mut dyn FnMut(&mut Node) -> Result<()>) -> Result<()> {
        match self.borrow_mut().deref_mut() {
            None => Err(Error::Error(anyhow!("Unknown Node"))),
            Some(value) => value.visit_mut(f),
        }
    }

    fn unify(&mut self, _other: Self) -> Result<()> {
        Err(Error::Error(anyhow!("Cannot unify variables directly")))
    }

    fn is_unknown(&self) -> bool {
        match self.borrow().as_ref() {
            None => true,
            Some(node) => node.is_unknown(),
        }
    }
}

impl NodeHolder for ContainerNode {
    fn visit_mut(&mut self, f: &mut dyn FnMut(&mut Node) -> Result<()>) -> Result<()> {
        match self {
            Self::UnitStruct => (),
            Self::NewTypeStruct(node) => node.visit_mut(f)?,
            Self::TupleStruct(nodes) => {
                for node in nodes {
                    node.visit_mut(f)?;
                }
            }
            Self::Struct(named_nodes) => {
                for node in named_nodes {
                    node.visit_mut(f)?;
                }
            }
            Self::Enum(variants) => {
                for variant in variants {
                    variant.1.visit_mut(f)?;
                }
            }
        }
        Ok(())
    }

    fn unify(&mut self, node: ContainerNode) -> Result<()> {
        match (self, node) {
            (Self::UnitStruct, Self::UnitStruct) => (),

            (Self::NewTypeStruct(node1), Self::NewTypeStruct(node2)) => {
                node1.as_mut().unify(*node2)?;
            }

            (Self::TupleStruct(nodes1), Self::TupleStruct(nodes2))
                if nodes1.len() == nodes2.len() =>
            {
                for (node1, node2) in nodes1.iter_mut().zip(nodes2.into_iter()) {
                    node1.unify(node2)?;
                }
            }

            (Self::Struct(named_nodes1), Self::Struct(named_nodes2))
                if named_nodes1.len() == named_nodes2.len() =>
            {
                for (node1, node2) in named_nodes1.iter_mut().zip(named_nodes2.into_iter()) {
                    node1.unify(node2)?;
                }
            }

            (Self::Enum(variants1), Self::Enum(variants2)) => {
                for (index2, variant2) in variants2.into_iter() {
                    match variants1.entry(index2) {
                        Entry::Vacant(e) => {
                            e.insert(variant2);
                        }
                        Entry::Occupied(mut e) => {
                            e.get_mut().unify(variant2)?;
                        }
                    }
                }
            }

            (node1, node2) => {
                return Err(unification_error(node1, node2));
            }
        }
        Ok(())
    }

    fn is_unknown(&self) -> bool {
        false
    }
}

impl NodeHolder for Node {
    fn visit_mut(&mut self, f: &mut dyn FnMut(&mut Node) -> Result<()>) -> Result<()> {
        match self {
            Self::Variable(variable) => {
                variable.visit_mut(f)?;
                *self = std::mem::take(variable).into_inner().expect("variable is known");
            }
            Self::TypeName(_)
            | Self::Unit
            | Self::Bool
            | Self::I8
            | Self::I16
            | Self::I32
            | Self::I64
            | Self::I128
            | Self::U8
            | Self::U16
            | Self::U32
            | Self::U64
            | Self::U128
            | Self::F32
            | Self::F64
            | Self::Char
            | Self::Str
            | Self::Bytes => (),

            Self::Option(node) | Self::Seq(node) | Self::TupleArray { content: node, .. } => {
                node.visit_mut(f)?;
            }

            Self::Map { key, value } => {
                key.visit_mut(f)?;
                value.visit_mut(f)?;
            }

            Self::Tuple(nodes) => {
                for node in nodes {
                    node.visit_mut(f)?;
                }
            }
        }
        f(self)
    }

    fn unify(&mut self, node: Node) -> Result<()> {
        match (self, node) {
            (node1, Self::Variable(variable2)) => {
                if let Some(node2) = variable2.borrow_mut().deref_mut() {
                    node1.unify(std::mem::take(node2))?;
                }
                *variable2.borrow_mut() = Some(node1.clone());
            }
            (Self::Variable(variable1), node2) => {
                let var = match variable1.borrow_mut().deref_mut() {
                    value1 @ None => {
                        *value1 = Some(node2);
                        None
                    }
                    Some(node1) => {
                        node1.unify(node2)?;
                        match node1 {
                            Self::Variable(variable) => Some(variable.clone()),
                            _ => None,
                        }
                    }
                };
                if let Some(variable) = var {
                    *variable1 = variable;
                }
            }

            (Self::Unit, Self::Unit)
            | (Self::Bool, Self::Bool)
            | (Self::I8, Self::I8)
            | (Self::I16, Self::I16)
            | (Self::I32, Self::I32)
            | (Self::I64, Self::I64)
            | (Self::I128, Self::I128)
            | (Self::U8, Self::U8)
            | (Self::U16, Self::U16)
            | (Self::U32, Self::U32)
            | (Self::U64, Self::U64)
            | (Self::U128, Self::U128)
            | (Self::F32, Self::F32)
            | (Self::F64, Self::F64)
            | (Self::Char, Self::Char)
            | (Self::Str, Self::Str)
            | (Self::Bytes, Self::Bytes) => (),

            (Self::TypeName(name1), Self::TypeName(name2)) if *name1 == name2 => (),

            (Self::Option(node1), Self::Option(node2)) | (Self::Seq(node1), Self::Seq(node2)) => {
                node1.as_mut().unify(*node2)?;
            }

            (Self::Tuple(nodes1), Self::Tuple(nodes2)) if nodes1.len() == nodes2.len() => {
                for (node1, node2) in nodes1.iter_mut().zip(nodes2.into_iter()) {
                    node1.unify(node2)?;
                }
            }

            (Self::Map { key: key1, value: value1 }, Self::Map { key: key2, value: value2 }) => {
                key1.as_mut().unify(*key2)?;
                value1.as_mut().unify(*value2)?;
            }

            (node1, node2) => {
                return Err(unification_error(node1, node2));
            }
        }
        Ok(())
    }

    fn is_unknown(&self) -> bool {
        if let Self::Variable(v) = self {
            return v.is_unknown();
        }
        false
    }
}

pub(crate) trait ContainerNodeEntry {
    fn unify(self, node: ContainerNode) -> Result<()>;
}

impl<'a, K> ContainerNodeEntry for Entry<'a, K, ContainerNode>
where
    K: std::cmp::Ord,
{
    fn unify(self, node: ContainerNode) -> Result<()> {
        match self {
            Entry::Vacant(e) => {
                e.insert(node);
                Ok(())
            }
            Entry::Occupied(e) => e.into_mut().unify(node),
        }
    }
}
