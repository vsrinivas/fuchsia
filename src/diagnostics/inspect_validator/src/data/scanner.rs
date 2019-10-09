// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{validate::ROOT_ID, Data, Node, Payload, Property, ROOT_NAME},
    failure::{bail, format_err, Error},
    fuchsia_inspect::{
        self,
        format::{
            block::{ArrayFormat, Block, PropertyFormat},
            block_type::BlockType,
        },
        reader as ireader,
    },
    std::{
        self,
        cmp::min,
        collections::{HashMap, HashSet},
    },
};

// When reading from a VMO, the keys of the HashMaps are the indexes of the relevant
// blocks. Thus, they will never collide.
//
// Reading from a VMO is a complicated process.
// 1) Try to take a fuchsia_inspect::reader::snapshot::Snapshot of the VMO.
// 2) Iterate through it, pedantically examining all its blocks and loading
//   the relevant blocks into a ScannedObjects structure (which contains
//   ScannedNode, ScannedName, ScannedProperty, and ScannedExtent).
// 2.5) ScannedNodes may be added before they're scanned, since they need to
//   track their child nodes and properties. In this case, their "validated"
//   field will be false until they're actually scanned.
// 3) Starting from the "0" node, create Node and Property objects for all the
//   dependent children and properties (verifying that all dependent objects
//   exist (and are valid in the case of Nodes)). This is also when Extents are
//   combined into byte vectors, and in the case of String, verified to be valid UTF-8.
// 4) Add the Node and Property objects (each with its ID) into the "nodes" and
//   "properties" HashMaps of a new Data object. Note that these HashMaps do not
//   hold the hierarchical information; instead, each Node contains a HashSet of
//   the keys of its children and properties.

#[derive(Debug)]
pub struct Scanner {
    nodes: HashMap<u32, ScannedNode>,
    names: HashMap<u32, ScannedName>,
    properties: HashMap<u32, ScannedProperty>,
    extents: HashMap<u32, ScannedExtent>,
}

impl Scanner {
    pub fn scan(snapshot: ireader::snapshot::Snapshot) -> Result<Data, Error> {
        let mut objects = Scanner::new();
        for block in snapshot.scan() {
            match block.block_type_or() {
                Ok(BlockType::Free) => objects.process_free(block)?,
                Ok(BlockType::Reserved) => objects.process_reserved(block)?,
                Ok(BlockType::Header) => objects.process_header(block)?,
                Ok(BlockType::NodeValue) => objects.process_node(block)?,
                Ok(BlockType::IntValue)
                | Ok(BlockType::UintValue)
                | Ok(BlockType::DoubleValue)
                | Ok(BlockType::ArrayValue)
                | Ok(BlockType::PropertyValue) => objects.process_property(block)?,
                Ok(BlockType::Extent) => objects.process_extent(block)?,
                Ok(BlockType::Name) => objects.process_name(block)?,
                Ok(BlockType::Tombstone) => objects.process_tombstone(block)?,
                Ok(BlockType::LinkValue) => bail!("LinkValue isn't supported yet."),
                Err(error) => return Err(error),
            }
        }
        let (mut new_nodes, mut new_properties) = objects.make_valid_node_tree(ROOT_ID)?;
        let mut nodes = HashMap::new();
        for (node, id) in new_nodes.drain(..) {
            nodes.insert(id, node);
        }
        let mut properties = HashMap::new();
        for (property, id) in new_properties.drain(..) {
            properties.insert(id, property);
        }
        Ok(Data::build(nodes, properties))
    }

    // ***** Utility functions

    fn new() -> Scanner {
        let mut objects = Scanner {
            nodes: HashMap::new(),
            names: HashMap::new(),
            properties: HashMap::new(),
            extents: HashMap::new(),
        };
        // The ScannedNode at 0 is the "root" node. It exists to receive pointers to objects
        // whose parent is 0 while scanning the VMO.
        objects.nodes.insert(
            0,
            ScannedNode {
                validated: true,
                parent: 0,
                name: 0,
                children: HashSet::new(),
                properties: HashSet::new(),
            },
        );
        objects
    }

