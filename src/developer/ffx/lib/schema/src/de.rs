// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        ast::{ContainerNode, ContainerNodeEntry, Named, Node, NodeHolder, VariantNode},
        errors::{Error, Result},
        schema::Generator,
    },
    anyhow::anyhow,
    serde::de::{DeserializeSeed, IntoDeserializer, Visitor},
    std::collections::BTreeMap,
};

/// A serde Deserializer that uses the deserialize mechanism to resolve type information into the
/// schema inside the generator. As a by product, the node will resolve to a value of the desired
/// type.
pub(crate) struct Deserializer<'a> {
    generator: &'a mut Generator,
    node: &'a mut Node,
}

impl<'a> Deserializer<'a> {
    pub fn new(generator: &'a mut Generator, node: &'a mut Node) -> Self {
        Self { generator, node }
    }
}

macro_rules! declare_deserialize {
    ($method:ident) => {
        fn $method<V>(self, _visitor: V) -> Result<V::Value>
        where
            V: Visitor<'de>,
        {
            Err(Error::Error(anyhow!("not supported: {}", stringify!($method))))
        }
    };

    ($method:ident, $node:ident, $visit:ident) => {
        fn $method<V>(self, visitor: V) -> Result<V::Value>
        where
            V: Visitor<'de>,
        {
            self.node.unify(Node::$node)?;
            visitor.$visit()
        }
    };

    ($method:ident, $node:ident, $visit:ident, $val:expr) => {
        fn $method<V>(self, visitor: V) -> Result<V::Value>
        where
            V: Visitor<'de>,
        {
            self.node.unify(Node::$node)?;
            visitor.$visit($val)
        }
    };
}

impl<'de, 'a> serde::de::Deserializer<'de> for Deserializer<'a> {
    type Error = Error;

    // unsupported
    declare_deserialize!(deserialize_any);
    declare_deserialize!(deserialize_identifier);
    declare_deserialize!(deserialize_ignored_any);

    // values
    declare_deserialize!(deserialize_bool, Bool, visit_bool, false);
    declare_deserialize!(deserialize_i8, I8, visit_i8, 0);
    declare_deserialize!(deserialize_i16, I16, visit_i16, 0);
    declare_deserialize!(deserialize_i32, I32, visit_i32, 0);
    declare_deserialize!(deserialize_i64, I64, visit_i64, 0);
    declare_deserialize!(deserialize_i128, I128, visit_i128, 0);
    declare_deserialize!(deserialize_u8, U8, visit_u8, 0);
    declare_deserialize!(deserialize_u16, U16, visit_u16, 0);
    declare_deserialize!(deserialize_u32, U32, visit_u32, 0);
    declare_deserialize!(deserialize_u64, U64, visit_u64, 0);
    declare_deserialize!(deserialize_u128, U128, visit_u128, 0);
    declare_deserialize!(deserialize_f32, F32, visit_f32, 0.0);
    declare_deserialize!(deserialize_f64, F64, visit_f64, 0.0);
    declare_deserialize!(deserialize_char, Char, visit_char, 'A');
    declare_deserialize!(deserialize_str, Str, visit_borrowed_str, "");
    declare_deserialize!(deserialize_string, Str, visit_string, String::new());
    declare_deserialize!(deserialize_bytes, Bytes, visit_borrowed_bytes, b"");
    declare_deserialize!(deserialize_byte_buf, Bytes, visit_byte_buf, Vec::new());

    // no value
    declare_deserialize!(deserialize_unit, Unit, visit_unit);

    fn deserialize_option<V>(self, visitor: V) -> Result<V::Value>
    where
        V: Visitor<'de>,
    {
        let mut node = Node::unknown();
        self.node.unify(Node::Option(Box::new(node.clone())))?;
        let inner = Deserializer::new(self.generator, &mut node);
        visitor.visit_some(inner)
    }

