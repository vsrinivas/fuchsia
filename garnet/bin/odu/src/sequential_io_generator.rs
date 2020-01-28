// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! SequentialIoGenerator generates sequential IO an a file.
//! IOs will be of different size. Every next IO will start where previous one
//! end in a non overlapping way. When generator reaches end of allowed range,
//! the next offset will wrap around and starts from the beginning of the range.

use {
    crate::generator::Generator,
    crate::operations::OperationType,
    byteorder::{LittleEndian, ReadBytesExt, WriteBytesExt},
    std::{io::Cursor, mem, ops::Range},
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
    // See struct Header
    magic_number: u64,

    // See struct Header
    process_id: u64,

    // See struct Header
    fd_unique_id: u64,

    // See struct Header
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

    // If true aligns the ios to block_size
    align: bool,
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
        align: bool,
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
            align,
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
    /// If so this routine write to each such block start.
    /// Buffers may very in size and offset. Not all buffers may be large enough
    /// to hold the header and not all buffers may overlap with beginning of the
    /// block.
    /// TODO(auradkar): SInce current IOs are always 4KiB aligned, this function
    /// works well. But not all unaligned cases are covered by this function.
    fn write_headers(&self, buf: &mut Vec<u8>, sequence_number: u64, offset_range: Range<u64>) {
        let start = round_up(offset_range.start as usize, self.block_size as usize);
        let end = offset_range.end as usize;
        let header_size = mem::size_of::<Header>();
        let mut offset = start;

        while offset + header_size < end {
            let header = Header::new(
                self.magic_number,
                self.process_id,
                self.fd_unique_id,
                self.generator_unique_id as u64,
                sequence_number,
                offset as u64,
                buf.capacity() as u64,
                self.last_number,
            );

            let buf_offset = offset - start;
            header.write_header(&mut buf[buf_offset..(buf_offset + header_size)]);

            offset += self.block_size as usize;
        }
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

        if !self.align {
            panic!("unaligned write not implemented yet");
        }

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

    fn fill_buffer(&self, buf: &mut Vec<u8>, sequence_number: u64, offset_range: Range<u64>) {
        self.zero_fill(buf);
        self.write_headers(buf, sequence_number, offset_range);
    }
}

/// Each block of written data contains a header field. This field helps us to
/// verify the written data. In future this also acts as poison value to detect
/// any corruptions. TODO(auradkar): This needs a better home.
#[derive(Default)]
struct Header {
    /// magic_number helps to identify that the block was written
    /// by the app
    magic_number: u64,

    /// process_id helps to differentiate this run from other runs
    process_id: u64,

    /// fd_unique_id tells what file descriptor was used to write
    /// this data
    fd_unique_id: u64,

    /// generator_unique_id is unique id of the generator that
    /// updated this block
    generator_unique_id: u64,

    /// io_op_unique_id tells which io operation updated this block
    io_op_unique_id: u64,

    // file_offset is offset within the file where this data
    // should be found
    file_offset: u64,

    /// size of the IO that wrote this header
    size: u64,

    /// seed that was used to generate the rest of the data in this
    /// block
    seed: u64,

    /// Stores the crc32 of this header
    crc32: u32,
}

impl Header {
    fn new(
        magic_number: u64,
        process_id: u64,
        fd_unique_id: u64,
        generator_unique_id: u64,
        io_op_unique_id: u64,
        file_offset: u64,
        size: u64,
        seed: u64,
    ) -> Header {
        let header = Header {
            magic_number,
            process_id,
            fd_unique_id,
            generator_unique_id,
            io_op_unique_id,
            file_offset,
            size,
            seed,
            crc32: 0,
        };

        // TODO(auradkar): compute crc32 or some equivalent. Couldn't find a
        // crate that does compute 64/32-bit checksum in the repo.
        header
    }

    /// Convert byte vector to header
    #[allow(dead_code)]
    pub fn read_header(buf: &[u8]) -> Header {
        let mut cursor = Cursor::new(buf);
        let mut header: Header = Default::default();

        header.magic_number = cursor.read_u64::<LittleEndian>().unwrap();

        header.process_id = cursor.read_u64::<LittleEndian>().unwrap();

        header.fd_unique_id = cursor.read_u64::<LittleEndian>().unwrap();

        header.generator_unique_id = cursor.read_u64::<LittleEndian>().unwrap();

        header.io_op_unique_id = cursor.read_u64::<LittleEndian>().unwrap();

        header.file_offset = cursor.read_u64::<LittleEndian>().unwrap();

        header.size = cursor.read_u64::<LittleEndian>().unwrap();

        header.seed = cursor.read_u64::<LittleEndian>().unwrap();

        header.crc32 = cursor.read_u32::<LittleEndian>().unwrap();

        header
    }

    /// Copy header into byte vector
    fn write_header(&self, buf: &mut [u8]) {
        let mut cursor = Cursor::new(buf);

        cursor.write_u64::<LittleEndian>(self.magic_number).unwrap();

        cursor.write_u64::<LittleEndian>(self.process_id).unwrap();

        cursor.write_u64::<LittleEndian>(self.fd_unique_id).unwrap();

        cursor.write_u64::<LittleEndian>(self.generator_unique_id).unwrap();

        cursor.write_u64::<LittleEndian>(self.io_op_unique_id).unwrap();

        cursor.write_u64::<LittleEndian>(self.file_offset).unwrap();

        cursor.write_u64::<LittleEndian>(self.size).unwrap();

        cursor.write_u64::<LittleEndian>(self.seed).unwrap();

        cursor.write_u32::<LittleEndian>(self.crc32).unwrap();
    }
}

#[cfg(test)]
mod tests {
    use {
        crate::generator::Generator,
        crate::operations::OperationType,
        crate::sequential_io_generator::{Header, SequentialIoGenerator},
        std::ops::Range,
    };

    static MAGIC_NUMBER: u64 = 100;
    static PROCESS_ID: u64 = 101;
    static TARGET_ID: u64 = 102;
    static GENERATOR_ID: u64 = 103;
    static ALIGN: bool = true;

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
            ALIGN,
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
        gen.fill_buffer(&mut buf, io_op_unique_id, io_offset_range.clone());
        let header: Header = Header::read_header(&buf);
        assert_eq!(header.magic_number, MAGIC_NUMBER);
        assert_eq!(header.process_id, PROCESS_ID);
        assert_eq!(header.fd_unique_id, TARGET_ID);
        assert_eq!(header.generator_unique_id, GENERATOR_ID);
        assert_eq!(header.io_op_unique_id, io_op_unique_id);
        assert_eq!(header.file_offset, io_offset_range.start);
        assert_eq!(header.size, io_offset_range.end - io_offset_range.start);
    }
}