    fn get_node(&self, node_id: u32) -> Result<&ScannedNode, Error> {
        self.nodes.get(&node_id).ok_or(format_err!("No node at index {}", node_id))
    }

    fn get_property(&self, property_id: u32) -> Result<&ScannedProperty, Error> {
        self.properties.get(&property_id).ok_or(format_err!("No property at index {}", property_id))
    }

    fn get_owned_name(&self, name_id: u32) -> Result<String, Error> {
        Ok(self
            .names
            .get(&name_id)
            .ok_or(format_err!("No string at index {}", name_id))?
            .name
            .clone())
    }

    // ***** Functions which read fuchsia_inspect::format::block::Block (actual
    // ***** VMO blocks), validate them, turn them into Scanned* objects, and
    // ***** add the ones we care about to Self.

    // Some blocks' metrics can only be calculated in the context of a tree. Metrics aren't run
    // on those in the process_ functions, but rather while the tree is being built.

    // TODO(cphoenix): Add full pedantic/paranoid checking on all process_ functions.
    // Note: process_ functions are only called from the scan() iterator on the
    // VMO's blocks, so indexes of the blocks themselves will never be duplicated; that's one
    // thing we don't have to verify.
    fn process_free(&self, _block: Block<&[u8]>) -> Result<(), Error> {
        Ok(())
    }

    fn process_header(&self, _block: Block<&[u8]>) -> Result<(), Error> {
        Ok(())
    }

    fn process_tombstone(&self, _block: Block<&[u8]>) -> Result<(), Error> {
        Ok(())
    }

    fn process_reserved(&self, _block: Block<&[u8]>) -> Result<(), Error> {
        Ok(())
    }

    fn process_extent(&mut self, block: Block<&[u8]>) -> Result<(), Error> {
        self.extents.insert(
            block.index(),
            ScannedExtent { next: block.next_extent()?, data: block.extent_contents()? },
        );
        Ok(())
    }

    fn process_name(&mut self, block: Block<&[u8]>) -> Result<(), Error> {
        self.names.insert(block.index(), ScannedName { name: block.name_contents()? });
        Ok(())
    }

    fn process_node(&mut self, block: Block<&[u8]>) -> Result<(), Error> {
        let parent = block.parent_index()?;
        let id = block.index();
        let name = block.name_index()?;
        let mut node;
        if let Some(placeholder) = self.nodes.remove(&id) {
            // We need to preserve the children and properties.
            node = placeholder;
            node.validated = true;
            node.parent = parent;
            node.name = name;
        } else {
            node = ScannedNode {
                validated: true,
                name,
                parent,
                children: HashSet::new(),
                properties: HashSet::new(),
            }
        }
        self.nodes.insert(id, node);
        self.add_to_parent(parent, id, |node| &mut node.children);
        Ok(())
    }

    fn add_to_parent<F: FnOnce(&mut ScannedNode) -> &mut HashSet<u32>>(
        &mut self,
        parent: u32,
        id: u32,
        get_the_hashset: F, // Gets children or properties
    ) {
        if !self.nodes.contains_key(&parent) {
            self.nodes.insert(
                parent,
                ScannedNode {
                    validated: false,
                    name: 0,
                    parent: 0,
                    children: HashSet::new(),
                    properties: HashSet::new(),
                },
            );
        }
        if let Some(parent_node) = self.nodes.get_mut(&parent) {
            get_the_hashset(parent_node).insert(id);
        }
    }