    fn deserialize_unit_struct<V>(self, name: &'static str, visitor: V) -> Result<V::Value>
    where
        V: Visitor<'de>,
    {
        self.node.unify(Node::TypeName(name.into()))?;
        self.generator.schema.entry(name.to_string()).unify(ContainerNode::UnitStruct)?;
        visitor.visit_unit()
    }

    fn deserialize_newtype_struct<V>(self, name: &'static str, visitor: V) -> Result<V::Value>
    where
        V: Visitor<'de>,
    {
        self.node.unify(Node::TypeName(name.into()))?;
        let mut node = Node::unknown();
        self.generator
            .schema
            .entry(name.to_string())
            .unify(ContainerNode::NewTypeStruct(Box::new(node.clone())))?;
        let inner = Deserializer::new(self.generator, &mut node);
        visitor.visit_newtype_struct(inner)
    }

    fn deserialize_seq<V>(self, visitor: V) -> Result<V::Value>
    where
        V: Visitor<'de>,
    {
        let mut node = Node::unknown();
        self.node.unify(Node::Seq(Box::new(node.clone())))?;
        let inner = SeqDeserializer::new(self.generator, std::iter::once(&mut node));
        visitor.visit_seq(inner)
    }

    fn deserialize_tuple<V>(self, len: usize, visitor: V) -> Result<V::Value>
    where
        V: Visitor<'de>,
    {
        let mut nodes: Vec<_> = std::iter::repeat_with(Node::unknown).take(len).collect();
        self.node.unify(Node::Tuple(nodes.clone()))?;
        let inner = SeqDeserializer::new(self.generator, nodes.iter_mut());
        visitor.visit_seq(inner)
    }

    fn deserialize_tuple_struct<V>(
        self,
        name: &'static str,
        len: usize,
        visitor: V,
    ) -> Result<V::Value>
    where
        V: Visitor<'de>,
    {
        self.node.unify(Node::TypeName(name.into()))?;
        let mut nodes: Vec<_> = std::iter::repeat_with(Node::unknown).take(len).collect();
        self.generator
            .schema
            .entry(name.to_string())
            .unify(ContainerNode::TupleStruct(nodes.clone()))?;
        let inner = SeqDeserializer::new(self.generator, nodes.iter_mut());
        visitor.visit_seq(inner)
    }

    fn deserialize_map<V>(self, visitor: V) -> Result<V::Value>
    where
        V: Visitor<'de>,
    {
        let mut key_node = Node::unknown();
        let mut value_node = Node::unknown();
        self.node.unify(Node::Map {
            key: Box::new(key_node.clone()),
            value: Box::new(value_node.clone()),
        })?;
        let inner =
            SeqDeserializer::new(self.generator, vec![&mut key_node, &mut value_node].into_iter());
        visitor.visit_map(inner)
    }

    fn deserialize_struct<V>(
        self,
        name: &'static str,
        fields: &'static [&'static str],
        visitor: V,
    ) -> Result<V::Value>
    where
        V: Visitor<'de>,
    {
        self.node.unify(Node::TypeName(name.into()))?;
        let mut nodes: Vec<_> = fields
            .iter()
            .map(|&name| Named { name: name.into(), value: Node::unknown() })
            .collect();
        self.generator
            .schema
            .entry(name.to_string())
            .unify(ContainerNode::Struct(nodes.clone()))?;
        let inner =
            SeqDeserializer::new(self.generator, nodes.iter_mut().map(|named| &mut named.value));
        visitor.visit_seq(inner)
    }

    fn deserialize_enum<V>(
        self,
        name: &'static str,
        variants: &'static [&'static str],
        visitor: V,
    ) -> Result<V::Value>
    where
        V: Visitor<'de>,
    {
        self.node.unify(Node::TypeName(name.into()))?;
        self.generator
            .schema
            .entry(name.to_string())
            .unify(ContainerNode::Enum(BTreeMap::new()))?;

        let known_variants = match self.generator.schema.get_mut(name) {
            Some(ContainerNode::Enum(x)) => x,
            _ => unreachable!(),
        };

        if known_variants.len() == variants.len() && self.generator.incomplete_enums.contains(name)
        {
            self.generator.incomplete_enums.remove(name);
        }

        let index = if known_variants.len() == variants.len() {
            // This code exists so that in we cycle through the variant that
            // is returned so that we can make progress exploring branches of the type tree to find
            // more enum variants for nested types.
            //
            // If we didn't want to do that we could just return 0.
            //
            // A more complicated approach would be to try to track which index leads to nested
            // incomplete enums but doing that is much harder than this more brute force approach.
            // And this works pretty well in practice.
            *self
                .generator
                .index_state
                .entry(name.to_string())
                .and_modify(|e| {
                    *e = (*e + 1) % (known_variants.len() as u32);
                })
                .or_insert(0)
        } else {
            let mut index = known_variants.len() as u32;
            while known_variants.contains_key(&index) {
                index -= 1;
            }
            index
        };
        let variant = known_variants.entry(index).or_insert_with(|| Named {
            name: (*variants.get(index as usize).expect("an index in 0..variants.len()"))
                .to_string(),
            value: VariantNode::unknown(),
        });
        let mut value = variant.value.clone();
        if known_variants.len() != variants.len() {
            self.generator.incomplete_enums.insert(name.into());
        }
        let inner = EnumDeserializer::new(self.generator, index, &mut value);
        visitor.visit_enum(inner)
    }

