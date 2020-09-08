// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        format::{block::PropertyFormat, block_type::BlockType},
        reader::snapshot::{ScannedBlock, Snapshot},
        utils, Inspector,
    },
    anyhow::{format_err, Error},
    fidl_fuchsia_inspect::TreeProxy,
    fuchsia_inspect_node_hierarchy::{testing::NodeHierarchyGetter, *},
    fuchsia_zircon::Vmo,
    maplit::btreemap,
    std::{borrow::Cow, cmp::min, collections::BTreeMap, convert::TryFrom},
};

pub use {
    crate::reader::readable_tree::ReadableTree,
    fuchsia_inspect_node_hierarchy::{
        ArrayContent, ArrayFormat, Bucket, LinkNodeDisposition, LinkValue, NodeHierarchy, Property,
    },
};

mod readable_tree;
#[allow(missing_docs)]
pub mod snapshot;
mod tree_reader;

/// Read a NodeHierarchy from an |Inspector| object.
pub async fn read_from_inspector(inspector: &Inspector) -> Result<NodeHierarchy, Error> {
    tree_reader::read(inspector).await
}

/// Read a NodeHierarchy from a |Tree| connection.
pub async fn read_from_tree(tree: &TreeProxy) -> Result<NodeHierarchy, Error> {
    tree_reader::read(tree).await
}

/// A partial node hierarchy represents a node in an inspect tree without
/// the linked (lazy) nodes expanded.
/// Usually a client would prefer to use a `NodeHierarchy` to get the full
/// inspect tree.
#[derive(Clone, Debug, PartialEq)]
pub struct PartialNodeHierarchy {
    /// The name of this node.
    pub(in crate) name: String,

    /// The properties for the node.
    pub(in crate) properties: Vec<Property>,

    /// The children of this node.
    pub(in crate) children: Vec<PartialNodeHierarchy>,

    /// Links this node hierarchy haven't expanded yet.
    pub(in crate) links: Vec<LinkValue>,
}

impl PartialNodeHierarchy {
    /// Creates an `PartialNodeHierarchy` with the given `name`, `properties` and `children`
    pub fn new(
        name: impl Into<String>,
        properties: Vec<Property>,
        children: Vec<PartialNodeHierarchy>,
    ) -> Self {
        Self { name: name.into(), properties, children, links: vec![] }
    }

    /// Creates an empty `PartialNodeHierarchy`
    pub fn empty() -> Self {
        PartialNodeHierarchy::new("", vec![], vec![])
    }

    /// Whether the partial hierarchy is complete or not. A complete node hierarchy
    /// has all the links loaded into it.
    pub fn is_complete(&self) -> bool {
        self.links.is_empty()
    }
}

/// Transforms the partial hierarchy into a `NodeHierarchy`. If the node hierarchy had
/// unexpanded links, those will appear as missing values.
impl Into<NodeHierarchy> for PartialNodeHierarchy {
    fn into(self) -> NodeHierarchy {
        let hierarchy = NodeHierarchy {
            name: self.name,
            children: self.children.into_iter().map(|child| child.into()).collect(),
            properties: self.properties,
            missing: self
                .links
                .into_iter()
                .map(|link_value| MissingValue {
                    reason: MissingValueReason::LinkNeverExpanded,
                    name: link_value.name,
                })
                .collect(),
        };
        hierarchy
    }
}

impl NodeHierarchyGetter<String> for PartialNodeHierarchy {
    fn get_node_hierarchy(&self) -> Cow<NodeHierarchy> {
        let hierarchy: NodeHierarchy = self.clone().into();
        if !hierarchy.missing.is_empty() {
            panic!(
                "Missing links: {:?}",
                hierarchy
                    .missing
                    .iter()
                    .map(|missing| {
                        format!("(name:{:?}, reason:{:?})", missing.name, missing.reason)
                    })
                    .collect::<Vec<_>>()
                    .join(", ")
            );
        }
        Cow::Owned(hierarchy)
    }
}

impl TryFrom<Snapshot> for PartialNodeHierarchy {
    type Error = anyhow::Error;

    fn try_from(snapshot: Snapshot) -> Result<Self, Self::Error> {
        read(&snapshot)
    }
}

impl TryFrom<&Vmo> for PartialNodeHierarchy {
    type Error = anyhow::Error;

    fn try_from(vmo: &Vmo) -> Result<Self, Self::Error> {
        let snapshot = Snapshot::try_from(vmo)?;
        read(&snapshot)
    }
}

impl TryFrom<Vec<u8>> for PartialNodeHierarchy {
    type Error = anyhow::Error;