    fn build_scanned_payload(
        block: &Block<&[u8]>,
        block_type: BlockType,
    ) -> Result<ScannedPayload, Error> {
        Ok(match block_type {
            BlockType::IntValue => ScannedPayload::Int(block.int_value()?),
            BlockType::UintValue => ScannedPayload::Uint(block.uint_value()?),
            BlockType::DoubleValue => ScannedPayload::Double(block.double_value()?),
            BlockType::PropertyValue => {
                let format = block.property_format()?;
                let length = block.property_total_length()?;
                let link = block.property_extent_index()?;
                match format {
                    PropertyFormat::String => ScannedPayload::String { length, link },
                    PropertyFormat::Bytes => ScannedPayload::Bytes { length, link },
                }
            }
            BlockType::ArrayValue => {
                let entry_type = block.array_entry_type()?;
                let array_format = block.array_format()?;
                let slots = block.array_slots()? as usize;
                match entry_type {
                    BlockType::IntValue => {
                        let numbers: Result<Vec<i64>, _> =
                            (0..slots).map(|i| block.array_get_int_slot(i)).collect();
                        ScannedPayload::IntArray(numbers?, array_format)
                    }
                    BlockType::UintValue => {
                        let numbers: Result<Vec<u64>, _> =
                            (0..slots).map(|i| block.array_get_uint_slot(i)).collect();
                        ScannedPayload::UintArray(numbers?, array_format)
                    }
                    BlockType::DoubleValue => {
                        let numbers: Result<Vec<f64>, _> =
                            (0..slots).map(|i| block.array_get_double_slot(i)).collect();
                        ScannedPayload::DoubleArray(numbers?, array_format)
                    }
                    illegal_type => {
                        bail!("No way I should see {:?} for ArrayEntryType", illegal_type)
                    }
                }
            }
            illegal_type => bail!("No way I should see {:?} for BlockType", illegal_type),
        })
    }

    fn process_property(&mut self, block: Block<&[u8]>) -> Result<(), Error> {
        let id = block.index();
        let parent = block.parent_index()?;
        let block_type = block.block_type_or()?;
        let payload = Self::build_scanned_payload(&block, block_type)?;
        let property = ScannedProperty { name: block.name_index()?, parent, payload };
        self.properties.insert(id, property);
        self.add_to_parent(parent, id, |node| &mut node.properties);
        Ok(())
    }

    // ***** Functions which convert Scanned* objects into Node and Property objects.

    fn make_valid_node_tree(
        &self,
        id: u32,
    ) -> Result<(Vec<(Node, u32)>, Vec<(Property, u32)>), Error> {
        let scanned_node = self.get_node(id)?;
        if !scanned_node.validated {
            bail!("No node at {}", id)
        }
        let mut nodes_in_tree = vec![];
        let mut properties_under = vec![];
        for node_id in scanned_node.children.iter() {
            let (mut nodes_of, mut properties_of) = self.make_valid_node_tree(*node_id)?;
            nodes_in_tree.append(&mut nodes_of);
            properties_under.append(&mut properties_of);
        }
        for property_id in scanned_node.properties.iter() {
            properties_under.push((self.make_valid_property(*property_id)?, *property_id));
        }
        let name =
            if id == 0 { ROOT_NAME.to_owned() } else { self.get_owned_name(scanned_node.name)? };
        let this_node = Node {
            name,
            parent: scanned_node.parent,
            children: scanned_node.children.clone(),
            properties: scanned_node.properties.clone(),
        };
        nodes_in_tree.push((this_node, id));
        Ok((nodes_in_tree, properties_under))
    }

    fn make_valid_property(&self, id: u32) -> Result<Property, Error> {
        let scanned_property = self.get_property(id)?;
        let name = self.get_owned_name(scanned_property.name)?;
        let payload = self.make_valid_payload(&scanned_property.payload)?;
        Ok(Property { id, name, parent: scanned_property.parent, payload })
    }

