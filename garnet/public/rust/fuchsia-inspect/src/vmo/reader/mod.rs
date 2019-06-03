// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::vmo::{
        block::PropertyFormat,
        block_type::BlockType,
        reader::snapshot::{ScannedBlock, Snapshot},
        Inspector,
    },
    failure::{format_err, Error},
    fuchsia_zircon::Vmo,
    std::{cmp::min, collections::BTreeMap, convert::TryFrom},
};

pub mod snapshot;

/// A hierarchy of Inspect Nodes.
///
/// Each hierarchy consists of properties, and a map of named child hierarchies.
#[derive(Debug, PartialEq, Clone)]
pub struct NodeHierarchy {
    /// The name of this node.
    pub name: String,

    /// The properties for the node.
    pub properties: Vec<Property>,

    /// The children of this node.
    pub children: Vec<NodeHierarchy>,
}

impl NodeHierarchy {
    fn new() -> Self {
        NodeHierarchy { name: "".to_string(), properties: vec![], children: vec![] }
    }
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
    UInt(String, u64),

    /// The value is a double.
    Double(String, f64),
    // TODO(CF-798): add array types
}

impl Property {
    pub fn name(&self) -> &str {
        match self {
            Property::String(name, _)
            | Property::Bytes(name, _)
            | Property::Int(name, _)
            | Property::UInt(name, _)
            | Property::Double(name, _) => &name,
        }
    }
}

impl TryFrom<Snapshot> for NodeHierarchy {
    type Error = failure::Error;

    fn try_from(snapshot: Snapshot) -> Result<Self, Self::Error> {
        read(&snapshot)
    }
}

impl TryFrom<&Vmo> for NodeHierarchy {
    type Error = failure::Error;

    fn try_from(vmo: &Vmo) -> Result<Self, Self::Error> {
        let snapshot = Snapshot::try_from(vmo)?;
        read(&snapshot)
    }
}

impl TryFrom<&Inspector> for NodeHierarchy {
    type Error = failure::Error;

    fn try_from(inspector: &Inspector) -> Result<Self, Self::Error> {
        let snapshot = Snapshot::try_from(inspector)?;
        read(&snapshot)
    }
}

/// Read the blocks in the snapshot as a node hierarchy.
fn read(snapshot: &Snapshot) -> Result<NodeHierarchy, Error> {
    let result = scan_blocks(snapshot)?;
    result.reduce()
}

fn scan_blocks<'a>(snapshot: &'a Snapshot) -> Result<ScanResult<'a>, Error> {
    let mut result = ScanResult::new(snapshot);
    for block in snapshot.scan() {
        if block.index() == 0 && block.block_type() != BlockType::Header {
            return Err(format_err!("expected header block on index 0"));
        }
        match block.block_type() {
            BlockType::NodeValue => {
                result.parse_node(&block)?;
            }
            // TODO(CF-798): add ArrayValue
            BlockType::IntValue | BlockType::UintValue | BlockType::DoubleValue => {
                result.parse_numeric_property(&block)?;
            }
            BlockType::PropertyValue => {
                result.parse_property(&block)?;
            }
            _ => {}
        }
    }
    Ok(result)
}

/// Result of scanning a snapshot before aggregating hierarchies.
struct ScanResult<'a> {
    /// All the nodes found while scanning the snapshot.
    /// Scanned nodes NodeHierarchies won't have their children filled.
    parsed_nodes: BTreeMap<u32, ScannedNode>,

    /// A snapshot of the Inspect VMO tree.
    snapshot: &'a Snapshot,
}

/// A scanned node in the Inspect VMO tree.
struct ScannedNode {
    /// The node hierarchy with properties and children nodes filled.
    hierarchy: NodeHierarchy,

    /// The number of children nodes this node has.
    child_nodes_count: usize,

    /// The index of the parent node of this node.
    parent_index: u32,
}

impl ScannedNode {
    fn new() -> Self {
        ScannedNode { hierarchy: NodeHierarchy::new(), child_nodes_count: 0, parent_index: 0 }
    }

    /// Sets the name and parent index of the node.
    fn initialize(&mut self, name: String, parent_index: u32) {
        self.hierarchy.name = name;
        self.parent_index = parent_index;
    }

    /// A scanned node is considered complete if the number of children in the
    /// hierarchy is the same as the number of children counted while scanning.
    fn is_complete(&self) -> bool {
        self.hierarchy.children.len() == self.child_nodes_count
    }
}

macro_rules! get_or_create_scanned_node {
    ($map:expr, $key:expr) => {
        $map.entry($key).or_insert(ScannedNode::new())
    };
}

impl<'a> ScanResult<'a> {
    fn new(snapshot: &'a Snapshot) -> Self {
        ScanResult { snapshot, parsed_nodes: BTreeMap::new() }
    }

    fn reduce(self) -> Result<NodeHierarchy, Error> {
        // Stack of nodes that have been found that are complete.
        let mut complete_nodes = Vec::<ScannedNode>::new();

        // Maps a block index to the node there. These nodes are still not
        // complete.
        let mut pending_nodes = BTreeMap::<u32, ScannedNode>::new();

        // Split the parsed_nodes into complete nodes and pending nodes.
        for (index, scanned_node) in self.parsed_nodes.into_iter() {
            if scanned_node.is_complete() {
                complete_nodes.push(scanned_node);
            } else {
                pending_nodes.insert(index, scanned_node);
            }
        }

        // Build a valid hierarchy by attaching completed nodes to their parent.
        // Once the parent is complete, it's added to the stack and we recurse
        // until the root is found (parent index = 0).
        while complete_nodes.len() > 0 {
            let scanned_node = complete_nodes.pop().unwrap();
            if scanned_node.parent_index == 0 {
                return Ok(scanned_node.hierarchy);
            }
            {
                // Add the current node to the parent hierarchy.
                let parent_node = pending_nodes
                    .get_mut(&scanned_node.parent_index)
                    .ok_or(format_err!("Cannot find index {}", scanned_node.parent_index))?;
                parent_node.hierarchy.children.push(scanned_node.hierarchy);
            }
            // If the parent node is complete now, then push it to the stack.
            if pending_nodes
                .get(&scanned_node.parent_index)
                .ok_or(format_err!("Cannot find index {}", scanned_node.parent_index))?
                .is_complete()
            {
                let parent_node = pending_nodes.remove(&scanned_node.parent_index).unwrap();
                complete_nodes.push(parent_node);
            }
        }

        Err(format_err!("Malformed tree, no complete node with parent=0"))
    }

