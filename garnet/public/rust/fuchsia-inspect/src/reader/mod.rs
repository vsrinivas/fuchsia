// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        block::PropertyFormat,
        block_type::BlockType,
        reader::snapshot::{ScannedBlock, Snapshot},
        utils, Inspector,
    },
    failure::{bail, format_err, Error},
    fuchsia_zircon::Vmo,
    std::{cmp::min, collections::BTreeMap, convert::TryFrom},
};

pub use crate::block::ArrayFormat;

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
    Uint(String, u64),

    /// The value is a double.
    Double(String, f64),

    /// The value is a double array.
    DoubleArray(String, Vec<f64>, ArrayFormat),

    /// The value is an integer.
    IntArray(String, Vec<i64>, ArrayFormat),

    /// The value is an unsigned integer.
    UintArray(String, Vec<u64>, ArrayFormat),
}

impl Property {
    pub fn name(&self) -> &str {
        match self {
            Property::String(name, _)
            | Property::Bytes(name, _)
            | Property::Int(name, _)
            | Property::IntArray(name, _, _)
            | Property::Uint(name, _)
            | Property::UintArray(name, _, _)
            | Property::Double(name, _)
            | Property::DoubleArray(name, _, _) => &name,
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
            bail!("expected header block on index 0");
        }
        match block.block_type() {
            BlockType::NodeValue => {
                result.parse_node(&block)?;
            }
            BlockType::IntValue | BlockType::UintValue | BlockType::DoubleValue => {
                result.parse_numeric_property(&block)?;
            }
            BlockType::ArrayValue => {
                result.parse_array_property(&block)?;
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

        bail!("Malformed tree, no complete node with parent=0");
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
                parent.hierarchy.properties.push(Property::Uint(name, value));
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

    fn parse_array_property(&mut self, block: &ScannedBlock) -> Result<(), Error> {
        let name = self.get_name(block.name_index()?).ok_or(format_err!("failed to parse name"))?;
        let parent = get_or_create_scanned_node!(self.parsed_nodes, block.parent_index()?);
        let array_slots = block.array_slots()? as usize;
        if utils::array_capacity(block.order()) < array_slots {
            bail!("Tried to read more slots than available");
        }
        let value_indexes = 0..array_slots;
        match block.array_entry_type()? {
            BlockType::IntValue => {
                let values = value_indexes
                    .map(|i| block.array_get_int_slot(i).unwrap())
                    .collect::<Vec<i64>>();
                parent.hierarchy.properties.push(Property::IntArray(
                    name,
                    values,
                    block.array_format().unwrap(),
                ));
            }
            BlockType::UintValue => {
                let values = value_indexes
                    .map(|i| block.array_get_uint_slot(i).unwrap())
                    .collect::<Vec<u64>>();
                parent.hierarchy.properties.push(Property::UintArray(
                    name,
                    values,
                    block.array_format().unwrap(),
                ));
            }
            BlockType::DoubleValue => {
                let values = value_indexes
                    .map(|i| block.array_get_double_slot(i).unwrap())
                    .collect::<Vec<f64>>();
                parent.hierarchy.properties.push(Property::DoubleArray(
                    name,
                    values,
                    block.array_format().unwrap(),
                ));
            }
            _ => bail!("Unexpected array entry type format"),
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
                    .push(Property::String(name, String::from_utf8_lossy(&buffer).to_string()));
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
    use {
        super::*,
        crate::{
            bitfields::Payload, constants, ArrayProperty, ExponentialHistogramParams,
            HistogramProperty, LinearHistogramParams,
        },
    };

    #[test]
    fn read_vmo() {
        let inspector = Inspector::new();
        let root = inspector.root();
        let _root_int = root.create_int("int-root", 3);
        let root_double_array = root.create_double_array("property-double-array", 5);
        let double_array_data = vec![-1.2, 2.3, 3.4, 4.5, -5.6];
        for (i, x) in double_array_data.iter().enumerate() {
            root_double_array.set(i, *x);
        }

        let child1 = root.create_child("child-1");
        let _child1_uint = child1.create_uint("property-uint", 10);
        let _child1_double = child1.create_double("property-double", -3.4);

        let chars = ['a', 'b', 'c', 'd', 'e', 'f', 'g'];
        let string_data = chars.iter().cycle().take(6000).collect::<String>();
        let _string_prop = child1.create_string("property-string", &string_data);

        let child1_int_array = child1.create_int_linear_histogram(
            "property-int-array",
            LinearHistogramParams { floor: 1, step_size: 2, buckets: 3 },
        );
        for x in [-1, 2, 3, 5, 8].iter() {
            child1_int_array.insert(*x);
        }

        let child2 = root.create_child("child-2");
        let _child2_double = child2.create_double("property-double", 5.8);

        let child3 = child1.create_child("child-1-1");
        let _child3_int = child3.create_int("property-int", -9);
        let bytes_data = (0u8..=9u8).cycle().take(5000).collect::<Vec<u8>>();
        let _bytes_prop = child3.create_bytes("property-bytes", &bytes_data);

        let child3_int_array = child3.create_uint_exponential_histogram(
            "property-uint-array",
            ExponentialHistogramParams {
                floor: 1,
                initial_step: 1,
                step_multiplier: 2,
                buckets: 4,
            },
        );
        for x in [1, 2, 3, 4].iter() {
            child3_int_array.insert(*x);
        }

        let result = NodeHierarchy::try_from(&inspector).unwrap();

        assert_eq!(
            result,
            NodeHierarchy {
                name: "root".to_string(),
                properties: vec![
                    Property::Int("int-root".to_string(), 3),
                    Property::DoubleArray(
                        "property-double-array".to_string(),
                        double_array_data,
                        ArrayFormat::Default
                    ),
                ],
                children: vec![
                    NodeHierarchy {
                        name: "child-1".to_string(),
                        properties: vec![
                            Property::Uint("property-uint".to_string(), 10),
                            Property::Double("property-double".to_string(), -3.4),
                            Property::String("property-string".to_string(), string_data),
                            Property::IntArray(
                                "property-int-array".to_string(),
                                vec![1, 2, 1, 1, 1, 1, 1],
                                ArrayFormat::LinearHistogram
                            ),
                        ],
                        children: vec![NodeHierarchy {
                            name: "child-1-1".to_string(),
                            properties: vec![
                                Property::Int("property-int".to_string(), -9),
                                Property::Bytes("property-bytes".to_string(), bytes_data),
                                Property::UintArray(
                                    "property-uint-array".to_string(),
                                    vec![1, 1, 2, 0, 1, 2, 1, 0, 0],
                                    ArrayFormat::ExponentialHistogram
                                ),
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

    #[test]
    fn from_invalid_utf8_string() {
        // Creates a perfectly normal Inspector with a perfectly normal string
        // property with a perfectly normal value.
        let inspector = Inspector::new();
        let root = inspector.root();
        let prop = root.create_string("property", "hello world");

        // Now we will excavate the bytes that comprise the string property, then mess with them on
        // purpose to produce an invalid UTF8 string in the property.
        let vmo = inspector.vmo.as_ref().unwrap();
        let snapshot = Snapshot::try_from(vmo).expect("getting snapshot");
        let block = snapshot.get_block(prop.block_index()).expect("getting block");

        // The first byte of the actual property string is at this byte offset in the VMO.
        let byte_offset = constants::MIN_ORDER_SIZE
            * (block.property_extent_index().unwrap() as usize)
            + constants::HEADER_SIZE_BYTES;

        // Get the raw VMO bytes to mess with.
        let vmo_size = vmo.get_size().expect("VMO size");
        let mut buf = vec![0u8; vmo_size as usize];
        vmo.read(&mut buf[..], /*offset=*/ 0).expect("read is a success");

        // Mess up the first byte of the string property value such that the byte is an invalid
        // UTF8 character.  Then build a new node hierarchy based off those bytes, see if invalid
        // string is converted into a valid UTF8 string with some information lost.
        buf[byte_offset] = 0xFE;
        let nh = NodeHierarchy::try_from(Snapshot::build(&buf)).expect("creating node hierarchy");

        assert_eq!(
            nh,
            NodeHierarchy {
                name: "root".to_string(),
                properties: vec![Property::String(
                    "property".to_string(),
                    "\u{FFFD}ello world".to_string()
                )],
                children: vec![],
            },
        );
    }

    #[test]
    fn test_invalid_array_slots() -> Result<(), Error> {
        let inspector = Inspector::new();
        let root = inspector.root();
        let array = root.create_int_array("int-array", 3);

        let vmo = inspector.vmo.as_ref().unwrap();
        let vmo_size = vmo.get_size()?;
        let mut buf = vec![0u8; vmo_size as usize];
        vmo.read(&mut buf[..], /*offset=*/ 0)?;

        // Mess up with the block slots by setting them to a too big number.
        let offset = utils::offset_for_index(array.block_index()) + 8;
        let mut payload =
            Payload(u64::from_le_bytes(*<&[u8; 8]>::try_from(&buf[offset..offset + 8])?));
        payload.set_array_slots_count(255);

        buf[offset..offset + 8].clone_from_slice(&payload.value().to_le_bytes()[..]);
        assert!(NodeHierarchy::try_from(Snapshot::build(&buf)).is_err());

        Ok(())
    }
}