    fn make_valid_payload(&self, payload: &ScannedPayload) -> Result<Payload, Error> {
        Ok(match payload {
            ScannedPayload::Int(data) => Payload::Int(*data),
            ScannedPayload::Uint(data) => Payload::Uint(*data),
            ScannedPayload::Double(data) => Payload::Double(*data),
            ScannedPayload::IntArray(data, format) => {
                Payload::IntArray(data.clone(), format.clone())
            }
            ScannedPayload::UintArray(data, format) => {
                Payload::UintArray(data.clone(), format.clone())
            }
            ScannedPayload::DoubleArray(data, format) => {
                Payload::DoubleArray(data.clone(), format.clone())
            }
            ScannedPayload::Bytes { length, link } => {
                Payload::Bytes(self.make_valid_vector(*length, *link)?)
            }
            ScannedPayload::String { length, link } => Payload::String(
                std::str::from_utf8(&self.make_valid_vector(*length, *link)?)?.to_owned(),
            ),
        })
    }

    fn make_valid_vector(&self, length: usize, link: u32) -> Result<Vec<u8>, Error> {
        let mut dest = vec![];
        let mut length_remaining = length;
        let mut next_link = link;
        while length_remaining > 0 {
            let extent =
                self.extents.get(&next_link).ok_or(format_err!("No extent at {}", next_link))?;
            let copy_len = min(extent.data.len(), length_remaining);
            dest.extend_from_slice(&extent.data[..copy_len]);
            length_remaining -= copy_len;
            next_link = extent.next;
        }
        Ok(dest)
    }
}

#[derive(Debug)]
struct ScannedNode {
    // These may be created two ways: Either from being named as a parent, or
    // from being processed in the VMO. Those named but not yet processed will
    // have validated = false. Of course after a complete VMO scan,
    // everything descended from a root node must be validated.
    // validated refers to the binary contents of this block; it doesn't
    // guarantee that properties, descendents, name, etc. are valid.
    validated: bool,
    name: u32,
    parent: u32,
    children: HashSet<u32>,
    properties: HashSet<u32>,
}

#[derive(Debug)]
struct ScannedProperty {
    name: u32,
    parent: u32,
    payload: ScannedPayload,
}

#[derive(Debug)]
struct ScannedName {
    name: String,
}

#[derive(Debug)]
struct ScannedExtent {
    next: u32,
    data: Vec<u8>,
}

#[derive(Debug)]
enum ScannedPayload {
    String { length: usize, link: u32 },
    Bytes { length: usize, link: u32 },
    Int(i64),
    Uint(u64),
    Double(f64),
    IntArray(Vec<i64>, ArrayFormat),
    UintArray(Vec<u64>, ArrayFormat),
    DoubleArray(Vec<f64>, ArrayFormat),
}

#[cfg(test)]
mod tests {
    use num_traits::ToPrimitive;
    use {
        super::*,
        crate::*,
        fidl_test_inspect_validate::Number,
        fuchsia_async as fasync,
        fuchsia_inspect::format::{bitfields::BlockHeader, block_type::BlockType, constants},
    };

    fn copy_into(source: &[u8], dest: &mut [u8], offset: usize) {
        dest[offset..offset + source.len()].copy_from_slice(source);
    }

    // Run "fx run-test inspect_validator_tests -- --nocapture" to see all the output
    // and verify you're getting appropriate error messages for each tweaked byte.
    // (The alternative is hard-coding expected error strings, which is possible but ugh.)
    fn try_byte(
        buffer: &mut [u8],
        (index, offset): (usize, usize),
        value: u8,
        predicted: Option<&str>,
    ) {
        let location = index * 16 + offset;
        let previous = buffer[location];
        buffer[location] = value;
        let actual = Data::try_from_bytes(buffer).map(|d| d.to_string());
        if predicted.is_none() {
            if actual.is_err() {
                println!(
                    "With ({},{}) -> {}, got expected error {:?}",
                    index, offset, value, actual
                );
            } else {
                println!(
                    "BAD: With ({},{}) -> {}, expected error but got string {:?}",
                    index,
                    offset,
                    value,
                    actual.as_ref().unwrap()
                );
            }
        } else {
            if actual.is_err() {
                println!(
                    "BAD: With ({},{}) -> {}, got unexpected error {:?}",
                    index, offset, value, actual
                );
            } else if actual.as_ref().ok().map(|s| &s[..]) == predicted {
                println!(
                    "With ({},{}) -> {}, got expected string {:?}",
                    index,
                    offset,
                    value,
                    predicted.unwrap()
                );
            } else {
                println!(
                    "BAD: With ({},{}) -> {}, expected string {:?} but got {:?}",
                    index,
                    offset,
                    value,
                    predicted.unwrap(),
                    actual.as_ref().unwrap()
                );
                println!("Raw data: {:?}", Data::try_from_bytes(buffer))
            }
        }
        assert_eq!(predicted, actual.as_ref().ok().map(|s| &s[..]));
        buffer[location] = previous;
    }

