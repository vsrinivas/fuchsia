// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Result},
    core::hash::Hasher,
    crc::crc32::{self, Hasher32},
    serde::{Deserialize, Serialize},
    std::io::Cursor,
};

/// Each block of written data contains a header. This header helps to verify
/// the written data. In the future this will also act as a poison value to detect
/// corruption.
#[derive(Clone, Debug, Serialize, Deserialize, Eq, PartialEq)]
pub struct IoHeader {
    /// The magic_number helps to identify that the block was written by odu.
    pub magic_number: u64,

    /// process_id helps to differentiate this run from other runs.
    pub process_id: u64,

    /// fd_unique_id tells what file descriptor was used to write this data.
    pub fd_unique_id: u64,

    /// generator_unique_id is a unique id of the generator that updated this block.
    pub generator_unique_id: u64,

    /// io_op_unique_id tells which io operation updated this block.
    pub io_op_unique_id: u64,

    // file_offset is the offset within the file where this data should be found.
    pub file_offset: u64,

    /// The size of the IO operation that wrote this header.
    pub size: u64,

    /// The seed that was used to generate the rest of the data in this block.
    pub seed: u64,
}

/// Adds a crc32 field to the IoHeader to make sure it wasn't corrupted when being written to or
/// read from the file.
#[derive(Clone, Debug, Serialize, Deserialize, Eq, PartialEq)]
struct IoHeaderWithCrc32 {
    header: IoHeader,
    crc32: u32,
}

impl IoHeader {
    pub fn size() -> u64 {
        let header = IoHeaderWithCrc32 {
            header: IoHeader {
                magic_number: 0,
                process_id: 0,
                fd_unique_id: 0,
                generator_unique_id: 0,
                io_op_unique_id: 0,
                file_offset: 0,
                size: 0,
                seed: 0,
            },
            crc32: 0,
        };
        bincode::serialized_size(&header).unwrap()
    }

    /// Try and read an IoHeader from |buf|.
    #[allow(dead_code)]
    pub fn read_header(buf: &[u8]) -> Result<Self> {
        let header: IoHeaderWithCrc32 = bincode::deserialize(buf)?;

        if header.crc32 != header.header.crc32() {
            return Err(anyhow!("Header was corrupted, crc32 did not match"));
        }

        Ok(header.header)
    }

    /// Write the header to |buf|.
    pub fn write_header(&self, buf: &mut [u8]) -> Result<()> {
        let header = IoHeaderWithCrc32 { header: self.clone(), crc32: self.crc32() };
        let mut cursor = Cursor::new(buf);
        bincode::serialize_into(&mut cursor, &header)?;
        Ok(())
    }

    fn crc32(&self) -> u32 {
        let mut crc = crc32::Digest::new(crc32::IEEE);
        crc.write_u64(self.magic_number);
        crc.write_u64(self.process_id);
        crc.write_u64(self.fd_unique_id);
        crc.write_u64(self.generator_unique_id);
        crc.write_u64(self.io_op_unique_id);
        crc.write_u64(self.file_offset);
        crc.write_u64(self.size);
        crc.write_u64(self.seed);
        crc.sum32()
    }
}

#[cfg(test)]
mod tests {
    use crate::io_header::IoHeader;

    static MAGIC_NUMBER: u64 = 0x1111_1111_1111_1111;
    static PROCESS_ID: u64 = 0x2222_2222_2222_2222;
    static FD_UNIQUE_ID: u64 = 0x3333_3333_3333_3333;
    static GENERATOR_UNIQUE_ID: u64 = 0x4444_4444_4444_4444;
    static IO_OP_UNIQUE_ID: u64 = 0x5555_5555_5555_5555;
    static FILE_OFFSET: u64 = 0x6666_6666_6666_6666;
    static IO_SIZE: u64 = 0x7777_7777_7777_7777;
    static SEED: u64 = 0x8888_8888_8888_8888;

    #[test]
    fn test_header_can_round_trip_through_buffer() {
        let header_in = IoHeader {
            magic_number: MAGIC_NUMBER,
            process_id: PROCESS_ID,
            fd_unique_id: FD_UNIQUE_ID,
            generator_unique_id: GENERATOR_UNIQUE_ID,
            io_op_unique_id: IO_OP_UNIQUE_ID,
            file_offset: FILE_OFFSET,
            size: IO_SIZE,
            seed: SEED,
        };
        let mut buf = vec![0; IoHeader::size() as usize];
        header_in.write_header(&mut buf).expect("Header written successfully");
        let header_out = IoHeader::read_header(&buf).expect("Header read successfully");

        assert_eq!(header_out, header_in);
    }

    #[test]
    fn test_write_header_to_too_small_of_buffer_fails() {
        let header = IoHeader {
            magic_number: MAGIC_NUMBER,
            process_id: PROCESS_ID,
            fd_unique_id: FD_UNIQUE_ID,
            generator_unique_id: GENERATOR_UNIQUE_ID,
            io_op_unique_id: IO_OP_UNIQUE_ID,
            file_offset: FILE_OFFSET,
            size: IO_SIZE,
            seed: SEED,
        };
        let mut buf = vec![0; (IoHeader::size() - 1) as usize];

        assert!(header.write_header(&mut buf).is_err());
    }

    #[test]
    fn test_reading_a_corrupted_header_fails() {
        let header = IoHeader {
            magic_number: MAGIC_NUMBER,
            process_id: PROCESS_ID,
            fd_unique_id: FD_UNIQUE_ID,
            generator_unique_id: GENERATOR_UNIQUE_ID,
            io_op_unique_id: IO_OP_UNIQUE_ID,
            file_offset: FILE_OFFSET,
            size: IO_SIZE,
            seed: SEED,
        };
        let mut buf = vec![0; IoHeader::size() as usize];
        header.write_header(&mut buf).expect("Header written successfully");

        // Corrupt the header.
        buf[0] = 0;

        let header = IoHeader::read_header(&buf);
        assert!(header.is_err());
    }
}
