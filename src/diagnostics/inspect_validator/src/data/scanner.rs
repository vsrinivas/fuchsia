// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{validate::ROOT_ID, Data, LazyNode, Metrics, Node, Payload, Property, ROOT_NAME},
    crate::metrics::{BlockMetrics, BlockStatus},
    anyhow::{bail, format_err, Error},
    fuchsia_inspect::{
        self,
        format::{
            block::{ArrayFormat, Block, PropertyFormat},
            block_type::BlockType,
            constants::MIN_ORDER_SIZE,
        },
        reader as ireader,
    },
    fuchsia_inspect_node_hierarchy::LinkNodeDisposition,
    fuchsia_zircon::Vmo,
    std::{
        self,
        cmp::min,
        collections::{HashMap, HashSet},
        convert::TryFrom,
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
    final_nodes: HashMap<u32, Node>,
    final_properties: HashMap<u32, Property>,
    metrics: Metrics,
    child_trees: Option<HashMap<String, LazyNode>>,
}

impl TryFrom<&[u8]> for Scanner {
    type Error = Error;

    fn try_from(bytes: &[u8]) -> Result<Self, Self::Error> {
        let scanner = Scanner::new(None);
        scanner.scan(ireader::snapshot::Snapshot::try_from(bytes)?, bytes)
    }
}

impl TryFrom<&Vmo> for Scanner {
    type Error = Error;
    fn try_from(vmo: &Vmo) -> Result<Self, Self::Error> {
        let scanner = Scanner::new(None);
        scanner.scan(ireader::snapshot::Snapshot::try_from(vmo)?, &vmo_as_buffer(vmo)?)
    }
}

impl TryFrom<LazyNode> for Scanner {
    type Error = Error;

    fn try_from(mut vmo_tree: LazyNode) -> Result<Self, Self::Error> {
        let snapshot = ireader::snapshot::Snapshot::try_from(vmo_tree.vmo())?;
        let buffer = vmo_as_buffer(vmo_tree.vmo())?;
        let scanner = Scanner::new(vmo_tree.take_children());
        scanner.scan(snapshot, &buffer)
    }
}

fn vmo_as_buffer(vmo: &Vmo) -> Result<Vec<u8>, Error> {
    // NOTE: In any context except a controlled test, it's not safe to read the VMO manually -
    // the contents may differ or even be invalid (mid-update).
    let size = vmo.get_size()?;
    let mut buffer = vec![0u8; size as usize];
    vmo.read(&mut buffer[..], 0)?;
    Ok(buffer)
}

fn low_bits(number: u8, n_bits: usize) -> u8 {
    let n_bits = min(n_bits, 8);
    let mask = !(0xff_u16 << n_bits) as u8;
    number & mask
}

fn high_bits(number: u8, n_bits: usize) -> u8 {
    let n_bits = min(n_bits, 8);
    let mask = !(0xff_u16 >> n_bits) as u8;
    number & mask
}

const BITS_PER_BYTE: usize = 8;

/// Get size in bytes of a given |order|. Copied from private mod fuchsia-inspect/src/utils.rs
fn order_to_size(order: usize) -> usize {
    MIN_ORDER_SIZE << order
}

// Checks if these bits (start...end) are 0. Restricts the range checked to the given block.
fn check_zero_bits(
    buffer: &[u8],
    block: &Block<&[u8]>,
    start: usize,
    end: usize,
) -> Result<(), Error> {
    if end < start {
        return Err(format_err!("End must be >= start"));
    }
    let bits_in_block = order_to_size(block.order()) * BITS_PER_BYTE;
    if start > bits_in_block - 1 {
        return Ok(());
    }
    let end = min(end, bits_in_block - 1);
    let block_offset = block.index() as usize * MIN_ORDER_SIZE;
    let low_byte = start / BITS_PER_BYTE;
    let high_byte = end / BITS_PER_BYTE;
    let bottom_bits = high_bits(buffer[low_byte + block_offset], 8 - (start % 8));
    let top_bits = low_bits(buffer[high_byte + block_offset], (end % 8) + 1);
    if low_byte == high_byte {
        match bottom_bits & top_bits {
            0 => return Ok(()),
            nonzero => bail!(
                "Bits {}...{} of block type {} at {} have nonzero value {}",
                start,
                end,
                block.block_type_or()?,
                block.index(),
                nonzero
            ),
        }
    }
    if bottom_bits != 0 {
        bail!(
            "Non-zero value {} for bits {}.. of block type {} at {}",
            bottom_bits,
            start,
            block.block_type_or()?,
            block.index()
        );
    }
    if top_bits != 0 {
        bail!(
            "Non-zero value {} for bits ..{} of block type {} at {}",
            top_bits,
            end,
            block.block_type_or()?,
            block.index()
        );
    }
    for byte in low_byte + 1..high_byte {
        if buffer[byte + block_offset] != 0 {
            bail!(
                "Non-zero value {} for byte {} of block type {} at {}",
                buffer[byte],
                byte,
                block.block_type_or()?,
                block.index()
            );
        }
    }
    Ok(())
}