    fn is_human_readable(&self) -> bool {
        false
    }
}

struct SeqDeserializer<'a, I> {
    generator: &'a mut Generator,
    nodes: I,
}

impl<'a, I> SeqDeserializer<'a, I> {
    fn new(generator: &'a mut Generator, nodes: I) -> Self {
        Self { generator, nodes }
    }
}

impl<'de, 'a, I> serde::de::SeqAccess<'de> for SeqDeserializer<'a, I>
where
    I: Iterator<Item = &'a mut Node>,
{
    type Error = Error;

    fn next_element_seed<T>(&mut self, seed: T) -> Result<Option<T::Value>>
    where
        T: DeserializeSeed<'de>,
    {
        let node = match self.nodes.next() {
            Some(x) => x,
            None => return Ok(None),
        };
        let inner = Deserializer::new(self.generator, node);
        seed.deserialize(inner).map(Some)
    }

    fn size_hint(&self) -> Option<usize> {
        self.nodes.size_hint().1
    }
}

impl<'de, 'a, I> serde::de::MapAccess<'de> for SeqDeserializer<'a, I>
where
    I: Iterator<Item = &'a mut Node>,
{
    type Error = Error;

    fn next_key_seed<K>(&mut self, seed: K) -> Result<Option<K::Value>>
    where
        K: DeserializeSeed<'de>,
    {
        let node = match self.nodes.next() {
            Some(x) => x,
            None => return Ok(None),
        };
        let inner = Deserializer::new(self.generator, node);
        seed.deserialize(inner).map(Some)
    }

    fn next_value_seed<V>(&mut self, seed: V) -> Result<V::Value>
    where
        V: DeserializeSeed<'de>,
    {
        let node = match self.nodes.next() {
            Some(x) => x,
            None => unreachable!(),
        };
        let inner = Deserializer::new(self.generator, node);
        seed.deserialize(inner)
    }

    fn size_hint(&self) -> Option<usize> {
        self.nodes.size_hint().1.map(|x| x / 2)
    }
}

struct EnumDeserializer<'a> {
    generator: &'a mut Generator,
    index: u32,
    node: &'a mut VariantNode,
}

impl<'a> EnumDeserializer<'a> {
    fn new(generator: &'a mut Generator, index: u32, node: &'a mut VariantNode) -> Self {
        Self { generator, index, node }
    }
}

impl<'de, 'a> serde::de::EnumAccess<'de> for EnumDeserializer<'a> {
    type Error = Error;
    type Variant = Self;

    fn variant_seed<V>(self, seed: V) -> Result<(V::Value, Self::Variant)>
    where
        V: DeserializeSeed<'de>,
    {
        // All these type annotations were necessary to get around some type inference issues. I am
        // not entirely sure why.
        let index = self.index;
        let value: std::result::Result<V::Value, Error> =
            seed.deserialize(index.into_deserializer());
        let value = value?;
        Ok((value, self))
    }
}

impl<'de, 'a> serde::de::VariantAccess<'de> for EnumDeserializer<'a> {
    type Error = Error;

    fn unit_variant(self) -> Result<()> {
        self.node.unify(VariantNode::Unit)
    }

    fn newtype_variant_seed<T>(self, seed: T) -> Result<T::Value>
    where
        T: DeserializeSeed<'de>,
    {
        let mut node = Node::unknown();
        self.node.unify(VariantNode::NewType(Box::new(node.clone())))?;
        let inner = Deserializer::new(self.generator, &mut node);
        seed.deserialize(inner)
    }

    fn tuple_variant<V>(self, len: usize, visitor: V) -> Result<V::Value>
    where
        V: Visitor<'de>,
    {
        let mut nodes: Vec<_> = std::iter::repeat_with(Node::unknown).take(len).collect();
        self.node.unify(VariantNode::Tuple(nodes.clone()))?;
        let inner = SeqDeserializer::new(self.generator, nodes.iter_mut());
        visitor.visit_seq(inner)
    }

    fn struct_variant<V>(self, fields: &'static [&'static str], visitor: V) -> Result<V::Value>
    where
        V: Visitor<'de>,
    {
        let mut nodes: Vec<_> = fields
            .iter()
            .map(|&name| Named { name: name.into(), value: Node::unknown() })
            .collect();
        self.node.unify(VariantNode::Struct(nodes.clone()))?;

        let inner =
            SeqDeserializer::new(self.generator, nodes.iter_mut().map(|named| &mut named.value));
        visitor.visit_seq(inner)
    }
}
