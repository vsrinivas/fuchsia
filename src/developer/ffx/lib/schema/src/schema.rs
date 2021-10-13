// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        ast::{ContainerNode, Node, NodeHolder},
        de::Deserializer,
        errors::{Error, Result},
    },
    anyhow::anyhow,
    serde::Deserialize,
    std::collections::{BTreeMap, BTreeSet},
};

/// A map from type names to an AST node that represents that type.
pub type Schema = BTreeMap<String, ContainerNode>;

/// An object which can be used to generate a schema by observing types.
/// Expected use of this object looks like:
///
///     let mut gen = Generator::new(Default::default());
///     gen.resolve_type::<MyType>()?;
///     let schema = gen.schema()?;
///
/// The default configuration attempts to resolve all enum variants even within nested types.
/// It is possible to shortcut this by calling `resolve_type` directly on other dependent types
/// before calling it on your top level type:
///
///     let config = GeneratorConfig::default();
///     let mut gen = Generator::new(config);
///     gen.resolve_type::<OtherNestedEnum>()?;
///     gen.resolve_type::<SomeEnum>()?;
///     gen.resolve_type::<MyType>()?;
///     let schema = gen.schema()?;
#[derive(Debug)]
pub struct Generator {
    pub(crate) schema: Schema,
    pub(crate) incomplete_enums: BTreeSet<String>,
    pub(crate) index_state: BTreeMap<String, u32>,
    depth_limit: usize,
}

#[derive(Debug)]
pub struct GeneratorConfig {
    pub depth_limit: usize,
}

impl Default for GeneratorConfig {
    fn default() -> Self {
        Self { depth_limit: 500 }
    }
}

impl GeneratorConfig {
    /// Configure the depth of paths to explore before giving up when trying to resolve nested
    /// parts of a type. Setting this to 0 is equivalent to no limit.
    pub fn new(depth_limit: usize) -> Self {
        Self { depth_limit }
    }
}

impl Generator {
    /// Create a new generator with the given configuration. All methods called on this instance
    /// will modify a shared representation of types. Therefore different calls to resolve_type and
    /// resolve_type_once can impact one another.
    pub fn new(config: GeneratorConfig) -> Self {
        Self {
            schema: BTreeMap::new(),
            incomplete_enums: BTreeSet::new(),
            index_state: BTreeMap::new(),
            depth_limit: config.depth_limit,
        }
    }

    /// Attempt to resolve T to a Node ignoring potentially incomplete enums.
    pub fn resolve_type_once<'de, T>(&mut self) -> Result<Node>
    where
        T: Deserialize<'de>,
    {
        let mut node = Node::unknown();
        let deserializer = Deserializer::new(self, &mut node);
        T::deserialize(deserializer)?;
        node.reduce();
        Ok(node)
    }

    /// Attempt to resolve T to Node by repeatedly calling resolve_type_once until all types have
    /// been resolved or the configured depth_limit has been reached.
    pub fn resolve_type<'de, T>(&mut self) -> Result<Node>
    where
        T: Deserialize<'de>,
    {
        let mut depth = 0;
        loop {
            if self.depth_limit > 0 && depth > self.depth_limit {
                return Err(Error::OverDepthLimit(self.depth_limit));
            }

            let node = self.resolve_type_once::<T>()?;
            if let Node::TypeName(name) = &node {
                if self.incomplete_enums.contains(name) {
                    self.incomplete_enums.remove(name);
                    continue;
                }
                if !self.incomplete_enums.is_empty() {
                    depth += 1;
                    continue;
                }
            }
            return Ok(node);
        }
    }

    /// Consume this generator to create a Schema.
    /// This will return an error if there are enums that have not been completely explored.
    pub fn schema(self) -> Result<Schema> {
        let mut schema = self.schema;
        for (name, node) in schema.iter_mut() {
            node.normalize().map_err(|_| {
                Error::Error(anyhow!("Unknown node in container: {}", name.clone()))
            })?;
        }
        if self.incomplete_enums.is_empty() {
            Ok(schema)
        } else {
            Err(Error::Error(anyhow!(
                "Missing variants: {:?}",
                self.incomplete_enums.into_iter().collect::<Vec<_>>()
            )))
        }
    }
}