    fn put_header(header: &BlockHeader, buffer: &mut [u8], index: usize) {
        copy_into(&header.value().to_le_bytes(), buffer, index * 16);
    }

    #[test]
    fn test_scanning_logic() {
        let mut buffer = [0u8; 4096];
        // VMO Header block (index 0)
        const HEADER: usize = 0;
        let mut header = BlockHeader(0);
        header.set_order(0);
        header.set_block_type(BlockType::Header.to_u8().unwrap());
        header.set_header_magic(constants::HEADER_MAGIC_NUMBER);
        header.set_header_version(constants::HEADER_VERSION_NUMBER);
        put_header(&header, &mut buffer, HEADER);
        const ROOT: usize = 1;
        let mut header = BlockHeader(0);
        header.set_order(0);
        header.set_block_type(BlockType::NodeValue.to_u8().unwrap());
        header.set_value_name_index(2);
        header.set_value_parent_index(0);
        put_header(&header, &mut buffer, ROOT);
        // Root's Name block
        const ROOT_NAME: usize = 2;
        let mut header = BlockHeader(0);
        header.set_order(0);
        header.set_block_type(BlockType::Name.to_u8().unwrap());
        header.set_name_length(4);
        put_header(&header, &mut buffer, ROOT_NAME);
        copy_into(b"node", &mut buffer, ROOT_NAME * 16 + 8);
        try_byte(&mut buffer, (16, 0), 0, Some(" root ->\n\n>  node ->\n\n\n\n"));
        // Mess up HEADER_MAGIC_NUMBER - it should fail to load.
        try_byte(&mut buffer, (HEADER, 7), 0, None);
        // Mess up node's parent; should disappear.
        try_byte(&mut buffer, (ROOT, 1), 1, Some(" root ->\n\n\n"));
        // Mess up root's name; should fail.
        try_byte(&mut buffer, (ROOT, 5), 1, None);
        // Mess up generation count; should fail (and not hang).
        try_byte(&mut buffer, (HEADER, 8), 1, None);
        // But an even generation count should work.
        try_byte(&mut buffer, (HEADER, 8), 2, Some(" root ->\n\n>  node ->\n\n\n\n"));
        // Let's give it a property.
        const NUMBER: usize = 3;
        let mut header = BlockHeader(0);
        header.set_order(0);
        header.set_block_type(BlockType::IntValue.to_u8().unwrap());
        header.set_value_name_index(4);
        header.set_value_parent_index(1);
        put_header(&header, &mut buffer, NUMBER);
        const NUMBER_NAME: usize = 4;
        let mut header = BlockHeader(0);
        header.set_order(0);
        header.set_block_type(BlockType::Name.to_u8().unwrap());
        header.set_name_length(6);
        put_header(&header, &mut buffer, NUMBER_NAME);
        copy_into(b"number", &mut buffer, NUMBER_NAME * 16 + 8);
        try_byte(
            &mut buffer,
            (HEADER, 8),
            2,
            Some(" root ->\n\n>  node ->\n> >  number: Int(0)\n\n\n"),
        );
        try_byte(
            &mut buffer,
            (NUMBER, 0),
            0x50,
            Some(" root ->\n\n>  node ->\n> >  number: Uint(0)\n\n\n"),
        );
        try_byte(
            &mut buffer,
            (NUMBER, 0),
            0x60,
            Some(" root ->\n\n>  node ->\n> >  number: Double(0.0)\n\n\n"),
        );
        try_byte(
            &mut buffer,
            (NUMBER, 0),
            0x70,
            Some(" root ->\n\n>  node ->\n> >  number: String(\"\")\n\n\n"),
        );
        // Array block will have illegal Array Entry Type of 0.
        try_byte(&mut buffer, (NUMBER, 0), 0xb0, None);
        // 15 is an illegal block type.
        try_byte(&mut buffer, (NUMBER, 0), 0xf0, None);

        // TODO: Test more cases here.
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_to_string_order() -> Result<(), Error> {
        // Make sure property payloads are distinguished by name, value, and type
        // but ignore id and parent, and that prefix is used.
        let int0 = Property { name: "int0".into(), id: 2, parent: 1, payload: Payload::Int(0) }
            .to_string("");
        let int1_struct =
            Property { name: "int1".into(), id: 2, parent: 1, payload: Payload::Int(1) };
        let int1 = int1_struct.to_string("");
        assert_ne!(int0, int1);
        let uint0 = Property { name: "uint0".into(), id: 2, parent: 1, payload: Payload::Uint(0) }
            .to_string("");
        assert_ne!(int0, uint0);
        let int0_different_name = Property {
            name: "int0_different_name".into(),
            id: 2,
            parent: 1,
            payload: Payload::Int(0),
        }
        .to_string("");
        assert_ne!(int0, int0_different_name);
        let uint0_different_ids =
            Property { name: "uint0".into(), id: 3, parent: 4, payload: Payload::Uint(0) }
                .to_string("");
        assert_eq!(uint0, uint0_different_ids);
        let int1_different_prefix = int1_struct.to_string("foo");
        assert_ne!(int1, int1_different_prefix);
        // Test that order doesn't matter. Use a real VMO rather than Data's
        // HashMaps which may not reflect order of addition.
        let mut puppet1 = puppet::tests::local_incomplete_puppet().await?;
        let mut child1_action = create_node!(parent:0, id:1, name:"child1");
        let mut child2_action = create_node!(parent:0, id:2, name:"child2");
        let mut property1_action =
            create_numeric_property!(parent:0, id:1, name:"prop1", value: Number::IntT(1));
        let mut property2_action =
            create_numeric_property!(parent:0, id:2, name:"prop2", value: Number::IntT(2));
        puppet1.apply(&mut child1_action).await?;
        puppet1.apply(&mut child2_action).await?;
        let mut puppet2 = puppet::tests::local_incomplete_puppet().await?;
        puppet2.apply(&mut child2_action).await?;
        puppet2.apply(&mut child1_action).await?;
        assert_eq!(puppet1.read_data()?.to_string(), puppet2.read_data()?.to_string());
        puppet1.apply(&mut property1_action).await?;
        puppet1.apply(&mut property2_action).await?;
        puppet2.apply(&mut property2_action).await?;
        puppet2.apply(&mut property1_action).await?;
        assert_eq!(puppet1.read_data()?.to_string(), puppet2.read_data()?.to_string());
        // Make sure the tree distinguishes based on node position
        puppet1 = puppet::tests::local_incomplete_puppet().await?;
        puppet2 = puppet::tests::local_incomplete_puppet().await?;
        let mut subchild2_action = create_node!(parent:1, id:2, name:"child2");
        puppet1.apply(&mut child1_action).await?;
        puppet2.apply(&mut child1_action).await?;
        puppet1.apply(&mut child2_action).await?;
        puppet2.apply(&mut subchild2_action).await?;
        assert_ne!(puppet1.read_data()?.to_string(), puppet2.read_data()?.to_string());
        // ... and property position
        let mut subproperty2_action =
            create_numeric_property!(parent:1, id:2, name:"prop2", value: Number::IntT(1));
        puppet1.apply(&mut child1_action).await?;
        puppet2.apply(&mut child1_action).await?;
        puppet1.apply(&mut property2_action).await?;
        puppet2.apply(&mut subproperty2_action).await?;
        Ok(())
    }
}