    fn try_from(bytes: Vec<u8>) -> Result<Self, Self::Error> {
        let snapshot = Snapshot::try_from(&bytes[..])?;
        read(&snapshot)
    }
}

/// Read the blocks in the snapshot as a node hierarchy.
fn read(snapshot: &Snapshot) -> Result<PartialNodeHierarchy, Error> {
    let result = scan_blocks(snapshot)?;
    result.reduce()
}

fn scan_blocks<'a>(snapshot: &'a Snapshot) -> Result<ScanResult<'a>, Error> {
    let mut result = ScanResult::new(snapshot);
    for block in snapshot.scan() {
        if block.index() == 0 && block.block_type_or()? != BlockType::Header {
            return Err(format_err!("expected header block on index 0"));
        }
        match block.block_type_or()? {
            BlockType::NodeValue => {
                result.parse_node(&block)?;
            }
            BlockType::IntValue | BlockType::UintValue | BlockType::DoubleValue => {
                result.parse_numeric_property(&block)?;
            }
            BlockType::BoolValue => {
                result.parse_bool_property(&block)?;
            }
            BlockType::ArrayValue => {
                result.parse_array_property(&block)?;
            }
            BlockType::BufferValue => {
                result.parse_property(&block)?;
            }
            BlockType::LinkValue => {
                result.parse_link(&block)?;
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
#[derive(Debug)]
struct ScannedNode {
    /// The node hierarchy with properties and children nodes filled.
    partial_hierarchy: PartialNodeHierarchy,

    /// The number of children nodes this node has.
    child_nodes_count: usize,

    /// The index of the parent node of this node.
    parent_index: u32,

    /// True only if this node was intialized. Uninitialized nodes will be ignored.
    initialized: bool,
}

impl ScannedNode {
    fn new() -> Self {
        ScannedNode {
            partial_hierarchy: PartialNodeHierarchy::empty(),
            child_nodes_count: 0,
            parent_index: 0,
            initialized: false,
        }
    }

    /// Sets the name and parent index of the node.
    fn initialize(&mut self, name: String, parent_index: u32) {
        self.partial_hierarchy.name = name;
        self.parent_index = parent_index;
        self.initialized = true;
    }

    /// A scanned node is considered complete if the number of children in the
    /// hierarchy is the same as the number of children counted while scanning.
    fn is_complete(&self) -> bool {
        self.partial_hierarchy.children.len() == self.child_nodes_count
    }

    /// A scanned node is considered initialized if a NodeValue was parsed for it.
    fn is_initialized(&self) -> bool {
        self.initialized
    }
}

macro_rules! get_or_create_scanned_node {
    ($map:expr, $key:expr) => {
        $map.entry($key).or_insert(ScannedNode::new())
    };
}

impl<'a> ScanResult<'a> {
    fn new(snapshot: &'a Snapshot) -> Self {
        let mut root_node = ScannedNode::new();
        root_node.initialize("root".to_string(), 0);
        let parsed_nodes = btreemap!(
            0 => root_node,
        );
        ScanResult { snapshot, parsed_nodes }
    }

    fn reduce(self) -> Result<PartialNodeHierarchy, Error> {
        // Stack of nodes that have been found that are complete.
        let mut complete_nodes = Vec::<ScannedNode>::new();

        // Maps a block index to the node there. These nodes are still not
        // complete.
        let mut pending_nodes = BTreeMap::<u32, ScannedNode>::new();

        let mut uninitialized_nodes = std::collections::BTreeSet::new();

        // Split the parsed_nodes into complete nodes and pending nodes.
        for (index, scanned_node) in self.parsed_nodes.into_iter() {
            if !scanned_node.is_initialized() {
                // Skip all nodes that were not initialized.
                uninitialized_nodes.insert(index);
                continue;
            }
            if scanned_node.is_complete() {
                if index == 0 {
                    return Ok(scanned_node.partial_hierarchy);
                }
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
            if uninitialized_nodes.contains(&scanned_node.parent_index) {
                // Skip children of initialized nodes. These nodes were implicitly unlinked due to
                // tombstoning.
                continue;
            }
            {
                // Add the current node to the parent hierarchy.
                let parent_node = pending_nodes
                    .get_mut(&scanned_node.parent_index)
                    .ok_or(format_err!("Cannot find index {}", scanned_node.parent_index))?;
                parent_node.partial_hierarchy.children.push(scanned_node.partial_hierarchy);
            }
            if pending_nodes
                .get(&scanned_node.parent_index)
                .ok_or(format_err!("Cannot find index {}", scanned_node.parent_index))?
                .is_complete()
            {
                let parent_node = pending_nodes.remove(&scanned_node.parent_index).unwrap();
                if scanned_node.parent_index == 0 {
                    return Ok(parent_node.partial_hierarchy);
                }
                complete_nodes.push(parent_node);
            }
        }

        return Err(format_err!("Malformed tree, no complete node with parent=0"));
    }

    fn get_name(&self, index: u32) -> Option<String> {
        self.snapshot.get_block(index).and_then(|block| block.name_contents().ok())
    }

    fn parse_node(&mut self, block: &ScannedBlock) -> Result<(), Error> {
        let name = self.get_name(block.name_index()?).ok_or(format_err!("failed to parse name"))?;
        let parent_index = block.parent_index()?;
        get_or_create_scanned_node!(self.parsed_nodes, block.index())
            .initialize(name, parent_index);
        if parent_index != block.index() {
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
                parent.partial_hierarchy.properties.push(Property::Int(name, value));
            }
            BlockType::UintValue => {
                let value = block.uint_value()?;
                parent.partial_hierarchy.properties.push(Property::Uint(name, value));
            }
            BlockType::DoubleValue => {
                let value = block.double_value()?;
                parent.partial_hierarchy.properties.push(Property::Double(name, value));
            }
            _ => {}
        }
        Ok(())
    }

    fn parse_bool_property(&mut self, block: &ScannedBlock) -> Result<(), Error> {
        let name = self.get_name(block.name_index()?).ok_or(format_err!("failed to parse name"))?;
        let parent = get_or_create_scanned_node!(self.parsed_nodes, block.parent_index()?);
        match block.block_type() {
            BlockType::BoolValue => {
                let value = block.bool_value()?;
                parent.partial_hierarchy.properties.push(Property::Bool(name, value));
            }
            _ => {}
        }
        Ok(())
    }

    fn parse_array_property(&mut self, block: &ScannedBlock) -> Result<(), Error> {
        let name = self.get_name(block.name_index()?).ok_or(format_err!("failed to parse name"))?;
        let parent = get_or_create_scanned_node!(self.parsed_nodes, block.parent_index()?);
        let array_slots = block.array_slots()?;
        if utils::array_capacity(block.order()) < array_slots {
            return Err(format_err!("Tried to read more slots than available"));
        }
        let value_indexes = 0..array_slots;
        match block.array_entry_type()? {
            BlockType::IntValue => {
                let values = value_indexes
                    .map(|i| block.array_get_int_slot(i).unwrap())
                    .collect::<Vec<i64>>();
                parent.partial_hierarchy.properties.push(Property::IntArray(
                    name,
                    ArrayContent::new(values, block.array_format().unwrap())?,
                ));
            }
            BlockType::UintValue => {
                let values = value_indexes
                    .map(|i| block.array_get_uint_slot(i).unwrap())
                    .collect::<Vec<u64>>();
                parent.partial_hierarchy.properties.push(Property::UintArray(
                    name,
                    ArrayContent::new(values, block.array_format().unwrap())?,
                ));
            }
            BlockType::DoubleValue => {
                let values = value_indexes
                    .map(|i| block.array_get_double_slot(i).unwrap())
                    .collect::<Vec<f64>>();
                parent.partial_hierarchy.properties.push(Property::DoubleArray(
                    name,
                    ArrayContent::new(values, block.array_format().unwrap())?,
                ));
            }
            _ => return Err(format_err!("Unexpected array entry type format")),
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
                    .partial_hierarchy
                    .properties
                    .push(Property::String(name, String::from_utf8_lossy(&buffer).to_string()));
            }
            PropertyFormat::Bytes => {
                parent.partial_hierarchy.properties.push(Property::Bytes(name, buffer));
            }
        }
        Ok(())
    }

    fn parse_link(&mut self, block: &ScannedBlock) -> Result<(), Error> {
        let name = self.get_name(block.name_index()?).ok_or(format_err!("failed to parse name"))?;
        let parent = get_or_create_scanned_node!(self.parsed_nodes, block.parent_index()?);
        let content = self
            .snapshot
            .get_block(block.link_content_index()?)
            .ok_or(format_err!("failed to get link content block"))?
            .name_contents()?;
        let disposition = block.link_node_disposition()?;
        parent.partial_hierarchy.links.push(LinkValue { name, content, disposition });
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            assert_inspect_tree,
            format::{bitfields::Payload, constants},
            ArrayProperty, ExponentialHistogramParams, HistogramProperty, LinearHistogramParams,
        },
        fuchsia_async as fasync,
        futures::prelude::*,
    };

    #[fasync::run_singlethreaded(test)]
    async fn read_vmo() {
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
        let _child1_bool = child1.create_bool("property-bool", true);

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
        let _child2_bool = child2.create_bool("property-bool", false);

        let child3 = child1.create_child("child-1-1");
        let _child3_int = child3.create_int("property-int", -9);
        let bytes_data = (0u8..=9u8).cycle().take(5000).collect::<Vec<u8>>();
        let _bytes_prop = child3.create_bytes("property-bytes", &bytes_data);

        let child3_uint_array = child3.create_uint_exponential_histogram(
            "property-uint-array",
            ExponentialHistogramParams {
                floor: 1,
                initial_step: 1,
                step_multiplier: 2,
                buckets: 4,
            },
        );
        for x in [1, 2, 3, 4].iter() {
            child3_uint_array.insert(*x);
        }

        let result = read_from_inspector(&inspector).await.unwrap();

        assert_inspect_tree!(result, root: {
            "int-root": 3i64,
            "property-double-array": double_array_data,
            "child-1": {
                "property-uint": 10u64,
                "property-double": -3.4,
                "property-bool": true,
                "property-string": string_data,
                "property-int-array": vec![
                    Bucket { floor: i64::MIN, upper: 1, count: 1 },
                    Bucket { floor: 1, upper: 3, count: 1 },
                    Bucket { floor: 3, upper: 5, count: 1 },
                    Bucket { floor: 5, upper: 7, count: 1 },
                    Bucket { floor: 7, upper: i64::MAX, count: 1 }
                ],
                "child-1-1": {
                    "property-int": -9i64,
                    "property-bytes": bytes_data,
                    "property-uint-array": vec![
                        Bucket { floor: 0, upper: 1, count: 0 },
                        Bucket { floor: 1, upper: 2, count: 1 },
                        Bucket { floor: 2, upper: 3, count: 1 },
                        Bucket { floor: 3, upper: 5, count: 2 },
                        Bucket { floor: 5, upper: 9, count: 0 },
                        Bucket { floor: 9, upper: u64::MAX, count: 0 },
                    ],
                }
            },
            "child-2": {
                "property-double": 5.8,
                "property-bool": false,
            }
        })
    }

    #[test]
    fn tombstone_reads() {
        let inspector = Inspector::new();
        let node1 = inspector.root().create_child("child1");
        let node2 = node1.create_child("child2");
        let node3 = node2.create_child("child3");
        let prop1 = node1.create_string("val", "test");
        let prop2 = node2.create_string("val", "test");
        let prop3 = node3.create_string("val", "test");

        assert_inspect_tree!(inspector,
            root: {
                child1: {
                    val: "test",
                    child2: {
                        val: "test",
                        child3: {
                            val: "test",
                        }
                    }
                }
            }
        );

        std::mem::drop(node3);
        assert_inspect_tree!(inspector,
            root: {
                child1: {
                    val: "test",
                    child2: {
                        val: "test",
                    }
                }
            }
        );

        std::mem::drop(node2);
        assert_inspect_tree!(inspector,
            root: {
                child1: {
                    val: "test",
                }
            }
        );

        // Recreate the nodes. Ensure that the old properties are not picked up.
        let node2 = node1.create_child("child2");
        let _node3 = node2.create_child("child3");
        assert_inspect_tree!(inspector,
            root: {
                child1: {
                    val: "test",
                    child2: {
                        child3: {}
                    }
                }
            }
        );

        // Delete out of order, leaving 3 dangling.
        std::mem::drop(node2);
        assert_inspect_tree!(inspector,
            root: {
                child1: {
                    val: "test",
                }
            }
        );

        std::mem::drop(node1);
        assert_inspect_tree!(inspector,
            root: {
            }
        );

        std::mem::drop(prop3);
        assert_inspect_tree!(inspector,
            root: {
            }
        );

        std::mem::drop(prop2);
        assert_inspect_tree!(inspector,
            root: {
            }
        );

        std::mem::drop(prop1);
        assert_inspect_tree!(inspector,
            root: {
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
        let vmo = inspector.vmo.unwrap();
        let snapshot = Snapshot::try_from(&*vmo).expect("getting snapshot");
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
        let hierarchy: NodeHierarchy = PartialNodeHierarchy::try_from(Snapshot::build(&buf))
            .expect("creating node hierarchy")
            .into();

        assert_eq!(
            hierarchy,
            NodeHierarchy::new(
                "root",
                vec![Property::String("property".to_string(), "\u{FFFD}ello world".to_string())],
                vec![],
            ),
        );
    }

    #[test]
    fn test_invalid_array_slots() -> Result<(), Error> {
        let inspector = Inspector::new();
        let root = inspector.root();
        let array = root.create_int_array("int-array", 3);

        let vmo = inspector.vmo.unwrap();
        let vmo_size = vmo.get_size()?;
        let mut buf = vec![0u8; vmo_size as usize];
        vmo.read(&mut buf[..], /*offset=*/ 0)?;

        // Mess up with the block slots by setting them to a too big number.
        let offset = utils::offset_for_index(array.block_index()) + 8;
        let mut payload =
            Payload(u64::from_le_bytes(*<&[u8; 8]>::try_from(&buf[offset..offset + 8])?));
        payload.set_array_slots_count(255);

        buf[offset..offset + 8].clone_from_slice(&payload.value().to_le_bytes()[..]);
        assert!(PartialNodeHierarchy::try_from(Snapshot::build(&buf)).is_err());

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn lazy_nodes() -> Result<(), Error> {
        let inspector = Inspector::new();
        inspector.root().record_int("int", 3);
        let child = inspector.root().create_child("child");
        child.record_double("double", 1.5);
        inspector.root().record_lazy_child("lazy", || {
            async move {
                let inspector = Inspector::new();
                inspector.root().record_uint("uint", 5);
                inspector.root().record_lazy_values("nested-lazy-values", || {
                    async move {
                        let inspector = Inspector::new();
                        inspector.root().record_string("string", "test");
                        let child = inspector.root().create_child("nested-lazy-child");
                        let array = child.create_int_array("array", 3);
                        array.set(0, 1);
                        child.record(array);
                        inspector.root().record(child);
                        Ok(inspector)
                    }
                    .boxed()
                });
                Ok(inspector)
            }
            .boxed()
        });

        inspector.root().record_lazy_values("lazy-values", || {
            async move {
                let inspector = Inspector::new();
                let child = inspector.root().create_child("lazy-child-1");
                child.record_string("test", "testing");
                inspector.root().record(child);
                inspector.root().record_uint("some-uint", 3);
                inspector.root().record_lazy_values("nested-lazy-values", || {
                    async move {
                        let inspector = Inspector::new();
                        inspector.root().record_int("lazy-int", -3);
                        let child = inspector.root().create_child("one-more-child");
                        child.record_double("lazy-double", 4.3);
                        inspector.root().record(child);
                        Ok(inspector)
                    }
                    .boxed()
                });
                inspector.root().record_lazy_child("nested-lazy-child", || {
                    async move {
                        let inspector = Inspector::new();
                        // This will go out of scope and is not recorded, so it shouldn't appear.
                        let _double = inspector.root().create_double("double", -1.2);
                        Ok(inspector)
                    }
                    .boxed()
                });
                Ok(inspector)
            }
            .boxed()
        });

        let hierarchy = read_from_inspector(&inspector).await?;
        assert_inspect_tree!(hierarchy, root: {
            int: 3i64,
            child: {
                double: 1.5,
            },
            lazy: {
                uint: 5u64,
                string: "test",
                "nested-lazy-child": {
                    array: vec![1i64, 0, 0],
                }
            },
            "some-uint": 3u64,
            "lazy-child-1": {
                test: "testing",
            },
            "lazy-int": -3i64,
            "one-more-child": {
                "lazy-double": 4.3,
            },
            "nested-lazy-child": {
            }
        });

        Ok(())
    }

    #[test]
    fn test_matching_with_inspector() {
        let inspector = Inspector::new();
        assert_data_tree!(inspector, root: {});
    }

    #[test]
    fn test_matching_with_partial() {
        let propreties = vec![Property::String("sub".to_string(), "sub_value".to_string())];
        let partial = PartialNodeHierarchy::new("root", propreties, vec![]);
        assert_data_tree!(partial, root: {
            sub: "sub_value",
        });
    }

    #[test]
    #[should_panic]
    fn test_missing_values_with_partial() {
        let mut partial = PartialNodeHierarchy::new("root", vec![], vec![]);
        partial.links = vec![LinkValue {
            name: "missing-link".to_string(),
            content: "missing-link-404".to_string(),
            disposition: LinkNodeDisposition::Child,
        }];
        assert_data_tree!(partial, root: {});
    }

    #[test]
    fn test_matching_with_expression_as_key() {
        let properties = vec![Property::String("sub".to_string(), "sub_value".to_string())];
        let partial = PartialNodeHierarchy::new("root", properties, vec![]);
        let value = || "sub_value";
        let key = || "sub".to_string();
        assert_data_tree!(partial, root: {
            key() => value(),
        });
    }
}