impl Scanner {
    fn new(child_trees: Option<HashMap<String, LazyNode>>) -> Scanner {
        let mut ret = Scanner {
            nodes: HashMap::new(),
            names: HashMap::new(),
            properties: HashMap::new(),
            extents: HashMap::new(),
            metrics: Metrics::new(),
            final_nodes: HashMap::new(),
            final_properties: HashMap::new(),
            child_trees,
        };
        // The ScannedNode at 0 is the "root" node. It exists to receive pointers to objects
        // whose parent is 0 while scanning the VMO.
        ret.nodes.insert(
            0,
            ScannedNode {
                validated: true,
                parent: 0,
                name: 0,
                children: HashSet::new(),
                properties: HashSet::new(),
                metrics: None,
            },
        );
        ret
    }

    fn scan(mut self, snapshot: ireader::snapshot::Snapshot, buffer: &[u8]) -> Result<Self, Error> {
        let mut link_blocks: Vec<Block<&[u8]>> = Vec::new();
        for block in snapshot.scan() {
            match block.block_type_or() {
                Ok(BlockType::Free) => self.process_free(block)?,
                Ok(BlockType::Reserved) => self.process_reserved(block)?,
                Ok(BlockType::Header) => self.process_header(block)?,
                Ok(BlockType::NodeValue) => self.process_node(block)?,
                Ok(BlockType::IntValue)
                | Ok(BlockType::UintValue)
                | Ok(BlockType::DoubleValue)
                | Ok(BlockType::ArrayValue)
                | Ok(BlockType::BufferValue)
                | Ok(BlockType::BoolValue) => self.process_property(block, buffer)?,
                Ok(BlockType::LinkValue) => link_blocks.push(block),
                Ok(BlockType::Extent) => self.process_extent(block, buffer)?,
                Ok(BlockType::Name) => self.process_name(block, buffer)?,
                Ok(BlockType::Tombstone) => self.process_tombstone(block)?,
                Err(error) => return Err(error),
            }
        }
        // We defer processing LINK blocks after because the population of the ScannedPayload::Link depends on all NAME blocks having been read.
        for block in link_blocks.into_iter() {
            self.process_property(block, buffer)?
        }

        let (mut new_nodes, mut new_properties) = self.make_valid_node_tree(ROOT_ID)?;
        for (node, id) in new_nodes.drain(..) {
            self.final_nodes.insert(id, node);
        }
        for (property, id) in new_properties.drain(..) {
            self.final_properties.insert(id, property);
        }
        self.record_unused_metrics();
        Ok(self)
    }

    pub fn data(self) -> Data {
        Data::build(self.final_nodes, self.final_properties)
    }

    pub fn metrics(self) -> Metrics {
        self.metrics
    }

    // ***** Utility functions
    fn record_unused_metrics(&mut self) {
        for (_, node) in self.nodes.drain() {
            if let Some(metrics) = node.metrics {
                self.metrics.record(&metrics, BlockStatus::NotUsed);
            }
        }
        for (_, name) in self.names.drain() {
            self.metrics.record(&name.metrics, BlockStatus::NotUsed);
        }
        for (_, property) in self.properties.drain() {
            self.metrics.record(&property.metrics, BlockStatus::NotUsed);
        }
        for (_, extent) in self.extents.drain() {
            self.metrics.record(&extent.metrics, BlockStatus::NotUsed);
        }
    }

