// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! SequentialIoGenerator generates sequential IO an a file.
//! IOs will be of different size. Every next IO will start where previous one
//! end in a non overlapping way. When generator reaches end of allowed range,
//! the next offset will wrap around and starts from the beginning of the range.

use {
    crate::generator::Generator, crate::io_header::IoHeader, crate::operations::OperationType,
    anyhow::Result, std::ops::Range,
};

enum FillType {
    ZeroFill,
}

/// Rounds up x to next multiple of align and returns the rounded up value.
pub fn round_up(x: usize, align: usize) -> usize {
    if align == 0 {
        x
    } else {
        ((x + align - 1) / align) * align
    }
}

pub struct SequentialIoGenerator {
    // See IoHeader
    magic_number: u64,

    // See IoHeader
    process_id: u64,

    // See IoHeader
    fd_unique_id: u64,

    // See IoHeader
    generator_unique_id: u64,

    // Range within which IO should be performed
    offset_range: Range<u64>,

    // This offset points location where next IO should be performed
    current_offset: u64,

    next_start_offset: u64,

    // last "random" number that was generated
    last_number: u64,

    // How to fill the buffer
    _fill_type: FillType,

    // block size suggest where to write headers in the buffer
    block_size: u64,

    max_io_size: u64,
}

impl SequentialIoGenerator {
    pub fn new(
        magic_number: u64,
        process_id: u64,
        fd_unique_id: u64,
        generator_unique_id: u64,
        offset_range: Range<u64>,
        block_size: u64,
        max_io_size: u64,
    ) -> SequentialIoGenerator {
        return SequentialIoGenerator {
            magic_number,
            process_id,
            fd_unique_id,
            generator_unique_id,
            offset_range: offset_range.clone(),
            current_offset: offset_range.start,
            last_number: 0,
            _fill_type: FillType::ZeroFill,
            block_size,
            max_io_size,
            next_start_offset: offset_range.start,
        };
    }

    /// We zero fill all buffers - because it was less programmer-time consuming.
    /// This is unnecessary and cpu time-consuming. TODO(auradkar): Introduce
    /// alternate ways.
    fn zero_fill(&self, buf: &mut Vec<u8>) {
        buf.resize(buf.capacity(), 0);
    }

    /// The function writes block header(s) to the buffer. We write block_header
    /// the beginning of each block. Some buffer may range over multiple blocks.
    /// If so this routine writes to each such block start.
    /// Buffers may very in size and offset. Not all buffers may be large enough
    /// to hold the header and not all buffers may overlap with beginning of the
    /// block.
    /// TODO(auradkar): Since current IOs are always 4KiB aligned, this function
    /// works well. Not all unaligned cases are covered by this function.
    fn write_headers(
        &self,
        buf: &mut Vec<u8>,
        sequence_number: u64,
        offset_range: Range<u64>,
    ) -> Result<()> {
        let start = round_up(offset_range.start as usize, self.block_size as usize);
        let end = offset_range.end as usize;
        let header_size = IoHeader::size() as usize;
        let mut offset = start;

        while offset + header_size < end {
            let header = IoHeader {
                magic_number: self.magic_number,
                process_id: self.process_id,
                fd_unique_id: self.fd_unique_id,
                generator_unique_id: self.generator_unique_id,
                io_op_unique_id: sequence_number,
                file_offset: offset as u64,
                size: buf.capacity() as u64,
                seed: self.last_number,
            };

            let buf_offset = offset - start;
            header.write_header(&mut buf[buf_offset..(buf_offset + header_size)])?;

            offset += self.block_size as usize;
        }
        Ok(())
    }

    fn io_size(&self) -> u64 {
        let max_blocks = self.max_io_size / self.block_size;
        let size = ((self.last_number % max_blocks as u64) + 1) * self.block_size;
        size
    }
}

impl Generator for SequentialIoGenerator {
    fn generate_number(&mut self) -> u64 {
        // NOTE: We don't generate number zero.
        self.last_number += 1;
        self.current_offset = self.next_start_offset;

        let size = self.io_size();
        self.next_start_offset += size;
        if self.next_start_offset >= self.offset_range.end {
            self.next_start_offset = self.offset_range.start
        }

        self.last_number
    }

    fn get_io_operation(&self, allowed_ops: &Vec<OperationType>) -> OperationType {
        let index = self.last_number as usize % allowed_ops.len();
        allowed_ops[index]
    }

    fn get_io_range(&self) -> Range<u64> {
        let cur = self.current_offset;

        let size = self.io_size();
        let mut end = cur + size;

        if end >= self.offset_range.end {
            end = self.offset_range.end;
        }

        cur..end
    }

    fn fill_buffer(
        &self,
        buf: &mut Vec<u8>,
        sequence_number: u64,
        offset_range: Range<u64>,
    ) -> Result<()> {
        self.zero_fill(buf);
        self.write_headers(buf, sequence_number, offset_range)
    }
}

#[cfg(test)]
mod tests {
    use {
        crate::generator::Generator, crate::io_header::IoHeader, crate::operations::OperationType,
        crate::sequential_io_generator::SequentialIoGenerator, std::ops::Range,
    };

