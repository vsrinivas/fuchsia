// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::generator::Generator,
    crate::io_header::IoHeader,
    crate::operations::OperationType,
    anyhow::Result,
    rand::{Rng, RngCore},
    std::ops::Range,
};

/// Generates IO operations at random offsets within a file.
pub struct RandomIoGenerator {
    // See IoHeader.
    magic_number: u64,

    // See IoHeader.
    process_id: u64,

    // See IoHeader.
    fd_unique_id: u64,

    // See IoHeader.
    generator_unique_id: u64,

    // Range within which IO should be performed.
    offset_range: Range<u64>,

    // The size of IO operations to generate. Must be larger than size of the IoHeader.
    block_size: u64,

    // The last random number that was generated.
    last_number: u64,

    // Random number generator to generate where the next IO should happen.
    rng: Box<dyn RngCore>,
}

impl RandomIoGenerator {
    #[allow(dead_code)]
    pub fn new(
        magic_number: u64,
        process_id: u64,
        fd_unique_id: u64,
        generator_unique_id: u64,
        offset_range: Range<u64>,
        block_size: u64,
        rng: Box<dyn RngCore>,
    ) -> Self {
        return Self {
            magic_number,
            process_id,
            fd_unique_id,
            generator_unique_id,
            offset_range: offset_range.clone(),
            last_number: 0,
            block_size,
            rng,
        };
    }
}

impl Generator for RandomIoGenerator {
    fn generate_number(&mut self) -> u64 {
        self.last_number = self.rng.gen();
        self.last_number
    }

    fn get_io_operation(&self, allowed_ops: &Vec<OperationType>) -> OperationType {
        let index = self.last_number as usize % allowed_ops.len();
        allowed_ops[index]
    }

    fn get_io_range(&self) -> Range<u64> {
        let spanned_bytes = self.offset_range.end - self.offset_range.start;
        let spanned_blocks = spanned_bytes / self.block_size;
        let block = self.last_number % spanned_blocks;
        let start = self.offset_range.start + block * self.block_size;
        start..(start + self.block_size)
    }

    fn fill_buffer(
        &self,
        buf: &mut Vec<u8>,
        sequence_number: u64,
        offset_range: Range<u64>,
    ) -> Result<()> {
        let header = IoHeader {
            magic_number: self.magic_number,
            process_id: self.process_id,
            fd_unique_id: self.fd_unique_id,
            generator_unique_id: self.generator_unique_id,
            io_op_unique_id: sequence_number,
            file_offset: offset_range.start,
            size: self.block_size,
            seed: self.last_number,
        };
        header.write_header(buf)
    }
}

#[cfg(test)]
mod tests {
    use {
        crate::generator::Generator, crate::io_header::IoHeader, crate::operations::OperationType,
        crate::random_io_generator::RandomIoGenerator, rand::rngs::mock::StepRng,
    };
    static MAGIC_NUMBER: u64 = 100;
    static PROCESS_ID: u64 = 101;
    static TARGET_ID: u64 = 102;
    static GENERATOR_ID: u64 = 103;
    static BLOCK_SIZE: u64 = 4096;

    #[test]
    fn test_operation_generation() {
        let mut generator = RandomIoGenerator::new(
            MAGIC_NUMBER,
            PROCESS_ID,
            TARGET_ID,
            GENERATOR_ID,
            BLOCK_SIZE * 2..BLOCK_SIZE * 12,
            BLOCK_SIZE,
            Box::new(StepRng::new(/*start=*/ 3, /*increment*/ 15)),
        );
        let operations = vec![OperationType::Read, OperationType::Write];

        assert_eq!(generator.generate_number(), 3);
        assert_eq!(generator.get_io_range(), BLOCK_SIZE * 5..BLOCK_SIZE * 6);
        assert_eq!(generator.get_io_operation(&operations), OperationType::Write);

        assert_eq!(generator.generate_number(), 18);
        assert_eq!(generator.get_io_range(), BLOCK_SIZE * 10..BLOCK_SIZE * 11);
        assert_eq!(generator.get_io_operation(&operations), OperationType::Read);
    }

    #[test]
    fn test_filled_buffer_starts_with_header() {
        let mut generator = RandomIoGenerator::new(
            MAGIC_NUMBER,
            PROCESS_ID,
            TARGET_ID,
            GENERATOR_ID,
            0..BLOCK_SIZE * 10,
            BLOCK_SIZE,
            Box::new(StepRng::new(/*start=*/ 1, /*increment*/ 1)),
        );

        let seed = generator.generate_number();
        let io_range = generator.get_io_range();
        let sequence_number = 10 as u64;

        let mut buf = vec![0 as u8; BLOCK_SIZE as usize];
        generator
            .fill_buffer(&mut buf, sequence_number, io_range.clone())
            .expect("Header written successfully");
        let header = IoHeader::read_header(&buf).expect("Header read successfully");

        let expected_header = IoHeader {
            magic_number: MAGIC_NUMBER,
            process_id: PROCESS_ID,
            fd_unique_id: TARGET_ID,
            generator_unique_id: GENERATOR_ID,
            io_op_unique_id: sequence_number,
            file_offset: io_range.start,
            size: BLOCK_SIZE,
            seed,
        };

        assert_eq!(header, expected_header);
    }
}