    fn use_node(&mut self, node_id: u32) -> Result<ScannedNode, Error> {
        let mut node =
            self.nodes.remove(&node_id).ok_or(format_err!("No node at index {}", node_id))?;
        match node.metrics {
            None => {
                if node_id != 0 {
                    return Err(format_err!("Invalid node (no metrics) at index {}", node_id));
                }
            }
            Some(metrics) => {
                // I actually want as_deref() but that's nightly-only.
                self.metrics.record(&metrics, BlockStatus::Used);
                node.metrics = Some(metrics); // Put it back after I borrow it.
            }
        }
        Ok(node)
    }

    fn use_property(&mut self, property_id: u32) -> Result<ScannedProperty, Error> {
        let property = self
            .properties
            .remove(&property_id)
            .ok_or(format_err!("No property at index {}", property_id))?;
        self.metrics.record(&property.metrics, BlockStatus::Used);
        Ok(property)
    }

    fn use_owned_name(&mut self, name_id: u32) -> Result<String, Error> {
        let name =
            self.names.remove(&name_id).ok_or(format_err!("No string at index {}", name_id))?;
        self.metrics.record(&name.metrics, BlockStatus::Used);
        Ok(name.name.clone())
    }

    // ***** Functions which read fuchsia_inspect::format::block::Block (actual
    // ***** VMO blocks), validate them, turn them into Scanned* objects, and
    // ***** add the ones we care about to Self.

    // Some blocks' metrics can only be calculated in the context of a tree. Metrics aren't run
    // on those in the process_ functions, but rather while the tree is being built.

    // Note: process_ functions are only called from the scan() iterator on the
    // VMO's blocks, so indexes of the blocks themselves will never be duplicated; that's one
    // thing we don't have to verify.
    fn process_free(&mut self, block: Block<&[u8]>) -> Result<(), Error> {
        // TODO(fxbug.dev/39975): Uncomment or delete this line depending on the resolution of fxbug.dev/40012.
        // check_zero_bits(buffer, &block, 64, MAX_BLOCK_BITS)?;
        self.metrics.process(block)?;
        Ok(())
    }

    fn process_header(&mut self, block: Block<&[u8]>) -> Result<(), Error> {
        self.metrics.process(block)?;
        Ok(())
    }

    fn process_tombstone(&mut self, block: Block<&[u8]>) -> Result<(), Error> {
        self.metrics.process(block)?;
        Ok(())
    }

    fn process_reserved(&mut self, block: Block<&[u8]>) -> Result<(), Error> {
        self.metrics.process(block)?;
        Ok(())
    }

    fn process_extent(&mut self, block: Block<&[u8]>, buffer: &[u8]) -> Result<(), Error> {
        check_zero_bits(buffer, &block, 40, 63)?;
        self.extents.insert(
            block.index(),
            ScannedExtent {
                next: block.next_extent()?,
                data: block.extent_contents()?,
                metrics: Metrics::analyze(block)?,
            },
        );
        Ok(())
    }

    fn process_name(&mut self, block: Block<&[u8]>, buffer: &[u8]) -> Result<(), Error> {
        check_zero_bits(buffer, &block, 28, 63)?;
        self.names.insert(
            block.index(),
            ScannedName { name: block.name_contents()?, metrics: Metrics::analyze(block)? },
        );
        Ok(())
    }

