// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fuchsia_inspect::{
        self,
        format::{
            block::{Block, PropertyFormat},
            block_type::BlockType,
        },
    },
    serde_derive::Serialize,
    std::{self, collections::HashMap},
};

// Blocks such as Node, Extent, and Name may or may not be part of the Inspect tree. We want to
// count each case separately. Also, Extent blocks can't be fully analyzed when first scanned,
// since they don't store their own data size. So there's a three-step process to gather metrics.
// All these take place while Scanner is reading the VMO:
//
// 1) Analyze each block in the VMO with Metrics::analyze().
// 2) While building the tree,
//   2A) set the data size for Extent blocks;
//   2B) record the metrics for all blocks in the tree.
// 3) Record metrics as "NotUsed" for all remaining blocks, first setting their data size to 0.
//
// This can be combined for blocks that are never part of the tree, like Free and Reserved blocks.

// How many bytes are used to store a single number (same for i, u, and f, defined in the VMO spec)
const NUMERIC_TYPE_SIZE: usize = 8;

// Metrics for an individual block - will be remembered alongside the block's data by the Scanner.
#[derive(Debug)]
pub struct BlockMetrics {
    description: String,
    header_bytes: usize,
    data_bytes: usize,
    total_bytes: usize,
}

impl BlockMetrics {
    pub fn set_data_bytes(&mut self, bytes: usize) {
        self.data_bytes = bytes;
    }

    #[cfg(test)]
    pub(crate) fn sample_for_test(
        description: String,
        header_bytes: usize,
        data_bytes: usize,
        total_bytes: usize,
    ) -> BlockMetrics {
        BlockMetrics { description, header_bytes, data_bytes, total_bytes }
    }
}

// Tells whether the block was used in the Inspect tree or not.
#[derive(PartialEq)]
pub(crate) enum BlockStatus {
    Used,
    NotUsed,
}

// Gathers statistics for a type of block.
#[derive(Debug, Serialize, PartialEq)]
pub struct BlockStatistics {
    pub count: u64,
    pub header_bytes: usize,
    pub data_bytes: usize,
    pub total_bytes: usize,
    pub data_percent: u64,
}

impl BlockStatistics {
    fn new() -> BlockStatistics {
        BlockStatistics {
            count: 0,
            header_bytes: 0,
            data_bytes: 0,
            total_bytes: 0,
            data_percent: 0,
        }
    }

    fn update(&mut self, numbers: &BlockMetrics, status: BlockStatus) {
        let BlockMetrics { header_bytes, data_bytes, total_bytes, .. } = numbers;
        self.header_bytes += header_bytes;
        if status == BlockStatus::Used {
            self.data_bytes += data_bytes;
        }
        self.total_bytes += total_bytes;
        if self.total_bytes > 0 {
            self.data_percent = (self.data_bytes * 100 / self.total_bytes) as u64;
        }
    }
}

// Stores statistics for every type (description) of block, plus VMO as a whole.
#[derive(Debug, Serialize)]
pub struct Metrics {
    pub block_count: u64,
    pub size: usize,
    pub block_statistics: HashMap<String, BlockStatistics>,
}

trait Description {
    fn description(&self) -> Result<String, Error>;
}

// Describes the block, distinguishing array and histogram types.
impl Description for Block<&[u8]> {
    fn description(&self) -> Result<String, Error> {
        match self.block_type_or()? {
            BlockType::ArrayValue => {
                Ok(format!("ARRAY({:?}, {})", self.array_format()?, self.array_entry_type()?))
            }
            BlockType::PropertyValue => match self.property_format()? {
                PropertyFormat::String => Ok("STRING".to_owned()),
                PropertyFormat::Bytes => Ok("BYTES".to_owned()),
            },
            _ => Ok(format!("{}", self.block_type_or()?)),
        }
    }
}

impl Metrics {
    pub fn new() -> Metrics {
        Metrics { block_count: 0, size: 0, block_statistics: HashMap::new() }
    }

    pub(crate) fn record(&mut self, metrics: &BlockMetrics, status: BlockStatus) {
        let description = match status {
            BlockStatus::NotUsed => format!("{}(UNUSED)", metrics.description),
            BlockStatus::Used => metrics.description.clone(),
        };
        let statistics =
            self.block_statistics.entry(description).or_insert_with(|| BlockStatistics::new());
        statistics.count += 1;
        statistics.update(metrics, status);
        self.block_count += 1;
        self.size += metrics.total_bytes;
    }