    static MAGIC_NUMBER: u64 = 100;
    static PROCESS_ID: u64 = 101;
    static TARGET_ID: u64 = 102;
    static GENERATOR_ID: u64 = 103;

    fn create_generator(
        block_size: u64,
        target_range: Range<u64>,
        max_io_size: u64,
    ) -> SequentialIoGenerator {
        SequentialIoGenerator::new(
            MAGIC_NUMBER,
            PROCESS_ID,
            TARGET_ID,
            GENERATOR_ID,
            target_range,
            block_size,
            max_io_size,
        )
    }

    // Test for one block per io.
    #[test]
    fn simple_test() {
        let block_size = 4096 as u64;
        let target_range = 0..2 * block_size;
        let mut gen = create_generator(block_size, target_range, block_size);

        assert_eq!(gen.generate_number(), 1);
        let operations_vec = vec![OperationType::Write];
        assert_eq!(gen.get_io_operation(&operations_vec), OperationType::Write);
        assert_eq!(gen.get_io_range(), 0..block_size);

        assert_eq!(gen.generate_number(), 2);
        let operations_vec = vec![OperationType::Write];
        assert_eq!(gen.get_io_operation(&operations_vec), OperationType::Write);

        let operations_vec = vec![OperationType::Open, OperationType::Write];
        assert_eq!(gen.get_io_operation(&operations_vec), OperationType::Open);

        let operations_vec = vec![OperationType::Write, OperationType::Open];
        assert_eq!(gen.get_io_operation(&operations_vec), OperationType::Write);
        assert_eq!(gen.get_io_range(), block_size..2 * block_size);

        assert_eq!(gen.generate_number(), 3);
        let operations_vec = vec![OperationType::Write, OperationType::Open];
        assert_eq!(gen.get_io_operation(&operations_vec), OperationType::Open);
        assert_eq!(gen.get_io_range(), 0..block_size);
    }

    // Test for generating multiple blocks.
    #[test]
    fn multi_block_test() {
        let block_size = 4096 as u64;
        let target_range = 0..5 * block_size;
        let mut gen = create_generator(block_size, target_range.clone(), block_size * 4);

        assert_eq!(gen.generate_number(), 1);
        let mut start = 0;
        let mut end = (1 + 1) * block_size;
        assert_eq!(gen.get_io_range(), start..end);

        assert_eq!(gen.generate_number(), 2);
        start = end;
        end = target_range.end;
        assert_eq!(gen.get_io_range(), start..end);

        assert_eq!(gen.generate_number(), 3);
        start = 0;
        end = start + (3 + 1) * block_size;
        assert_eq!(gen.get_io_range(), start..end);

        assert_eq!(gen.generate_number(), 4);
        start = end;
        end = start + block_size;
        assert_eq!(gen.get_io_range(), start..end);
    }

    // Range test - Test if all the generated ios are withing given target range
    #[test]
    fn range_test() {
        let block_size = 4096 as u64;
        let target_range = 20 * block_size..50 * block_size;
        let max_io_size = 10 * block_size;
        let mut gen = create_generator(block_size, target_range.clone(), max_io_size);

        for _i in 0..2000 {
            gen.generate_number();
            let range = gen.get_io_range();
            assert_eq!(range.start >= target_range.start, true);
            assert_eq!((range.start + block_size) <= range.end, true);
            assert_eq!(range.end <= target_range.end, true);
            assert_eq!(range.start < target_range.end, true);
        }
    }

    // Size test - Test if the size of the IO falls withing the specified limit
    #[test]
    fn size_test() {
        let block_size = 4096 as u64;
        let target_range = 20 * block_size..50 * block_size;
        let max_io_size = 10 * block_size;
        let mut gen = create_generator(block_size, target_range, max_io_size);

        for _i in 0..2000 {
            gen.generate_number();
            let range = gen.get_io_range();
            assert_eq!((range.end - range.start) <= max_io_size, true);
            assert_eq!((range.end - range.start) > 0, true);
        }
    }

    // Read/write block header.
    #[test]
    fn header_test() {
        let block_size = 4096 as u64;
        let target_range = 20 * block_size..50 * block_size;
        let max_io_size = 10 * block_size;
        let gen = create_generator(block_size, target_range, max_io_size);

        let mut buf = vec![0 as u8; block_size as usize];
        let io_offset_range = block_size..2 * 4096;
        let io_op_unique_id = 10 as u64;
        gen.fill_buffer(&mut buf, io_op_unique_id, io_offset_range.clone())
            .expect("Header written successfully");
        let header = IoHeader::read_header(&buf).expect("Header read successfully");
        let expected_header = IoHeader {
            magic_number: MAGIC_NUMBER,
            process_id: PROCESS_ID,
            fd_unique_id: TARGET_ID,
            generator_unique_id: GENERATOR_ID,
            io_op_unique_id: io_op_unique_id,
            file_offset: io_offset_range.start,
            size: io_offset_range.end - io_offset_range.start,
            seed: 0,
        };

        assert_eq!(header, expected_header);
    }
}