    fn get_name(&self, index: u32) -> Option<String> {
        self.snapshot.get_block(index).and_then(|block| block.name_contents().ok())
    }

    fn parse_node(&mut self, block: &ScannedBlock) -> Result<(), Error> {
        let name = self.get_name(block.name_index()?).ok_or(format_err!("failed to parse name"))?;
        let parent_index = block.parent_index()?;
        get_or_create_scanned_node!(self.parsed_nodes, block.index())
            .initialize(name, parent_index);
        if parent_index != 0 && parent_index != block.index() {
            get_or_create_scanned_node!(self.parsed_nodes, parent_index).child_nodes_count += 1;
        }
        Ok(())
    }

    fn parse_numeric_property(&mut self, block: &ScannedBlock) -> Result<(), Error> {
        let name = self.get_name(block.name_index()?).ok_or(format_err!("failed to parse name"))?;
        let parent = get_or_create_scanned_node!(self.parsed_nodes, block.parent_index()?);
        match block.block_type() {
            BlockType::IntValue => {
                let value = block.int_value()?;
                parent.hierarchy.properties.push(Property::Int(name, value));
            }
            BlockType::UintValue => {
                let value = block.uint_value()?;
                parent.hierarchy.properties.push(Property::UInt(name, value));
            }
            BlockType::DoubleValue => {
                let value = block.double_value()?;
                parent.hierarchy.properties.push(Property::Double(name, value));
            }
            // TODO(CF-798): array types.
            _ => {}
        }
        Ok(())
    }

    fn parse_property(&mut self, block: &ScannedBlock) -> Result<(), Error> {
        let name = self.get_name(block.name_index()?).ok_or(format_err!("failed to parse name"))?;
        let parent = get_or_create_scanned_node!(self.parsed_nodes, block.parent_index()?);
        let total_length = block.property_total_length()?;
        let mut buffer = vec![0u8; block.property_total_length()?];
        let mut extent_index = block.property_extent_index()?;
        let mut offset = 0;
        // Incrementally add the contents of each extent in the extent linked list
        // until we reach the last extent or the maximum expected length.
        while extent_index != 0 && offset < total_length {
            let extent =
                self.snapshot.get_block(extent_index).ok_or(format_err!("failed to get extent"))?;
            let content = extent.extent_contents()?;
            let extent_length = min(total_length - offset, content.len());
            buffer[offset..offset + extent_length].copy_from_slice(&content[..extent_length]);
            offset += extent_length;
            extent_index = extent.next_extent()?;
        }
        match block.property_format()? {
            PropertyFormat::String => {
                parent
                    .hierarchy
                    .properties
                    // TODO(CF-808): support lossy conversions to strings.
                    .push(Property::String(name, String::from_utf8(buffer)?));
            }
            PropertyFormat::Bytes => {
                parent.hierarchy.properties.push(Property::Bytes(name, buffer));
            }
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn read_vmo() {
        let inspector = Inspector::new().unwrap();
        let root = inspector.root();
        let _root_int = root.create_int("int-root", 3);

        let child1 = root.create_child("child-1");
        let _child1_uint = child1.create_uint("property-uint", 10);
        let _child1_double = child1.create_double("property-double", -3.4);

        let chars = ['a', 'b', 'c', 'd', 'e', 'f', 'g'];
        let string_data = chars.iter().cycle().take(6000).collect::<String>();
        let _string_prop = child1.create_string("property-string", &string_data);

        let child2 = root.create_child("child-2");
        let _child2_double = child2.create_double("property-double", 5.8);

        let child3 = child1.create_child("child-1-1");
        let _child3_int = child3.create_int("property-int", -9);
        let bytes_data = (0u8..=9u8).cycle().take(5000).collect::<Vec<u8>>();
        let _bytes_prop = child3.create_bytes("property-bytes", &bytes_data);

        let result = NodeHierarchy::try_from(&inspector).unwrap();

        assert_eq!(
            result,
            NodeHierarchy {
                name: "root".to_string(),
                properties: vec![Property::Int("int-root".to_string(), 3),],
                children: vec![
                    NodeHierarchy {
                        name: "child-1".to_string(),
                        properties: vec![
                            Property::UInt("property-uint".to_string(), 10),
                            Property::Double("property-double".to_string(), -3.4),
                            Property::String("property-string".to_string(), string_data),
                        ],
                        children: vec![NodeHierarchy {
                            name: "child-1-1".to_string(),
                            properties: vec![
                                Property::Int("property-int".to_string(), -9),
                                Property::Bytes("property-bytes".to_string(), bytes_data),
                            ],
                            children: vec![],
                        },],
                    },
                    NodeHierarchy {
                        name: "child-2".to_string(),
                        properties: vec![Property::Double("property-double".to_string(), 5.8),],
                        children: vec![],
                    },
                ],
            }
        );
    }
}