    // Process (in a single operation) a block of a type that will never be part of the Inspect
    // data tree.
    pub fn process(&mut self, block: Block<&[u8]>) -> Result<(), Error> {
        self.record(&Metrics::analyze(block)?, BlockStatus::Used);
        Ok(())
    }

    pub fn analyze(block: Block<&[u8]>) -> Result<BlockMetrics, Error> {
        let description = block.description()?;
        let block_type = block.block_type_or()?;

        let data_bytes = match block_type {
            BlockType::Header
            | BlockType::Reserved
            | BlockType::NodeValue
            | BlockType::Free
            | BlockType::Extent
            | BlockType::PropertyValue
            | BlockType::Tombstone
            | BlockType::LinkValue => 0,
            BlockType::IntValue | BlockType::UintValue | BlockType::DoubleValue => {
                NUMERIC_TYPE_SIZE
            }

            BlockType::ArrayValue => NUMERIC_TYPE_SIZE * block.array_slots()?,
            BlockType::Name => block.name_length()?,
        };

        let header_bytes = match block_type {
            BlockType::Header
            | BlockType::NodeValue
            | BlockType::PropertyValue
            | BlockType::Free
            | BlockType::Reserved
            | BlockType::Tombstone
            | BlockType::ArrayValue
            | BlockType::LinkValue => 16,
            BlockType::IntValue
            | BlockType::DoubleValue
            | BlockType::UintValue
            | BlockType::Name
            | BlockType::Extent => 8,
        };

        let total_bytes = 16 << block.order();
        Ok(BlockMetrics { description, data_bytes, header_bytes, total_bytes })
    }
}

#[cfg(test)]
mod tests {
    use num_traits::ToPrimitive;
    use std::convert::TryFrom;
    use {
        super::*,
        crate::{data, puppet, results::Results},
        anyhow::{bail, format_err},
        fuchsia_async as fasync,
        fuchsia_inspect::format::{
            bitfields::{BlockHeader, Payload},
            block::{ArrayFormat, PropertyFormat},
            block_type::BlockType,
            constants,
        },
    };

    #[fasync::run_singlethreaded(test)]
    async fn metrics_work() -> Result<(), Error> {
        let puppet = puppet::tests::local_incomplete_puppet().await?;
        let metrics = puppet.metrics().unwrap();
        let mut results = Results::new();
        results.remember_metrics(metrics, "trialfoo", 42, "stepfoo");
        let json = results.to_json();
        assert!(json.contains("\"trial_name\":\"trialfoo\""), json);
        assert!(json.contains(&format!("\"size\":{}", puppet::VMO_SIZE)), json);
        assert!(json.contains("\"step_index\":42"), json);
        assert!(json.contains("\"step_name\":\"stepfoo\""), json);
        assert!(json.contains("\"block_count\":9"), json);
        assert!(json.contains("\"HEADER\":{\"count\":1,\"header_bytes\":16,\"data_bytes\":0,\"total_bytes\":16,\"data_percent\":0}"), json);
        Ok(())
    }

    fn test_metrics(
        buffer: &[u8],
        block_count: u64,
        size: usize,
        description: &str,
        count: u64,
        header_bytes: usize,
        data_bytes: usize,
        total_bytes: usize,
        data_percent: u64,
    ) -> Result<(), Error> {
        let metrics = data::Scanner::try_from(buffer).map(|d| d.metrics())?;
        assert_eq!(metrics.block_count, block_count, "Bad block_count for {}", description);
        assert_eq!(metrics.size, size, "Bad size for {}", description);
        let correct_statistics =
            BlockStatistics { count, header_bytes, data_bytes, total_bytes, data_percent };
        match metrics.block_statistics.get(description) {
            None => {
                return Err(format_err!(
                    "block {} not found in {:?}",
                    description,
                    metrics.block_statistics.keys()
                ))
            }
            Some(statistics) if statistics == &correct_statistics => {}
            Some(unexpected) => bail!(
                "Value mismatch, {:?} vs {:?} for {}",
                unexpected,
                correct_statistics,
                description
            ),
        }
        Ok(())
    }

    fn copy_into(source: &[u8], dest: &mut [u8], index: usize, offset: usize) {
        let offset = index * 16 + offset;
        dest[offset..offset + source.len()].copy_from_slice(source);
    }