    fn process_node(&mut self, block: Block<&[u8]>) -> Result<(), Error> {
        let parent = block.parent_index()?;
        let id = block.index();
        let name = block.name_index()?;
        let mut node;
        let metrics = Some(Metrics::analyze(block)?);
        if let Some(placeholder) = self.nodes.remove(&id) {
            // We need to preserve the children and properties.
            node = placeholder;
            node.validated = true;
            node.parent = parent;
            node.name = name;
            node.metrics = metrics;
        } else {
            node = ScannedNode {
                validated: true,
                name,
                parent,
                children: HashSet::new(),
                properties: HashSet::new(),
                metrics,
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
                    metrics: None,
                },
            );
        }
        if let Some(parent_node) = self.nodes.get_mut(&parent) {
            get_the_hashset(parent_node).insert(id);
        }
    }

    fn build_scanned_payload(
        &mut self,
        block: &Block<&[u8]>,
        block_type: BlockType,
    ) -> Result<ScannedPayload, Error> {
        Ok(match block_type {
            BlockType::IntValue => ScannedPayload::Int(block.int_value()?),
            BlockType::UintValue => ScannedPayload::Uint(block.uint_value()?),
            BlockType::DoubleValue => ScannedPayload::Double(block.double_value()?),
            BlockType::BoolValue => ScannedPayload::Bool(block.bool_value()?),
            BlockType::BufferValue => {
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
                        return Err(format_err!(
                            "No way I should see {:?} for ArrayEntryType",
                            illegal_type
                        ))
                    }
                }
            }
            BlockType::LinkValue => {
                let child_trees = self
                    .child_trees
                    .as_mut()
                    .ok_or(format_err!("LinkValue encountered without child tree."))?;
                let child_name = &self
                    .names
                    .get(&block.link_content_index()?)
                    .ok_or(format_err!(
                        "Child name not found for LinkValue block {}.",
                        block.index()
                    ))?
                    .name;
                let child_tree = child_trees.remove(child_name).ok_or(format_err!(
                    "Lazy node not found for LinkValue block {} with name {}.",
                    block.index(),
                    child_name
                ))?;
                ScannedPayload::Link {
                    disposition: block.link_node_disposition()?,
                    scanned_tree: Scanner::try_from(child_tree)?,
                }
            }
            illegal_type => {
                return Err(format_err!("No way I should see {:?} for BlockType", illegal_type))
            }
        })
    }

    fn process_property(&mut self, block: Block<&[u8]>, buffer: &[u8]) -> Result<(), Error> {
        if block.block_type_or()? == BlockType::ArrayValue {
            check_zero_bits(buffer, &block, 80, 127)?;
        }
        let id = block.index();
        let parent = block.parent_index()?;
        let block_type = block.block_type_or()?;
        let payload = self.build_scanned_payload(&block, block_type)?;
        let property = ScannedProperty {
            name: block.name_index()?,
            parent,
            payload,
            metrics: Metrics::analyze(block)?,
        };
        self.properties.insert(id, property);
        self.add_to_parent(parent, id, |node| &mut node.properties);
        Ok(())
    }

    // ***** Functions which convert Scanned* objects into Node and Property objects.

    fn make_valid_node_tree(
        &mut self,
        id: u32,
    ) -> Result<(Vec<(Node, u32)>, Vec<(Property, u32)>), Error> {
        let scanned_node = self.use_node(id)?;
        if !scanned_node.validated {
            return Err(format_err!("No node at {}", id));
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
            if id == 0 { ROOT_NAME.to_owned() } else { self.use_owned_name(scanned_node.name)? };
        let this_node = Node {
            name,
            parent: scanned_node.parent,
            children: scanned_node.children.clone(),
            properties: scanned_node.properties.clone(),
        };
        nodes_in_tree.push((this_node, id));
        Ok((nodes_in_tree, properties_under))
    }

    fn make_valid_property(&mut self, id: u32) -> Result<Property, Error> {
        let scanned_property = self.use_property(id)?;
        let name = self.use_owned_name(scanned_property.name)?;
        let payload = self.make_valid_payload(scanned_property.payload)?;
        Ok(Property { id, name, parent: scanned_property.parent, payload })
    }

    fn make_valid_payload(&mut self, payload: ScannedPayload) -> Result<Payload, Error> {
        Ok(match payload {
            ScannedPayload::Int(data) => Payload::Int(data),
            ScannedPayload::Uint(data) => Payload::Uint(data),
            ScannedPayload::Double(data) => Payload::Double(data),
            ScannedPayload::Bool(data) => Payload::Bool(data),
            ScannedPayload::IntArray(data, format) => Payload::IntArray(data, format),
            ScannedPayload::UintArray(data, format) => Payload::UintArray(data, format),
            ScannedPayload::DoubleArray(data, format) => Payload::DoubleArray(data, format),
            ScannedPayload::Bytes { length, link } => {
                Payload::Bytes(self.make_valid_vector(length, link)?)
            }
            ScannedPayload::String { length, link } => Payload::String(
                std::str::from_utf8(&self.make_valid_vector(length, link)?)?.to_owned(),
            ),
            ScannedPayload::Link { disposition, scanned_tree } => {
                Payload::Link { disposition, parsed_data: scanned_tree.data() }
            }
        })
    }

    fn make_valid_vector(&mut self, length: usize, link: u32) -> Result<Vec<u8>, Error> {
        let mut dest = vec![];
        let mut length_remaining = length;
        let mut next_link = link;
        while length_remaining > 0 {
            // This is effectively use_extent()
            let mut extent =
                self.extents.remove(&next_link).ok_or(format_err!("No extent at {}", next_link))?;
            let copy_len = min(extent.data.len(), length_remaining);
            extent.metrics.set_data_bytes(copy_len);
            self.metrics.record(&extent.metrics, BlockStatus::Used);
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
    metrics: Option<BlockMetrics>,
}

#[derive(Debug)]
struct ScannedProperty {
    name: u32,
    parent: u32,
    payload: ScannedPayload,
    metrics: BlockMetrics,
}

#[derive(Debug)]
struct ScannedName {
    name: String,
    metrics: BlockMetrics,
}

#[derive(Debug)]
struct ScannedExtent {
    next: u32,
    data: Vec<u8>,
    metrics: BlockMetrics,
}

#[derive(Debug)]
enum ScannedPayload {
    String { length: usize, link: u32 },
    Bytes { length: usize, link: u32 },
    Int(i64),
    Uint(u64),
    Double(f64),
    Bool(bool),
    IntArray(Vec<i64>, ArrayFormat),
    UintArray(Vec<u64>, ArrayFormat),
    DoubleArray(Vec<f64>, ArrayFormat),
    Link { disposition: LinkNodeDisposition, scanned_tree: Scanner },
}

#[cfg(test)]
mod tests {
    use num_traits::ToPrimitive;
    use {
        super::*,
        crate::*,
        fidl_test_inspect_validate::Number,
        fuchsia_async as fasync,
        fuchsia_inspect::format::{
            bitfields::{BlockHeader, Payload as BlockPayload},
            block_type::BlockType,
            constants,
        },
    };

    // TODO(fxbug.dev/39975): Depending on the resolution of fxbug.dev/40012, move this const out of mod test.
    const MAX_BLOCK_BITS: usize = constants::MAX_ORDER_SIZE * BITS_PER_BYTE;

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
        let actual = data::Scanner::try_from(buffer as &[u8]).map(|d| d.data().to_string());
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
                println!("Raw data: {:?}", data::Scanner::try_from(buffer as &[u8]))
            }
        }
        assert_eq!(predicted, actual.as_ref().ok().map(|s| &s[..]));
        buffer[location] = previous;
    }

    fn put_header(header: &BlockHeader, buffer: &mut [u8], index: usize) {
        copy_into(&header.value().to_le_bytes(), buffer, index * 16);
    }

    fn put_payload(payload: &BlockPayload, buffer: &mut [u8], index: usize) {
        copy_into(&payload.value().to_le_bytes(), buffer, index * 16 + 8);
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
        try_byte(&mut buffer, (16, 0), 0, Some("root ->\n> node ->"));
        // Mess up HEADER_MAGIC_NUMBER - it should fail to load.
        try_byte(&mut buffer, (HEADER, 7), 0, None);
        // Mess up node's parent; should disappear.
        try_byte(&mut buffer, (ROOT, 1), 1, Some("root ->"));
        // Mess up root's name; should fail.
        try_byte(&mut buffer, (ROOT, 5), 1, None);
        // Mess up generation count; should fail (and not hang).
        try_byte(&mut buffer, (HEADER, 8), 1, None);
        // But an even generation count should work.
        try_byte(&mut buffer, (HEADER, 8), 2, Some("root ->\n> node ->"));
        // Let's give it a property.
        const NUMBER: usize = 4;
        let mut number_header = BlockHeader(0);
        number_header.set_order(0);
        number_header.set_block_type(BlockType::IntValue.to_u8().unwrap());
        number_header.set_value_name_index(3);
        number_header.set_value_parent_index(1);
        put_header(&number_header, &mut buffer, NUMBER);
        const NUMBER_NAME: usize = 3;
        let mut header = BlockHeader(0);
        header.set_order(0);
        header.set_block_type(BlockType::Name.to_u8().unwrap());
        header.set_name_length(6);
        put_header(&header, &mut buffer, NUMBER_NAME);
        copy_into(b"number", &mut buffer, NUMBER_NAME * 16 + 8);
        try_byte(&mut buffer, (HEADER, 8), 2, Some("root ->\n> node ->\n> > number: Int(0)"));
        try_byte(&mut buffer, (NUMBER, 1), 5, Some("root ->\n> node ->\n> > number: Uint(0)"));
        try_byte(&mut buffer, (NUMBER, 1), 6, Some("root ->\n> node ->\n> > number: Double(0.0)"));
        try_byte(&mut buffer, (NUMBER, 1), 7, Some("root ->\n> node ->\n> > number: String(\"\")"));
        // Array block will have illegal Array Entry Type of 0.
        try_byte(&mut buffer, (NUMBER, 1), 0xb0, None);
        // 15 is an illegal block type.
        try_byte(&mut buffer, (NUMBER, 1), 0xf, None);
        number_header.set_order(2);
        number_header.set_block_type(BlockType::ArrayValue.to_u8().unwrap());
        put_header(&number_header, &mut buffer, NUMBER);
        // Array block again has illegal Array Entry Type of 0.
        try_byte(&mut buffer, (128, 0), 0, None);
        // 4, 5, and 6 are legal array types.
        try_byte(
            &mut buffer,
            (NUMBER, 8),
            0x04,
            Some("root ->\n> node ->\n> > number: IntArray([], Default)"),
        );
        try_byte(
            &mut buffer,
            (NUMBER, 8),
            0x05,
            Some("root ->\n> node ->\n> > number: UintArray([], Default)"),
        );
        try_byte(
            &mut buffer,
            (NUMBER, 8),
            0x06,
            Some("root ->\n> node ->\n> > number: DoubleArray([], Default)"),
        );
        // 0, 1, and 2 are legal formats.
        try_byte(
            &mut buffer,
            (NUMBER, 8),
            0x14,
            Some("root ->\n> node ->\n> > number: IntArray([], LinearHistogram)"),
        );
        try_byte(
            &mut buffer,
            (NUMBER, 8),
            0x24,
            Some("root ->\n> node ->\n> > number: IntArray([], ExponentialHistogram)"),
        );
        try_byte(&mut buffer, (NUMBER, 8), 0x34, None);
        // Let's make sure other Value block-type numbers are rejected.
        try_byte(&mut buffer, (NUMBER, 8), BlockType::ArrayValue.to_u8().unwrap(), None);
        buffer[NUMBER * 16 + 8] = 4; // Int, Default
        buffer[NUMBER * 16 + 9] = 2; // 2 entries
        try_byte(
            &mut buffer,
            (NUMBER, 16),
            42,
            Some("root ->\n> node ->\n> > number: IntArray([42, 0], Default)"),
        );
        try_byte(
            &mut buffer,
            (NUMBER, 24),
            42,
            Some("root ->\n> node ->\n> > number: IntArray([0, 42], Default)"),
        );
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
        assert_eq!(puppet1.read_data().await?.to_string(), puppet2.read_data().await?.to_string());
        puppet1.apply(&mut property1_action).await?;
        puppet1.apply(&mut property2_action).await?;
        puppet2.apply(&mut property2_action).await?;
        puppet2.apply(&mut property1_action).await?;
        assert_eq!(puppet1.read_data().await?.to_string(), puppet2.read_data().await?.to_string());
        // Make sure the tree distinguishes based on node position
        puppet1 = puppet::tests::local_incomplete_puppet().await?;
        puppet2 = puppet::tests::local_incomplete_puppet().await?;
        let mut subchild2_action = create_node!(parent:1, id:2, name:"child2");
        puppet1.apply(&mut child1_action).await?;
        puppet2.apply(&mut child1_action).await?;
        puppet1.apply(&mut child2_action).await?;
        puppet2.apply(&mut subchild2_action).await?;
        assert_ne!(puppet1.read_data().await?.to_string(), puppet2.read_data().await?.to_string());
        // ... and property position
        let mut subproperty2_action =
            create_numeric_property!(parent:1, id:2, name:"prop2", value: Number::IntT(1));
        puppet1.apply(&mut child1_action).await?;
        puppet2.apply(&mut child1_action).await?;
        puppet1.apply(&mut property2_action).await?;
        puppet2.apply(&mut subproperty2_action).await?;
        Ok(())
    }

    #[test]
    fn test_bit_ops() -> Result<(), Error> {
        assert_eq!(low_bits(0xff, 3), 7);
        assert_eq!(low_bits(0x04, 3), 4);
        assert_eq!(low_bits(0xf8, 3), 0);
        assert_eq!(low_bits(0xab, 99), 0xab);
        assert_eq!(low_bits(0xff, 0), 0);
        assert_eq!(high_bits(0xff, 3), 0xe0);
        assert_eq!(high_bits(0x20, 3), 0x20);
        assert_eq!(high_bits(0x1f, 3), 0);
        assert_eq!(high_bits(0xab, 99), 0xab);
        assert_eq!(high_bits(0xff, 0), 0);
        Ok(())
    }

    #[test]
    fn test_zero_bits() -> Result<(), Error> {
        let mut buffer = [0u8; 48];
        for byte in 0..16 {
            buffer[byte] = 0xff;
        }
        for byte in 32..48 {
            buffer[byte] = 0xff;
        }
        {
            let block = Block::new(&buffer[..], 1);
            assert!(check_zero_bits(&buffer, &block, 1, 0).is_err());
            assert!(check_zero_bits(&buffer, &block, 0, 0).is_ok());
            assert!(check_zero_bits(&buffer, &block, 0, MAX_BLOCK_BITS).is_ok());
        }
        // Don't mess with buffer[0]; that defines block size and type.
        // The block I'm testing (index 1) is in between two all-ones blocks.
        // Its bytes are thus 16..23 in the buffer.
        buffer[1 + 16] = 1;
        // Now bit 8 of the block is 1. Checking any range that includes bit 8 should give an
        // error (even single-bit 8...8). Other ranges should succeed.
        {
            let block = Block::new(&buffer[..], 1);
            assert!(check_zero_bits(&buffer, &block, 8, 8).is_err());
            assert!(check_zero_bits(&buffer, &block, 8, MAX_BLOCK_BITS).is_err());
            assert!(check_zero_bits(&buffer, &block, 9, MAX_BLOCK_BITS).is_ok());
        }
        buffer[2 + 16] = 0x80;
        // Now bits 8 and 23 are 1. The range 9...MAX_BLOCK_BITS that succeeded before should fail.
        // 9...22 and 24...MAX_BLOCK_BITS should succeed. So should 24...63.
        {
            let block = Block::new(&buffer[..], 1);
            assert!(check_zero_bits(&buffer, &block, 9, MAX_BLOCK_BITS).is_err());
            assert!(check_zero_bits(&buffer, &block, 9, 22).is_ok());
            assert!(check_zero_bits(&buffer, &block, 24, MAX_BLOCK_BITS).is_ok());
            assert!(check_zero_bits(&buffer, &block, 24, 63).is_ok());
        }
        buffer[2 + 16] = 0x20;
        // Now bits 8 and 21 are 1. This tests bit-checks in the middle of the byte.
        {
            let block = Block::new(&buffer[..], 1);
            assert!(check_zero_bits(&buffer, &block, 16, 20).is_ok());
            assert!(check_zero_bits(&buffer, &block, 21, 21).is_err());
            assert!(check_zero_bits(&buffer, &block, 22, 63).is_ok());
        }
        buffer[7 + 16] = 0x80;
        // Now bits 8, 21, and 63 are 1. Checking 22...63 should fail; 22...62 should succeed.
        {
            let block = Block::new(&buffer[..], 1);
            assert!(check_zero_bits(&buffer, &block, 22, 63).is_err());
            assert!(check_zero_bits(&buffer, &block, 22, 62).is_ok());
        }
        buffer[3 + 16] = 0x10;
        // Here I'm testing whether 1 bits in the bytes between the ends of the range are also
        // detected (cause the check to fail) (to make sure my loop doesn't have an off by 1 error).
        {
            let block = Block::new(&buffer[..], 1);
            assert!(check_zero_bits(&buffer, &block, 22, 62).is_err());
        }
        buffer[3 + 16] = 0;
        buffer[4 + 16] = 0x10;
        {
            let block = Block::new(&buffer[..], 1);
            assert!(check_zero_bits(&buffer, &block, 22, 62).is_err());
        }
        buffer[4 + 16] = 0;
        buffer[5 + 16] = 0x10;
        {
            let block = Block::new(&buffer[..], 1);
            assert!(check_zero_bits(&buffer, &block, 22, 62).is_err());
        }
        buffer[5 + 16] = 0;
        buffer[6 + 16] = 0x10;
        {
            let block = Block::new(&buffer[..], 1);
            assert!(check_zero_bits(&buffer, &block, 22, 62).is_err());
        }
        buffer[1 + 16] = 0x81;
        // Testing whether I can correctly ignore 1 bits within a single byte that are outside
        // the specified range, and detect 1 bits that are inside the range.
        {
            let block = Block::new(&buffer[..], 1);
            assert!(check_zero_bits(&buffer, &block, 9, 14).is_ok());
            assert!(check_zero_bits(&buffer, &block, 8, 14).is_err());
            assert!(check_zero_bits(&buffer, &block, 9, 15).is_err());
        }
        Ok(())
    }

    #[test]
    fn test_reserved_fields() {
        let mut buffer = [0u8; 4096];
        // VMO Header block (index 0)
        const HEADER: usize = 0;
        let mut header = BlockHeader(0);
        header.set_order(0);
        header.set_block_type(BlockType::Header.to_u8().unwrap());
        header.set_header_magic(constants::HEADER_MAGIC_NUMBER);
        header.set_header_version(constants::HEADER_VERSION_NUMBER);
        put_header(&header, &mut buffer, HEADER);
        const VALUE: usize = 1;
        let mut value_header = BlockHeader(0);
        value_header.set_order(0);
        value_header.set_block_type(BlockType::NodeValue.to_u8().unwrap());
        value_header.set_value_name_index(2);
        value_header.set_value_parent_index(0);
        put_header(&value_header, &mut buffer, VALUE);
        // Root's Name block
        const VALUE_NAME: usize = 2;
        let mut header = BlockHeader(0);
        header.set_order(0);
        header.set_block_type(BlockType::Name.to_u8().unwrap());
        header.set_name_length(5);
        put_header(&header, &mut buffer, VALUE_NAME);
        copy_into(b"value", &mut buffer, VALUE_NAME * 16 + 8);
        // Extent block (not linked into tree)
        const EXTENT: usize = 3;
        let mut header = BlockHeader(0);
        header.set_order(0);
        header.set_block_type(BlockType::Extent.to_u8().unwrap());
        header.set_extent_next_index(0);
        put_header(&header, &mut buffer, EXTENT);
        // Let's make sure it scans.
        try_byte(&mut buffer, (16, 0), 0, Some("root ->\n> value ->"));
        // Put garbage in a random FREE block body - should fail.
        // TODO(fxbug.dev/39975): Depending on the resolution of fxbug.dev/40012, uncomment or delete this test.
        //try_byte(&mut buffer, (6, 9), 42, None);
        // Put garbage in a random FREE block header - should be fine.
        try_byte(&mut buffer, (6, 7), 42, Some("root ->\n> value ->"));
        // Put garbage in NAME header - should fail.
        try_byte(&mut buffer, (VALUE_NAME, 7), 42, None);
        // Put garbage in EXTENT header - should fail.
        try_byte(&mut buffer, (EXTENT, 6), 42, None);
        value_header.set_block_type(BlockType::ArrayValue.to_u8().unwrap());
        put_header(&value_header, &mut buffer, VALUE);
        let mut array_subheader = BlockPayload(0);
        array_subheader.set_array_entry_type(BlockType::IntValue.to_u8().unwrap());
        array_subheader.set_array_flags(ArrayFormat::Default.to_u8().unwrap());
        put_payload(&array_subheader, &mut buffer, VALUE);
        try_byte(&mut buffer, (16, 0), 0, Some("root ->\n> value: IntArray([], Default)"));
        // Put garbage in reserved part of Array spec, should fail.
        try_byte(&mut buffer, (VALUE, 12), 42, None);
        value_header.set_block_type(BlockType::IntValue.to_u8().unwrap());
        put_header(&value_header, &mut buffer, VALUE);
        // Now the array spec is just a (large) value; it should succeed.
        try_byte(&mut buffer, (VALUE, 12), 42, Some("root ->\n> value: Int(180388626436)"));
    }
}