    macro_rules! enum_value {
        ($name:expr) => {
            $name.to_u8().unwrap()
        };
    }
    macro_rules! put_header {
        ($header:ident, $index:expr, $buffer:expr) => {
            copy_into(&$header.value().to_le_bytes(), $buffer, $index, 0);
        };
    }
    macro_rules! put_payload {
        ($header:ident, $index:expr, $buffer:expr) => {
            copy_into(&$header.value().to_le_bytes(), $buffer, $index, 8);
        };
    }
    macro_rules! set_type {
        ($header:ident, $block_type:ident) => {
            $header.set_block_type(enum_value!(BlockType::$block_type))
        };
    }

    const NAME_INDEX: u32 = 2;

    // Creates the required Header block. Also creates a Name block because
    // lots of things use it.
    // Note that \0 is a valid UTF-8 character so there's no need to set string data.
    fn init_vmo_contents(mut buffer: &mut [u8]) {
        const HEADER_INDEX: usize = 0;

        let mut header = BlockHeader(0);
        header.set_order(0);
        set_type!(header, Header);
        header.set_header_magic(constants::HEADER_MAGIC_NUMBER);
        header.set_header_version(constants::HEADER_VERSION_NUMBER);
        put_header!(header, HEADER_INDEX, &mut buffer);
        let mut name_header = BlockHeader(0);
        set_type!(name_header, Name);
        name_header.set_name_length(4);
        put_header!(name_header, NAME_INDEX as usize, &mut buffer);
    }

    #[test]
    fn header_metrics() -> Result<(), Error> {
        let mut buffer = [0u8; 256];
        init_vmo_contents(&mut buffer);
        test_metrics(&buffer, 16, 256, "HEADER", 1, 16, 0, 16, 0)?;
        test_metrics(&buffer, 16, 256, "FREE", 14, 224, 0, 224, 0)?;
        test_metrics(&buffer, 16, 256, "NAME(UNUSED)", 1, 8, 0, 16, 0)?;
        Ok(())
    }

    #[test]
    fn reserved_metrics() -> Result<(), Error> {
        let mut buffer = [0u8; 256];
        init_vmo_contents(&mut buffer);
        let mut reserved_header = BlockHeader(0);
        set_type!(reserved_header, Reserved);
        reserved_header.set_order(1);
        put_header!(reserved_header, 1, &mut buffer);
        test_metrics(&buffer, 15, 256, "RESERVED", 1, 16, 0, 32, 0)?;
        Ok(())
    }

    #[test]
    fn node_metrics() -> Result<(), Error> {
        let mut buffer = [0u8; 256];
        init_vmo_contents(&mut buffer);
        let mut node_header = BlockHeader(0);
        set_type!(node_header, NodeValue);
        node_header.set_value_parent_index(1);
        put_header!(node_header, 1, &mut buffer);
        test_metrics(&buffer, 16, 256, "NODE_VALUE(UNUSED)", 1, 16, 0, 16, 0)?;
        node_header.set_value_name_index(NAME_INDEX);
        node_header.set_value_parent_index(0);
        put_header!(node_header, 1, &mut buffer);
        test_metrics(&buffer, 16, 256, "NODE_VALUE", 1, 16, 0, 16, 0)?;
        test_metrics(&buffer, 16, 256, "NAME", 1, 8, 4, 16, 25)?;
        set_type!(node_header, Tombstone);
        put_header!(node_header, 1, &mut buffer);
        test_metrics(&buffer, 16, 256, "TOMBSTONE", 1, 16, 0, 16, 0)?;
        test_metrics(&buffer, 16, 256, "NAME(UNUSED)", 1, 8, 0, 16, 0)?;
        Ok(())
    }

    #[test]
    fn number_metrics() -> Result<(), Error> {
        let mut buffer = [0u8; 256];
        init_vmo_contents(&mut buffer);
        macro_rules! test_number {
            ($number_type:ident, $parent:expr, $block_string:expr, $data_size:expr, $data_percent:expr) => {
                let mut value_header = BlockHeader(0);
                set_type!(value_header, $number_type);
                value_header.set_value_name_index(NAME_INDEX);
                value_header.set_value_parent_index($parent);
                put_header!(value_header, 1, &mut buffer);
                test_metrics(&buffer, 16, 256, $block_string, 1, 8, $data_size, 16, $data_percent)?;
            };
        }
        test_number!(IntValue, 0, "INT_VALUE", 8, 50);
        test_number!(IntValue, 5, "INT_VALUE(UNUSED)", 0, 0);
        test_number!(DoubleValue, 0, "DOUBLE_VALUE", 8, 50);
        test_number!(DoubleValue, 5, "DOUBLE_VALUE(UNUSED)", 0, 0);
        test_number!(UintValue, 0, "UINT_VALUE", 8, 50);
        test_number!(UintValue, 5, "UINT_VALUE(UNUSED)", 0, 0);
        Ok(())
    }

    #[test]
    fn property_metrics() -> Result<(), Error> {
        let mut buffer = [0u8; 256];
        init_vmo_contents(&mut buffer);
        let mut value_header = BlockHeader(0);
        set_type!(value_header, PropertyValue);
        value_header.set_value_name_index(NAME_INDEX);
        value_header.set_value_parent_index(0);
        put_header!(value_header, 1, &mut buffer);
        let mut property_payload = Payload(0);
        property_payload.set_property_total_length(12);
        property_payload.set_property_extent_index(4);
        property_payload.set_property_flags(enum_value!(PropertyFormat::String));
        put_payload!(property_payload, 1, &mut buffer);
        let mut extent_header = BlockHeader(0);
        set_type!(extent_header, Extent);
        extent_header.set_extent_next_index(5);
        put_header!(extent_header, 4, &mut buffer);
        extent_header.set_extent_next_index(0);
        put_header!(extent_header, 5, &mut buffer);
        test_metrics(&buffer, 16, 256, "EXTENT", 2, 16, 12, 32, 37)?;
        test_metrics(&buffer, 16, 256, "STRING", 1, 16, 0, 16, 0)?;
        property_payload.set_property_flags(enum_value!(PropertyFormat::Bytes));
        put_payload!(property_payload, 1, &mut buffer);
        test_metrics(&buffer, 16, 256, "EXTENT", 2, 16, 12, 32, 37)?;
        test_metrics(&buffer, 16, 256, "BYTES", 1, 16, 0, 16, 0)?;
        value_header.set_value_parent_index(7);
        put_header!(value_header, 1, &mut buffer);
        test_metrics(&buffer, 16, 256, "EXTENT(UNUSED)", 2, 16, 0, 32, 0)?;
        test_metrics(&buffer, 16, 256, "BYTES(UNUSED)", 1, 16, 0, 16, 0)?;
        Ok(())
    }

    #[test]
    fn array_metrics() -> Result<(), Error> {
        let mut buffer = [0u8; 256];
        init_vmo_contents(&mut buffer);
        let mut value_header = BlockHeader(0);
        set_type!(value_header, ArrayValue);
        value_header.set_order(3);
        value_header.set_value_name_index(NAME_INDEX);
        value_header.set_value_parent_index(0);
        put_header!(value_header, 4, &mut buffer);
        let mut property_payload = Payload(0);
        property_payload.set_array_entry_type(enum_value!(BlockType::IntValue));
        property_payload.set_array_flags(enum_value!(ArrayFormat::Default));
        property_payload.set_array_slots_count(4);
        put_payload!(property_payload, 4, &mut buffer);
        test_metrics(&buffer, 9, 256, "ARRAY(Default, INT_VALUE)", 1, 16, 32, 128, 25)?;
        property_payload.set_array_flags(enum_value!(ArrayFormat::LinearHistogram));
        property_payload.set_array_slots_count(8);
        put_payload!(property_payload, 4, &mut buffer);
        test_metrics(&buffer, 9, 256, "ARRAY(LinearHistogram, INT_VALUE)", 1, 16, 64, 128, 50)?;
        property_payload.set_array_flags(enum_value!(ArrayFormat::ExponentialHistogram));
        // avoid line-wrapping the parameter list of test_metrics()
        let name = "ARRAY(ExponentialHistogram, INT_VALUE)";
        put_payload!(property_payload, 4, &mut buffer);
        test_metrics(&buffer, 9, 256, name, 1, 16, 64, 128, 50)?;
        property_payload.set_array_entry_type(enum_value!(BlockType::UintValue));
        let name = "ARRAY(ExponentialHistogram, UINT_VALUE)";
        put_payload!(property_payload, 4, &mut buffer);
        test_metrics(&buffer, 9, 256, name, 1, 16, 64, 128, 50)?;
        property_payload.set_array_entry_type(enum_value!(BlockType::DoubleValue));
        let name = "ARRAY(ExponentialHistogram, DOUBLE_VALUE)";
        put_payload!(property_payload, 4, &mut buffer);
        test_metrics(&buffer, 9, 256, name, 1, 16, 64, 128, 50)?;
        value_header.set_value_parent_index(1);
        let name = "ARRAY(ExponentialHistogram, DOUBLE_VALUE)(UNUSED)";
        put_header!(value_header, 4, &mut buffer);
        test_metrics(&buffer, 9, 256, name, 1, 16, 0, 128, 0)?;
        Ok(())
    }
}
